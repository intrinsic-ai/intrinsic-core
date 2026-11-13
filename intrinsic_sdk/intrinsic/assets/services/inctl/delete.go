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
	"intrinsic/assets/errors/report"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/util/status/extstatus"

	"github.com/spf13/cobra"
	"google.golang.org/grpc/status"

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

Delete a service instance, failing on any Solution validation issue
$ inctl service delete --project=my_project --cluster=some_cluster \
    --validation_policy=strict my_instance
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
			validationPolicy, err := flags.GetFlagValidationPolicy()
			if err != nil {
				return err
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("could not create connection to cluster: %w", err)
			}
			defer conn.Close()

			log.Printf("Requesting deletion of %q", name)
			client := adgrpcpb.NewAssetDeploymentServiceClient(conn)
			op, err := client.DeleteResource(ctx, &adpb.DeleteResourceRequest{
				Name:             name,
				ValidationPolicy: validationPolicy,
			})
			if err != nil {
				return wrapError(fmt.Sprintf("could not delete service %q", name), err)
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

			if err := status.ErrorProto(op.GetError()); err != nil {
				return wrapError(fmt.Sprintf("failed to delete %q", name), err)
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
	flags.AddFlagValidationPolicy()

	return cmd
}

// wrapError checks if err carries Solution validation errors, and wraps it with those
// details. Otherwise it just annotates err with msg.
func wrapError(msg string, err error) error {
	es, ok := extstatus.FromGRPCError(err)
	if !ok {
		return fmt.Errorf("%s: %w", msg, err)
	}
	sc := es.Proto().GetStatusCode()
	if sc.GetComponent() != report.ComponentName || sc.GetCode() != report.StatusCode {
		return fmt.Errorf("%s: %w", msg, err)
	}
	return fmt.Errorf("%s: Solution validation failed:\n\n%s\n"+
		"Fix these errors and retry, or re-run with --validation_policy=recoverable to proceed "+
		"despite non-blocking validation issues.", msg, color.C.Red().Sprintf("%s", es.String()))
}
