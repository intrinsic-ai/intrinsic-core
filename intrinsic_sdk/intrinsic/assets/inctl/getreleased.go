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

// Package getreleased defines the command to get information about released Assets.
package getreleased

import (
	"fmt"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	acgrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto" 
	acipb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"     
	idpb "intrinsic/assets/proto/id_go_proto"                                     
)

// GetCommand returns the command to get information about a released Asset.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:        "get_released id_version",
		Short:      "Get information about the specified Asset version from the AssetCatalog.",
		Deprecated: "Use inctl asset catalog get",
		Example: `
  $ inctl asset get_released some.package.my_skill.0.0.1
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ivp, err := idutils.NewIDVersionParts(args[0])
			if err != nil {
				return fmt.Errorf("failed to parse id_version: %v", err)
			}

			view, err := flags.GetFlagView()
			if err != nil {
				return fmt.Errorf("failed to parse view: %v", err)
			}

			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("failed to create client connection: %v", err)
			}
			defer conn.Close()

			client := acgrpcpb.NewAssetCatalogClient(conn)
			asset, err := client.GetAsset(ctx, &acpb.GetAssetRequest{
				AssetId: &acpb.GetAssetRequest_IdVersion{
					IdVersion: ivp.IDVersionProto(),
				},
				View: view,
			})
			if err != nil {
				return fmt.Errorf("failed to get Asset: %v", err)
			}

			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}
			prtr.Print(asset)

			return nil
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



const (
	keySkipUnavailable = "skip_unavailable"
)

// GetBatchCommand returns the command to get information about released Assets.
func GetBatchCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:        "get_released id_version_1 id_version_2 ...",
		Short:      "Get information about the specified Assets from the AssetCatalog.",
		Deprecated: "Use inctl asset catalog get (or inctl asset catalog internal get)",
		Example: `
  $ inctl asset get_released some.package.my_skill.0.0.1 some.package.my_other_skill.0.0.1
`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			var idVersions []*idpb.IdVersion
			for _, arg := range args {
				ivp, err := idutils.NewIDVersionParts(arg)
				if err != nil {
					return fmt.Errorf("failed to parse id_version: %v", err)
				}
				idVersions = append(idVersions, ivp.IDVersionProto())
			}

			view, err := flags.GetFlagView()
			if err != nil {
				return fmt.Errorf("failed to parse view: %v", err)
			}

			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("failed to create client connection: %v", err)
			}
			defer conn.Close()

			client := acigrpcpb.NewAssetCatalogInternalClient(conn)
			response, err := client.BatchGetAssetsInternal(ctx, &acipb.BatchGetAssetsRequest{
				IdVersions:      idVersions,
				SkipUnavailable: flags.GetBool(keySkipUnavailable),
				View:            view,
			})
			if err != nil {
				return fmt.Errorf("failed to get Assets: %v", err)
			}

			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}
			for _, asset := range response.GetAssets() {
				prtr.Print(asset)
			}
			if flags.GetBool(keySkipUnavailable) {
				prtr.Print("Unavailable assets:")
				for _, unavailableAsset := range response.GetUnavailableAssets() {
					prtr.Print(unavailableAsset)
				}
			}

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagOrganizationOptional()
	flags.AddFlagView()

	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()

	flags.OptionalBool(keySkipUnavailable, false, "Skip assets that are not available. If true, then the reasons for unavailable assets will be included in the output.")

	return cmd
}


