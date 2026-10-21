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

// Package list contains the inctl asset catalog list command.
package list

import (
	"context"
	"fmt"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	catalogview "intrinsic/assets/inctl/catalog/view"
	"intrinsic/assets/interfaceutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/proto"

	acgrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	spb "intrinsic/assets/proto/v1/search_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

const (
	pageSize int64 = 50
)

func listAndPrintAssets(ctx context.Context, client acgrpcpb.AssetCatalogClient, view viewpb.AssetViewType, filter *acpb.ListAssetsRequest_AssetFilter, prtr printer.CommandPrinter) error {
	var orderBy spb.OrderBy
	if filter.GetId() == "" {
		orderBy = spb.OrderBy_ORDER_BY_ID
	}
	var nextPageToken string
	for {
		resp, err := client.ListAssets(ctx, &acpb.ListAssetsRequest{
			View:         view,
			OrderBy:      orderBy,
			PageToken:    nextPageToken,
			PageSize:     pageSize,
			StrictFilter: filter,
		})
		if err != nil {
			return fmt.Errorf("could not list Assets: %w", err)
		}

		for _, asset := range resp.GetAssets() {
			prtr.Println(catalogview.Of(asset))
		}

		nextPageToken = resp.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}

	return printer.Flush(prtr)
}

// Command returns the list command for the asset catalog.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "list [id]",
		Short: "List Assets from the catalog.",
		Example: `
  List Assets from the catalog:
  $ inctl asset catalog list

  List all versions of a specific Asset from the catalog:
  $ inctl asset catalog list some.package.my_skill
`,
		Args: cobra.MaximumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("cannot create client connection: %w", err)
			}
			defer conn.Close()
			client := acgrpcpb.NewAssetCatalogClient(conn)
			ot := printer.GetFlagOutputType(cmd)
			if ot == printer.OutputTypeText {
				ot = printer.OutputTypeTAB
			}
			cols := []string{"id", "version"}
			if len(args) == 1 {
				cols = []string{"version", "default", "org_private", "update_time"}
			}
			prtr, err := printer.NewPrinterOfType(ot, cmd, catalogview.TableConfig(cols...))
			if err != nil {
				return err
			}
			defer printer.Flush(prtr)

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

			flagView, err := flags.GetFlagView()
			if err != nil {
				return err
			}
			if flagView == viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED {
				if len(args) == 1 {
					flagView = viewpb.AssetViewType_ASSET_VIEW_TYPE_VERSIONS
				} else {
					flagView = viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC
				}
			}

			filter := &acpb.ListAssetsRequest_AssetFilter{
				AssetTypes: assetTypes,
				Provides:   provides,
			}
			if len(args) == 1 {
				filter.Id = proto.String(args[0])
			} else {
				filter.OnlyDefault = proto.Bool(true)
			}

			return listAndPrintAssets(ctx, client, flagView, filter, prtr)
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagAssetTypes("")
	flags.AddFlagOrganizationOptional()
	flags.AddFlagProvides()
	flags.AddFlagView()


	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()

	return cmd
}
