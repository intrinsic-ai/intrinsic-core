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

package solutionversion

import (
	"fmt"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/color"

	"github.com/spf13/cobra"

	branchpb "intrinsic/solution_versions/proto/v1/branch_go_proto"
	snapshotsourcepb "intrinsic/solution_versions/proto/v1/snapshot_source_go_proto"
	solutionversionservicegrpcpb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	solutionversionservicepb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
)

var (
	sourceSolutionID       string
	newSolutionDisplayName string
)

// SolutionVersionDuplicateCmd is the `inctl solution_version duplicate` command.
var SolutionVersionDuplicateCmd = &cobra.Command{
	Use:     "duplicate",
	Short:   "Duplicate a solution within the same project.",
	Long:    "Duplicate a solution. This creates a new solution with the same content as the source solution's latest committed version within the same project.",
	Example: `inctl solution_version duplicate --solution my-solution --title "Copy of my-solution" --org my-org`,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(viperLocal))
		if err != nil {
			return err
		}
		defer conn.Close()

		svsC := solutionversionservicegrpcpb.NewSolutionVersionServiceClient(conn)

		// Get the tip snapshot of the source branch.
		branch, err := svsC.GetBranch(ctx, &solutionversionservicepb.GetBranchRequest{
			BranchId: sourceSolutionID,
		})
		if err != nil {
			return fmt.Errorf("failed to get source branch %q: %v", sourceSolutionID, err)
		}
		tipSnapshotID := branch.GetTipSnapshotId()
		if tipSnapshotID == "" {
			return fmt.Errorf("source branch %q has no tip snapshot", sourceSolutionID)
		}

		// Create the version branch from the source snapshot.
		createVersionBranchReq := &solutionversionservicepb.CreateBranchRequest{
			Branch: &branchpb.Branch{
				DisplayName: newSolutionDisplayName,
				BranchType:  branchpb.Branch_BRANCH_TYPE_VERSION,
			},
			From: &solutionversionservicepb.CreateBranchRequest_SnapshotSource{
				SnapshotSource: &snapshotsourcepb.SnapshotSource{
					SnapshotId: tipSnapshotID,
					BranchId:   sourceSolutionID,
				},
			},
		}
		newVersionBranch, err := svsC.CreateBranch(ctx, createVersionBranchReq)
		if err != nil {
			return fmt.Errorf("failed to create version branch for solution %q: %v", sourceSolutionID, err)
		}

		// Create the deployment branch.
		createDeploymentBranchReq := &solutionversionservicepb.CreateBranchRequest{
			Branch: &branchpb.Branch{
				DisplayName:      newSolutionDisplayName,
				BranchType:       branchpb.Branch_BRANCH_TYPE_DEPLOYMENT,
				UpstreamBranchId: newVersionBranch.GetId(),
			},
		}
		newDeploymentBranch, err := svsC.CreateBranch(ctx, createDeploymentBranchReq)
		if err != nil {
			return fmt.Errorf("failed to create deployment branch for solution %q: %v", sourceSolutionID, err)
		}

		// Get the ID of the new branch.
		newSolutionID := newDeploymentBranch.GetId()
		color.C.Blue().Printf("Duplicated solution %q -> %q\n", sourceSolutionID, newSolutionID)
		return nil
	},
}

func init() {
	SolutionVersionDuplicateCmd.Flags().StringVar(&sourceSolutionID, "solution", "", "ID of the solution to duplicate.")
	SolutionVersionDuplicateCmd.MarkFlagRequired("solution")
	SolutionVersionDuplicateCmd.Flags().StringVar(&newSolutionDisplayName, "title", "", "Display name for the new duplicated solution.")
	SolutionVersionDuplicateCmd.MarkFlagRequired("title")
	SolutionVersionCmd.AddCommand(SolutionVersionDuplicateCmd)
}
