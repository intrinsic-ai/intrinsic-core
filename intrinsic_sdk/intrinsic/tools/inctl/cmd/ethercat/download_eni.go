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
	"io"
	"os"

	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"

	"github.com/spf13/cobra"

	dagrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	daspb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	enipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/eni_go_proto"
)

var (
	msg                   enipb.Eni
	eniDataAssetProtoName = string(msg.ProtoReflect().Descriptor().FullName())
)

func getDownloadEniCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "download_eni <asset_id>",
		Short: "Download ENI file from an installed Data Asset.",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return runDownloadEni(cmd, args, flags)
		},
		Example: `
	# Download ENI file and write to stdout
  inctl ethercat download_eni my.eni.asset.id

  # Download ENI file and write to a file
  inctl ethercat download_eni my.eni.asset.id --output_file /tmp/my.eni`,
	}
	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	flags.OptionalString("output_file", "", "Path to save the ENI file.")

	return cmd
}

func runDownloadEni(cmd *cobra.Command, args []string, flags *cmdutils.CmdFlags) error {
	ctx := cmd.Context()
	assetID := args[0]
	outputFile := flags.GetString("output_file")

	ctx, conn, _, err := clientutilsDialClusterFromInctl(ctx, flags)
	if err != nil {
		return fmt.Errorf("dial cluster: %w", err)
	}
	defer conn.Close()

	daClient := dagrpcpb.NewDataAssetsClient(conn)

	return downloadEni(ctx, daClient, assetID, outputFile, cmd.OutOrStdout())
}

func downloadEni(ctx context.Context, daClient dagrpcpb.DataAssetsClient, assetID, outputFile string, w io.Writer) error {
	idProto, err := idutils.IDProtoFromString(assetID)
	if err != nil {
		return fmt.Errorf("can't parse Asset id %q: %w", assetID, err)
	}
	getReq := &daspb.GetDataAssetRequest{
		Id: idProto,
	}
	getRes, err := daClient.GetDataAsset(ctx, getReq)
	if err != nil {
		return fmt.Errorf("failed to get Data Asset %q: %w", assetID, err)
	}

	eniProto := &enipb.Eni{}
	if err := getRes.GetData().UnmarshalTo(eniProto); err != nil {
		return fmt.Errorf("failed to unmarshal ENI proto for Asset %q: %w", assetID, err)
	}

	if outputFile != "" {
		if err := os.WriteFile(outputFile, []byte(eniProto.GetData()), 0o644); err != nil {
			return fmt.Errorf("failed to write ENI data to %q: %w", outputFile, err)
		}
		fmt.Printf("ENI data written to %s\n", outputFile)
	} else {
		fmt.Fprintln(w, eniProto.GetData())
	}

	return nil
}

func init() {
	EtherCATCmd.AddCommand(getDownloadEniCommand())
}
