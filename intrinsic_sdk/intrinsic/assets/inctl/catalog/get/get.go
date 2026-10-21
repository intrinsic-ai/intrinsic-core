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

// Package get contains the inctl asset catalog get command.
package get

import (
	"context"
	"fmt"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	catalogview "intrinsic/assets/inctl/catalog/view"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	acgrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

func getAndPrintAsset(ctx context.Context, client acgrpcpb.AssetCatalogClient, target string, view viewpb.AssetViewType, prtr printer.CommandPrinter) error {
	var req acgrpcpb.GetAssetRequest
	req.View = view
	if ivp, err := idutils.NewIDVersionParts(target); err == nil {
		req.AssetId = &acgrpcpb.GetAssetRequest_IdVersion{
			IdVersion: ivp.IDVersionProto(),
		}
	} else if idProto, errID := idutils.IDProtoFromString(target); errID == nil {
		req.AssetId = &acgrpcpb.GetAssetRequest_Id{
			Id: idProto,
		}
	} else {
		return fmt.Errorf("failed to parse id or id_version: %w", err)
	}

	asset, err := client.GetAsset(ctx, &req)
	if err != nil {
		return fmt.Errorf("failed to get Asset: %w", err)
	}

	prtr.Println(catalogview.Of(asset))
	return nil
}

// Command returns the get command for the asset catalog.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:   "get <id|id_version>",
		Short: "Get information about the specified Asset from the AssetCatalog.",
		Example: `
  $ inctl asset catalog get some.package.my_skill.0.0.1
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			flagView, err := flags.GetFlagView()
			if err != nil {
				return fmt.Errorf("failed to parse view: %w", err)
			}

			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("failed to create client connection: %w", err)
			}
			defer conn.Close()

			client := acgrpcpb.NewAssetCatalogClient(conn)
			prtr, err := printer.NewPrinterFromCommand(cmd)
			if err != nil {
				return err
			}
			defer printer.Flush(prtr)

			if flagView == viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED {
				flagView = viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL
			}

			return getAndPrintAsset(ctx, client, args[0], flagView, prtr)
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagOrganizationOptional()
	flags.AddFlagView()


	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()


	return cmd
}
