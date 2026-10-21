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

// Package start provides a command to start a solution.
package start

import (
	"context"
	"fmt"
	"log"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/config/operationmode"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/util/status/extstatus"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	solutiondeploymentpb "intrinsic/assets/proto/v1/solution_deployment_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

func startSolution(ctx context.Context, conn *grpc.ClientConn, solutionID string, operationMode string) error {
	client := solutiondeploymentpb.NewSolutionDeploymentServiceClient(conn)
	opMode := operationmode.FromString(operationMode)
	if opMode == opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED {
		return fmt.Errorf("invalid operation mode: %q", operationMode)
	}
	op, err := client.CreateSolutionDeploymentFromVersionedSolution(ctx, &solutiondeploymentpb.CreateSolutionDeploymentFromVersionedSolutionRequest{
		SolutionId:    solutionID,
		OperationMode: opMode,
	})
	if status.Code(err) == codes.FailedPrecondition {
		return fmt.Errorf("cannot start solution %q: %w", solutionID, err)
	}
	if err != nil {
		return fmt.Errorf("failed to start solution: %w", err)
	}

	name := op.GetName()
	lroClient := lropb.NewOperationsClient(conn)
	for !op.GetDone() {
		op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
			Name: name,
		})
		if err != nil {
			return fmt.Errorf("unable to check status of solution start operation %q: %w", name, err)
		}
	}

	if err := status.ErrorProto(op.GetError()); err != nil {
		return fmt.Errorf("solution start operation %q failed: %w", name, err)
	}

	if op.GetMetadata() != nil {
		metadata := &solutiondeploymentpb.CreateSolutionDeploymentFromVersionedSolutionMetadata{}
		if err := op.GetMetadata().UnmarshalTo(metadata); err != nil {
			log.Printf("failed to check for warnings: failed to unmarshal operation metadata: %v", err)
		} else if metadata.GetWarnings() != nil {
			ext := extstatus.FromProto(metadata.GetWarnings())
			color.C.Yellow().Printf("\nWARNING: Solution started with warnings:\n%v\n", ext)
		}
	}
	return nil
}

// NewCommand returns the solution start command.
func NewCommand() *cobra.Command {
	var flagOperationMode string

	viperLocal := viper.New()
	flags := cmdutils.NewCmdFlagsWithViper(viperLocal)

	solutionStartCmd := &cobra.Command{
		Use:   "start <solution_id>",
		Short: "Start a solution",
		Long: `Start a solution by id on a given cluster.

The solution id is a unique identifier of the solution to be started.
Solution names can be found by running 'inctl solution list' or on the "Solutions" page in the Portal (select "copy solution ID" in the Solution's context menu).

The cluster name can be found by running 'inctl cluster list' or through the Portal Solutions page.

For example:
  inctl solution start --org my-org --cluster vmp-1234567890 9999ffff-9999-ffff-9999-ffff9999ffff_BRANCH`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			solutionName := args[0]
			printer, err := printer.NewPrinter(cmd.Flags().Lookup("output").Value.String())
			if err != nil {
				return err
			}

			ctx := cmd.Context()

			_, clusterFlag, _, err := flags.GetFlagsAddressClusterSolution()
			if err != nil {
				return err
			}
			printer.PrintSf("Starting solution '%s' on cluster '%s'\n", solutionName, clusterFlag)

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			if err := startSolution(ctx, conn, solutionName, flagOperationMode); err != nil {
				return err
			}

			return nil
		},
	}

	flags.SetCommand(solutionStartCmd)
	flags.AddFlagsProjectOrg()
	flags.AddFlagsAddressClusterSolution()

	solutionStartCmd.Flags().StringVar(&flagOperationMode, "operation-mode", "sim",
		"The operation mode to start the solution in, one of 'sim' (default) or 'real'.")

	return solutionStartCmd
}
