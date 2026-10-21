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

// Package listreleased defines the list_released command that lists assets in catalog.
package listreleased

import (
	"fmt"
	"sort"
	"strings"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	"intrinsic/assets/listutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/proto"

	acgrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

const pageSize int64 = 50

// GetCommand returns a command to list released assets.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:        "list_released",
		Short:      "List assets from the catalog.",
		Deprecated: "Use inctl asset catalog list",
		Args:       cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("cannot create client connection: %w", err)
			}
			defer conn.Close()
			client := acgrpcpb.NewAssetCatalogClient(conn)
			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}

			assetTypes, err := flags.GetFlagAssetTypes()
			if err != nil {
				return err
			}

			provides, err := flags.GetFlagProvides()
			if err != nil {
				return err
			}
			for _, p := range provides {
				if err := interfaceutils.ValidateInterfaceName(p); err != nil {
					return fmt.Errorf("invalid --%s filter (must use protocol prefix, e.g., 'grpc://intrinsic_proto.services.Calculator'): %w", cmdutils.KeyProvides, err)
				}
			}

			assets, err := listutils.ListAllAssets(
				ctx,
				client,
				pageSize,
				viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC,
				&acpb.ListAssetsRequest_AssetFilter{
					AssetTypes:  assetTypes,
					OnlyDefault: proto.Bool(true),
					Provides:    provides,
				},
			)
			if err != nil {
				return err
			}
			idVersions := make([]string, len(assets))
			for i, asset := range assets {
				idVersion, err := idutils.IDVersionFromProto(asset.GetMetadata().GetIdVersion())
				if err != nil {
					return err
				}
				idVersions[i] = idVersion
			}
			sort.Strings(idVersions)
			prtr.Print(strings.Join(idVersions, "\n"))

			return nil
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagAssetTypes("")
	flags.AddFlagOrganizationOptional()
	flags.AddFlagProvides()


	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()

	return cmd
}
