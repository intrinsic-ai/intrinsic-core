// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package ethercat

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/types/known/anypb"

	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/util/proto/descriptor"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	ipb "intrinsic/assets/proto/id_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	vpb "intrinsic/assets/proto/vendor_go_proto"
	enipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/eni_go_proto"
)

const (
	_ = 1 << (10 * iota)
	KB
	MB
)

const (
	maxFileSizeWarning = 20 * MB
	lroWaitTimeout     = 5 * time.Minute
)

func getUploadEniCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "upload_eni <file_path> <asset_id>",
		Short: "Upload ENI file as a Data asset.",
		Args:  cobra.ExactArgs(2),
		RunE: func(cmd *cobra.Command, args []string) error {
			return runUploadENI(cmd, args, flags)
		},
		Example: `
  # Upload ENI file
  inctl ethercat upload_eni /path/to/my.eni ai.intrinsic.fieldbus.ethercat.test.full_eni

  # Upload ENI file with custom display name and vendor
  inctl ethercat upload_eni /path/to/my.eni ai.intrinsic.fieldbus.ethercat.test.full_eni --display_name "My ENI" --vendor "MyVendor"`,
	}
	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagPolicy("data")
	flags.AddFlagsProjectOrg()

	cmd.Flags().String("display_name", "", "Display name for the asset (default is file basename).")
	cmd.Flags().String("vendor", "undefined", "Vendor for the asset (default is `undefined`).")

	return cmd
}

func runUploadENI(cmd *cobra.Command, args []string, flags *cmdutils.CmdFlags) error {
	ctx := cmd.Context()
	filePath := args[0]
	assetIDStr := args[1]

	fileInfo, err := os.Stat(filePath)
	if err != nil {
		return fmt.Errorf("could not stat ENI file %q: %w", filePath, err)
	}

	if fileInfo.Size() > maxFileSizeWarning {
		fmt.Printf("WARNING: ENI file %q is unusually large (%.2f MB). Upload may take a while or fail due to memory limits.\n", filePath, float64(fileInfo.Size())/MB)
	}

	policy, err := flags.GetFlagPolicy()
	if err != nil {
		return fmt.Errorf("get policy: %w", err)
	}

	displayName, err := cmd.Flags().GetString("display_name")
	if err != nil {
		return fmt.Errorf("get display_name flag: %w", err)
	}
	if displayName == "" {
		displayName = filepath.Base(filePath)
	}
	vendorName, err := cmd.Flags().GetString("vendor")
	if err != nil {
		return fmt.Errorf("get vendor flag: %w", err)
	}
	content, err := os.ReadFile(filePath)
	if err != nil {
		return fmt.Errorf("could not read ENI file %q: %w", filePath, err)
	}

	if err := validateENI(content); err != nil {
		return fmt.Errorf("ENI file validation failed: %w", err)
	}

	idProto, err := idutils.IDProtoFromString(assetIDStr)
	if err != nil {
		return fmt.Errorf("can't parse Asset id %q: %w", assetIDStr, err)
	}

	eniProto := &enipb.Eni{
		Data: string(content),
	}

	eniAny, err := anypb.New(eniProto)
	if err != nil {
		return fmt.Errorf("could not marshal ENI proto: %w", err)
	}

	da := &dapb.DataAsset{
		Metadata: &metadatapb.Metadata{
			IdVersion:   &ipb.IdVersion{Id: idProto},
			DisplayName: displayName,
			AssetType:   atpb.AssetType_ASSET_TYPE_DATA,
			Vendor:      &vpb.Vendor{DisplayName: vendorName},
		},
		Data:              eniAny,
		FileDescriptorSet: descriptor.FileDescriptorSetFrom(eniProto),
	}

	ctx, conn, _, err := clientutilsDialClusterFromInctl(ctx, flags)
	if err != nil {
		return fmt.Errorf("dial cluster: %w", err)
	}
	defer conn.Close()

	client := iagrpcpb.NewInstalledAssetsClient(conn)

	fmt.Printf("Installing ENI asset %q...\n", assetIDStr)
	op, err := installAsset(ctx, client, da, policy)
	if err != nil {
		return fmt.Errorf("failed to install asset: %w", err)
	}

	fmt.Println("Awaiting completion of the installation...")

	waitCtx, cancel := context.WithTimeout(ctx, lroWaitTimeout)
	defer cancel()

	if err := waitForOperation(waitCtx, conn, op); err != nil {
		if waitCtx.Err() == context.DeadlineExceeded {
			return fmt.Errorf("upload is queued but taking longer than expected (%v). Check the UI for status", lroWaitTimeout)
		}
		return fmt.Errorf("installation failed: %w", err)
	}

	fmt.Printf("Successfully installed ENI asset %q\n", assetIDStr)
	return nil
}

func init() {
	EtherCATCmd.AddCommand(getUploadEniCommand())
}
