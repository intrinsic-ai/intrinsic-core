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

// Package delete provides a command to delete a solution.
package delete

import (
	"fmt"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	solutionversionservicegrpcpb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	solutionversionservicepb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
)

// NewCommand returns the delete command.
func NewCommand() *cobra.Command {
	viperLocal := viper.New()

	solutionDeleteCmd := orgutil.WrapCmd(&cobra.Command{
		Use:   "delete",
		Short: "Delete a versioned solution.",
		Long:  "Delete a versioned solution.",
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if err := agents.Check(cmd); err != nil {
				return err
			}
			solutionID := args[0]
			ctx := cmd.Context()
			conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(viperLocal))
			if err != nil {
				return err
			}
			defer conn.Close()

			svsC := solutionversionservicegrpcpb.NewSolutionVersionServiceClient(conn)

			_, err = svsC.DeleteBranch(ctx, &solutionversionservicepb.DeleteBranchRequest{
				BranchId: solutionID,
			})
			if err != nil {
				if code := status.Code(err); code != codes.NotFound {
					color.C.Red().Printf("A versioned solution with the given ID %q was not found or you do not have access to it.\n", solutionID)
					color.C.Red().Printf("Please make sure the solution is versioned and you have access to it. Deleting a legacy solution is not supported through inctl.\n")
				}
				return fmt.Errorf("failed to delete solution %q: %v", solutionID, err)
			}

			color.C.Blue().Printf("Successfully deleted solution : %q\n", solutionID)
			return nil
		},
	}, viperLocal)
	return solutionDeleteCmd
}
