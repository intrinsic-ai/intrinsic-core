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

// Package uninstall defines the command to uninstall an asset.
package uninstall

import (
	"fmt"
	"log"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"

	"github.com/spf13/cobra"
	"google.golang.org/grpc/status"

	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// GetCommand returns a command to uninstall an asset.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "uninstall <id>",
		Short: "Uninstall an asset (Note: This will fail if there are instances of it in the solution.)",
		Example: `
  $ inctl asset uninstall ai.intrinsic.box \
      --project my_project \
      --solution my_solution_id

  To find a running solution's id, run:
  $ inctl solution list --project my-project --filter "running_on_hw,running_in_sim" --output json

  Can also use:
  $ inctl asset uninstall <id> --project my_project --address my_address
  or
  $ inctl asset uninstall <id> --project my_project --cluster my_cluster
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := cmd.Context()
			idString := args[0]
			id, err := idutils.IDProtoFromString(idString)
			if err != nil {
				return err
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("could not connect to cluster: %w", err)
			}
			defer conn.Close()

			client := iagrpcpb.NewInstalledAssetsClient(conn)
			op, err := client.DeleteInstalledAsset(ctx, &iapb.DeleteInstalledAssetRequest{
				Asset: id,
			})
			if err != nil {
				return fmt.Errorf("could not uninstall the asset: %w", err)
			}

			log.Printf("Awaiting completion of the uninstallation")
			lroClient := lrogrpcpb.NewOperationsClient(conn)
			for !op.GetDone() {
				op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
					Name: op.GetName(),
				})
				if err != nil {
					return fmt.Errorf("waiting for uninstallation failed: %w", err)
				}
			}

			if err := status.ErrorProto(op.GetError()); err != nil {
				return fmt.Errorf("uninstalling failed: %w", err)
			}
			log.Printf("Finished uninstalling %q", idString)

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()

	return cmd
}
