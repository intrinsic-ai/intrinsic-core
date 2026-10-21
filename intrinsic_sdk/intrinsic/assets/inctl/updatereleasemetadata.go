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

// Package updatereleasemetadata defines the command to update the metadata of a released asset.
package updatereleasemetadata

import (
	"bufio"
	"fmt"
	"os"
	"unicode"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	acgrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	rmpb "intrinsic/assets/catalog/proto/v1/release_metadata_go_proto"

	fmpb "google.golang.org/protobuf/types/known/fieldmaskpb"
)

// GetCommand returns the command to get asset deployment data.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:        "update_release_metadata id_version",
		Short:      "Update the release metadata of the specified asset id_version.",
		Deprecated: "Use inctl asset catalog update-release-metadata",
		Example: `
  $ inctl asset update_release_metadata some.package.my_skill.0.0.1 --org_private=false
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ivp, err := idutils.NewIDVersionParts(args[0])
			if err != nil {
				return fmt.Errorf("failed to parse id_version: %v", err)
			}

			ctx := cmd.Context()
			ctx, conn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("failed to create client connection: %v", err)
			}
			defer conn.Close()

			rm := &rmpb.ReleaseMetadata{}
			var updateMask []string
			if flags.GetFlagOrgPrivateIsSet() {
				op := flags.GetFlagOrgPrivate()
				if !op && !flags.GetFlagSkipPrompts() {
					fmt.Println("WARNING: org_private cannot be set to true after being set to false.")
					fmt.Println("Do you want to continue? [y/N] ")
					reader := bufio.NewReader(os.Stdin)
					input, _, err := reader.ReadRune()
					if err != nil {
						return fmt.Errorf("could not read response: %v", err)
					}
					if unicode.ToLower(input) != 'y' {
						return fmt.Errorf("aborted")
					}
				}

				rm.OrgPrivate = op
				updateMask = append(updateMask, "org_private")
			}
			if flags.GetFlagDefaultIsSet() {
				rm.Default = flags.GetFlagDefault()
				updateMask = append(updateMask, "default")
			}

			client := acgrpcpb.NewAssetCatalogClient(conn)
			newRM, err := client.UpdateReleaseMetadata(ctx, &acpb.UpdateReleaseMetadataRequest{
				IdVersion:       ivp.IDVersionProto(),
				ReleaseMetadata: rm,
				UpdateMask:      &fmpb.FieldMask{Paths: updateMask},
			})
			if err != nil {
				return fmt.Errorf("failed to update release metadata: %v", err)
			}

			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}
			prtr.Print(newRM)

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagDefault("asset")
	flags.AddFlagOrganizationOptional()
	flags.AddFlagOrgPrivate()
	flags.AddFlagSkipPrompts()


	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()


	return cmd
}
