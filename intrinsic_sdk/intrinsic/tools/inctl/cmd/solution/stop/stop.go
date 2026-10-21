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

// Package stop provides a command to stop a solution.
package stop

import (
	"context"
	"fmt"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc"
	"google.golang.org/grpc/status"

	solutiondeploymentpb "intrinsic/assets/proto/v1/solution_deployment_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

func stopSolution(ctx context.Context, conn *grpc.ClientConn) error {
	client := solutiondeploymentpb.NewSolutionDeploymentServiceClient(conn)
	op, err := client.DeleteSolutionDeployment(ctx, &solutiondeploymentpb.DeleteSolutionDeploymentRequest{})
	if err != nil {
		return fmt.Errorf("failed to stop solution: %w", err)
	}

	name := op.GetName()
	lroClient := lropb.NewOperationsClient(conn)
	for !op.GetDone() {
		op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
			Name: name,
		})
		if err != nil {
			return fmt.Errorf("unable to check status of solution stop operation %q: %w", name, err)
		}
	}

	if err := status.ErrorProto(op.GetError()); err != nil {
		return fmt.Errorf("solution stop operation %q failed: %w", name, err)
	}

	return nil
}

// NewCommand returns the solution stop command.
func NewCommand() *cobra.Command {
	viperLocal := viper.New()
	flags := cmdutils.NewCmdFlagsWithViper(viperLocal)

	solutionStopCmd := &cobra.Command{
		Use:   "stop",
		Short: "Stop the solution running on a cluster",
		Long:  "Stop the solution running on a given cluster",
		Args:  cobra.ExactArgs(0),
		RunE: func(cmd *cobra.Command, args []string) error {
			printer, err := printer.NewPrinter(cmd.Flags().Lookup("output").Value.String())
			if err != nil {
				return err
			}

			ctx := cmd.Context()

			_, clusterFlag, _, err := flags.GetFlagsAddressClusterSolution()
			if err != nil {
				return err
			}
			printer.PrintSf("Stopping solution on cluster '%s'\n", clusterFlag)

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			if err = stopSolution(ctx, conn); err != nil {
				return err
			}
			return nil
		},
	}

	flags.SetCommand(solutionStopCmd)
	flags.AddFlagsProjectOrg()
	flags.AddFlagsAddressClusterSolution()

	return solutionStopCmd
}
