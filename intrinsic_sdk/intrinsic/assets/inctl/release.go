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

// Package release defines the command that releases an asset to the catalog.
package release

import (
	"fmt"

	"intrinsic/assets/catalog/releaseasset"
	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/imagetransfer"
	"intrinsic/assets/imageutils"
	"intrinsic/assets/throttle"
	"intrinsic/skills/tools/skill/cmd/directupload/directupload"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
)

// GetCommand returns command to release an asset.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()

	cmd := &cobra.Command{
		Use:   "release bundle.tar",
		Short: "Release an Asset to the catalog.",
		Example: `
  Release an Asset to the catalog
  $ inctl asset release abc/bundle.tar --version=0.0.1
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			printer, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}

			ctx := cmd.Context()
			ctx, rawConn, err := clientutils.DialCatalogFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("failed to create client connection: %v", err)
			}
			defer rawConn.Close()

			conn := throttle.ConnectionWithRateLimit(rawConn, flags.GetFlagRateLimit(), flags.GetFlagRateBurst())

			var transferer imagetransfer.Transferer

			registry := imageutils.GetRegistry(clientutils.ResolveCatalogProjectFromInctl(flags))
			user, pwd := flags.GetFlagsRegistryAuthUserPassword()
			transferer = imagetransfer.RemoteTransferer(registry, user, pwd)

			if !flags.GetFlagSkipDirectUpload() { 
				transferer = directupload.NewTransferer(
					directupload.WithDiscovery(directupload.NewCatalogTarget(conn)),
					directupload.WithOutput(cmd.OutOrStdout()),
					directupload.WithFailOver(transferer),
					directupload.WithCatalogOptions(flags.GetFlagImageUploadParallelism()), // this allows uploading images with max size of the single layer of 2GiB.
					directupload.WithRegistry(imageutils.GetRegistry(clientutils.ResolveCatalogProjectFromInctl(flags))),
				)
			}

			return releaseasset.FromBundle(ctx, args[0],
				releaseasset.WithConnection(conn),
				releaseasset.WithDryRun(flags.GetFlagDryRun()),
				releaseasset.WithFlagDefault(flags.GetFlagDefault()),
				releaseasset.WithFlagOrgPrivate(flags.GetFlagOrgPrivate()),
				releaseasset.WithIgnoreExisting(flags.GetFlagIgnoreExisting()),
				releaseasset.WithImageTransferer(transferer),
				releaseasset.WithPrinter(printer.PrintSf),
				releaseasset.WithProcessingConcurrencyLimiter(throttle.NewConcurrencyLimiter(flags.GetFlagProcessingConcurrency())),
				releaseasset.WithReleaseNotes(flags.GetFlagReleaseNotes()),
				releaseasset.WithVersion(flags.GetFlagVersion()),
				releaseasset.WithProgressWriter(cmd.OutOrStdout()),
			)
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagDefault("asset")
	flags.AddFlagDryRun()
	flags.AddFlagIgnoreExisting("asset")
	flags.AddFlagImageUploadParallelism(1)
	flags.AddFlagOrganizationOptional()
	flags.AddFlagOrgPrivate()
	flags.AddFlagsRateLimit(throttle.CloudRateLimit, throttle.CloudBurst)
	flags.AddFlagProcessingConcurrency(throttle.ReleaseProcessingConcurrency)
	flags.AddFlagReleaseNotes("asset")
	flags.AddFlagVersion("asset")


	flags.AddFlagCatalogAddress()
	flags.AddFlagCatalogProjectOptional()
	flags.AddFlagsCredentials()
	flags.AddFlagSkipDirectUpload("asset")


	return cmd
}
