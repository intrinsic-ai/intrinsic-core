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

// Package get contains the inctl asset catalog internal get command.
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

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	acipb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

const (
	keySkipUnavailable = "skip_unavailable"
)

func getInternalAndPrintAssets(ctx context.Context, client acigrpcpb.AssetCatalogInternalClient, args []string, view viewpb.AssetViewType, skipUnavailable bool, prtr printer.CommandPrinter) error {
	var idVersions []*idpb.IdVersion
	for _, arg := range args {
		ivp, err := idutils.NewIDVersionParts(arg)
		if err != nil {
			return fmt.Errorf("failed to parse id_version: %w", err)
		}
		idVersions = append(idVersions, ivp.IDVersionProto())
	}

	response, err := client.BatchGetAssetsInternal(ctx, &acipb.BatchGetAssetsRequest{
		IdVersions:      idVersions,
		SkipUnavailable: skipUnavailable,
		View:            view,
	})
	if err != nil {
		return fmt.Errorf("failed to get Assets: %w", err)
	}

	for idx, asset := range response.GetAssets() {
		if idx > 0 {
			prtr.Println()
		}
		prtr.Println(catalogview.Of(asset))
	}
	if skipUnavailable && len(response.GetUnavailableAssets()) > 0 {
		prtr.PrintErrln("Unavailable Assets:")
		for _, unavailableAsset := range response.GetUnavailableAssets() {
			idv := idutils.IDVersionFromProtoUnchecked(unavailableAsset.GetIdVersion())
			if reason := unavailableAsset.GetReason(); reason != "" {
				prtr.PrintErrf(" - %v: %v\n", idv, reason)
			} else {
				prtr.PrintErrf(" - %v\n", idv)
			}
		}
	}

	return nil
}

// Command returns the internal get command.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:   "get <id_version_1> [id_version_2 ...]",
		Short: "Get information about the specified Assets from the AssetCatalog.",
		Example: `
  $ inctl asset catalog internal get some.package.my_skill.0.0.1
  $ inctl asset catalog internal get some.package.my_skill.0.0.1 some.package.my_other_skill.0.0.1
`,
		Args: cobra.MinimumNArgs(1),
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

			client := acigrpcpb.NewAssetCatalogInternalClient(conn)
			prtr, err := printer.NewPrinterFromCommand(cmd)
			if err != nil {
				return err
			}
			defer printer.Flush(prtr)

			if flagView == viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED {
				flagView = viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL
			}

			return getInternalAndPrintAssets(ctx, client, args, flagView, flags.GetBool(keySkipUnavailable), prtr)
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagOrganizationOptional()
	flags.AddFlagView()
	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()
	flags.OptionalBool(keySkipUnavailable, false, "Skip Assets that are not available. If true, then the reasons for unavailable Assets will be included in the output.")

	return cmd
}
