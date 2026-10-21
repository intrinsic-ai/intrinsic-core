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

// Package install defines the skill command which installs a skill.
package install

import (
	"fmt"
	"log"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/assets/imagetransfer"
	"intrinsic/assets/services/bundleimages"
	"intrinsic/skills/skillbundle"
	"intrinsic/skills/tools/skill/cmd/directupload/directupload"
	"intrinsic/skills/tools/skill/cmd/waitforskill"

	"github.com/spf13/cobra"
	"google.golang.org/grpc/status"

	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// Command returns the command for installing a skill.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "install <bundle>",
		Short: "Install a skill",
		Example: `Upload skill image to a container registry, and install the skill
$ inctl skill install abc/skill.bundle.tar --registry=gcr.io/my-registry --cluster=my_cluster

Use the solution flag to automatically resolve the cluster (requires the solution to run)
$ inctl skill install abc/skill.bundle.tar --solution=my-solution
`,
		Args: cobra.ExactArgs(1),
		Aliases: []string{
			"load",
			"start",
		},
		RunE: func(command *cobra.Command, args []string) error {
			ctx := command.Context()
			target := args[0]

			policy, err := flags.GetFlagPolicy()
			if err != nil {
				return err
			}

			timeout, timeoutStr, err := flags.GetFlagSideloadStartTimeout()
			if err != nil {
				return err
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			// Determine the image transferer to use. Default to direct injection into the cluster.
			var transfer imagetransfer.Transferer
			if registry := flags.GetFlagRegistry(); registry != "" {
				user, pwd := flags.GetFlagsRegistryAuthUserPassword()
				transfer = imagetransfer.RemoteTransferer(registry, user, pwd)
			}
			if !flags.GetFlagSkipDirectUpload() {
				transfer = directupload.NewTransferer(
					directupload.WithDiscovery(directupload.NewFromConnection(conn)),
					directupload.WithOutput(command.OutOrStdout()),
					directupload.WithFailOver(transfer),
				)
			}
			if transfer == nil {
				return fmt.Errorf("--registry must be specified if --skip-direct-upload is used")
			}
			manifest, err := skillbundle.ProcessFile(ctx, target,
				skillbundle.WithImageProcessor(bundleimages.CreateImageProcessor(transfer)),
			)
			if err != nil {
				return fmt.Errorf("could not read bundle file %q: %v", target, err)
			}

			id, err := idutils.IDFromProto(manifest.GetMetadata().GetId())
			if err != nil {
				return fmt.Errorf("invalid id: %v", err)
			}
			log.Printf("Installing skill %q", id)

			client := iagrpcpb.NewInstalledAssetsClient(conn)
			// This needs an authorized context to pull from the catalog if not available.
			op, err := client.CreateInstalledAsset(ctx, &iapb.CreateInstalledAssetRequest{
				Policy: policy,
				Asset: &iapb.CreateInstalledAssetRequest_Asset{
					Variant: &iapb.CreateInstalledAssetRequest_Asset_Skill{
						Skill: manifest,
					},
				},
			})
			if err != nil {
				return fmt.Errorf("could not install the skill: %v", err)
			}

			log.Printf("Awaiting completion of the installation")
			lroClient := lrogrpcpb.NewOperationsClient(conn)
			for !op.GetDone() {
				op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
					Name: op.GetName(),
				})
				if err != nil {
					return fmt.Errorf("unable to check status of installation: %v", err)
				}
			}

			if err := status.ErrorProto(op.GetError()); err != nil {
				return fmt.Errorf("installation failed: %w", err)
			}

			log.Printf("Finished installing %q", id)

			if timeout == 0 {
				return nil
			}

			asset := &iapb.InstalledAsset{}
			if err := op.GetResponse().UnmarshalTo(asset); err != nil {
				return fmt.Errorf("unable to interpret the response: %w", err)
			}

			log.Printf("Waiting for the skill to be available for a maximum of %s", timeoutStr)
			if err := waitforskill.WaitForSkill(ctx, &waitforskill.Params{
				Connection:     conn,
				SkillID:        idutils.IDFromProtoUnchecked(asset.GetMetadata().GetIdVersion().GetId()),
				SkillIDVersion: idutils.IDVersionFromProtoUnchecked(asset.GetMetadata().GetIdVersion()),
				WaitDuration:   timeout,
			}); err != nil {
				return fmt.Errorf("failed waiting for skill: %w", err)
			}
			log.Printf("The skill is now available.")

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagPolicy("skill")
	flags.AddFlagsProjectOrg()
	flags.AddFlagRegistry()
	flags.AddFlagsRegistryAuthUserPassword()
	flags.AddFlagSideloadStartTimeout("skill")
	flags.AddFlagSkipDirectUpload("skill")

	return cmd
}
