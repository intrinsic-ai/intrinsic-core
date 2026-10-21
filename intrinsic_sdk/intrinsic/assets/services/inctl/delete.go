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

// Package delete defines the command which deletes a service instance from the
// solution.
package delete

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"time"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/util/status/extstatus"

	"github.com/spf13/cobra"

	adgrpcpb "intrinsic/assets/proto/asset_deployment_go_proto"
	adpb "intrinsic/assets/proto/asset_deployment_go_proto"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// GetCommand returns a command to delete a service instance from a solution.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "delete name",
		Short: "Delete a service instance from a solution",
		Example: `
Delete a service instance with the specified name
$ inctl service delete --project=my_project --cluster=some_cluster my_instance
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := agents.Check(cmd); err != nil {
				return err
			}
			// Generally try to cancel calls if the user hits ctrl-c
			ctx, stop := signal.NotifyContext(cmd.Context(), os.Interrupt)
			defer stop()
			name := args[0]

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("could not create connection to cluster: %w", err)
			}
			defer conn.Close()

			log.Printf("Requesting deletion of %q", name)
			client := adgrpcpb.NewAssetDeploymentServiceClient(conn)
			op, err := client.DeleteResource(ctx, &adpb.DeleteResourceRequest{
				Name: name,
			})
			if err != nil {
				return fmt.Errorf("could not delete service %q: %v", name, err)
			}

			lroClient := lrogrpcpb.NewOperationsClient(conn)
			defer func() {
				if !op.GetDone() {
					log.Printf("Cancelling unfinished operation")
					// Assume ctx has been cancelled if we're here.
					ctx, cancel := context.WithTimeout(cmd.Context(), 1*time.Second)
					defer cancel()
					if _, err = lroClient.CancelOperation(ctx, &lropb.CancelOperationRequest{
						Name: op.GetName(),
					}); err != nil {
						log.Printf("Cancelling failed: %v", err)
					}
				}
			}()
			log.Printf("Awaiting completion of the delete operation")
			for !op.GetDone() {
				op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
					Name: op.GetName(),
				})
				if err != nil {
					return fmt.Errorf("unable to check status of delete operation for %q: %v", name, err)
				}
			}

			if err := op.GetError(); err != nil {
				return fmt.Errorf("failed to delete %q: %v", name, err)
			}

			log.Printf("Deleted service %q", name)

			if op.GetMetadata() != nil {
				metadata := &adpb.DeleteResourceMetadata{}
				if err := op.GetMetadata().UnmarshalTo(metadata); err != nil {
					log.Printf("failed to check for warnings: failed to unmarshal operation metadata: %v", err)
				} else if metadata.GetWarnings() != nil {
					ext := extstatus.FromProto(metadata.GetWarnings())
					color.C.Yellow().Printf("\nWARNING: Deletion of %q succeeded with warnings:\n%v\n", name, ext)
				}
			}
			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()

	return cmd
}
