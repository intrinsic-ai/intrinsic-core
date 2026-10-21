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

// Package create provides a command to create a solution.
package create

import (
	"context"
	"fmt"

	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	branchpb "intrinsic/solution_versions/proto/v1/branch_go_proto"
	snapshotpb "intrinsic/solution_versions/proto/v1/snapshot_go_proto"
	snapshotsourcepb "intrinsic/solution_versions/proto/v1/snapshot_source_go_proto"
	solutionversionservicepb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
)

type createParams struct {
	displayName       string
	snapshotID        string
	sourceBranchID    string
	templateID        string
	commitTitle       string
	commitDescription string
}

type branchInfo struct {
	ID          string `json:"id"`
	DisplayName string `json:"displayName"`
}

type createResult struct {
	VersionBranch    branchInfo `json:"versionBranch"`
	DeploymentBranch branchInfo `json:"deploymentBranch"`
}

func makeCreateVersionBranchRequest(p createParams) *solutionversionservicepb.CreateBranchRequest {
	var commitMessage *snapshotpb.CommitMessage
	if p.commitTitle != "" || p.commitDescription != "" {
		commitMessage = &snapshotpb.CommitMessage{
			Title:       p.commitTitle,
			Description: p.commitDescription,
		}
	}

	req := &solutionversionservicepb.CreateBranchRequest{
		Branch: &branchpb.Branch{
			DisplayName: p.displayName,

			// TODO: b/453570541 - Remove main and unspecified branch type.

			BranchType: branchpb.Branch_BRANCH_TYPE_MAIN,
		},
		CommitMessage: commitMessage,
	}

	if p.snapshotID != "" {
		req.From = &solutionversionservicepb.CreateBranchRequest_SnapshotSource{
			SnapshotSource: &snapshotsourcepb.SnapshotSource{
				SnapshotId: p.snapshotID,
				BranchId:   p.sourceBranchID,
			},
		}
	} else {
		templateSource := &solutionversionservicepb.CreateBranchRequest_TemplateSource{}
		if p.templateID != "" {
			templateSource.TemplateId = p.templateID
		}

		req.From = &solutionversionservicepb.CreateBranchRequest_TemplateSource_{
			TemplateSource: templateSource,
		}
	}

	return req
}

func createSolution(ctx context.Context, svsC solutionversionservicepb.SolutionVersionServiceClient, reqVersion *solutionversionservicepb.CreateBranchRequest) (*createResult, error) {
	// 1. Create Version Branch
	respVersion, err := svsC.CreateBranch(ctx, reqVersion)
	if err != nil {
		return nil, fmt.Errorf("failed to create version branch: %v. Try again or contact support the issue persists.", err)
	}

	// 2. Create Deployment Branch
	reqDeployment := &solutionversionservicepb.CreateBranchRequest{
		Branch: &branchpb.Branch{
			DisplayName: reqVersion.GetBranch().GetDisplayName(),

			// TODO: b/453570541 - Remove main and unspecified branch type.

			BranchType:       branchpb.Branch_BRANCH_TYPE_UNSPECIFIED,
			UpstreamBranchId: respVersion.GetId(),
		},
	}

	respDeployment, err := svsC.CreateBranch(ctx, reqDeployment)
	if err != nil {
		return nil, fmt.Errorf("failed to create deployment branch: %v. Try again or contact support if the issue persists.", err)
	}

	return &createResult{
		VersionBranch: branchInfo{
			ID:          respVersion.GetId(),
			DisplayName: respVersion.GetDisplayName(),
		},
		DeploymentBranch: branchInfo{
			ID:          respDeployment.GetId(),
			DisplayName: respDeployment.GetDisplayName(),
		},
	}, nil
}

func printResult(cmd *cobra.Command, result *createResult) error {
	prtr, err := printer.NewPrinterFromCommand(cmd)
	if err != nil {
		return err
	}
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeJSON {
		prtr.Println(result)
	} else {
		color.C.Blue().Fprintf(cmd.OutOrStdout(), "Successfully created version branch %q with ID %q\n", result.VersionBranch.DisplayName, result.VersionBranch.ID)
		color.C.Blue().Fprintf(cmd.OutOrStdout(), "Successfully created deployment branch %q with ID %q\n", result.DeploymentBranch.DisplayName, result.DeploymentBranch.ID)
	}
	return nil
}

func NewCommand() *cobra.Command {
	viperLocal := viper.New()
	flags := cmdutils.NewCmdFlagsWithViper(viperLocal)

	var (
		flagDisplayName       string
		flagSnapshotID        string
		flagSourceBranchID    string
		flagTemplateID        string
		flagCommitTitle       string
		flagCommitDescription string
	)

	solutionCreateCmd := orgutil.WrapCmd(&cobra.Command{
		Use:   "create",
		Short: "Create a new solution (version and deployment branches).",
		Long:  `Create a new solution by creating a version branch and a tracking deployment branch. You can create an empty solution, a solution from an existing snapshot or from a template.`,
		Example: `  # Create an empty solution
  inctl solution create --display-name "my-solution"

  # Create a solution from an existing snapshot
  inctl solution create --display-name "my-solution" \
    --snapshot-id <snapshot_id> \
    --source-branch-id <branch_id>

  # Create a solution from a template
  inctl solution create --display-name "my-solution" --template-id <template_id>`,
		Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := cmd.Context()
			conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(viperLocal))
			if err != nil {
				return err
			}
			defer conn.Close()

			svsC := solutionversionservicepb.NewSolutionVersionServiceClient(conn)
			reqVersion := makeCreateVersionBranchRequest(createParams{
				displayName:       flagDisplayName,
				snapshotID:        flagSnapshotID,
				sourceBranchID:    flagSourceBranchID,
				templateID:        flagTemplateID,
				commitTitle:       flagCommitTitle,
				commitDescription: flagCommitDescription,
			})

			result, err := createSolution(ctx, svsC, reqVersion)
			if err != nil {
				return err
			}

			return printResult(cmd, result)
		},
	}, viperLocal)

	flags.SetCommand(solutionCreateCmd)

	solutionCreateCmd.Flags().StringVar(&flagDisplayName, "display-name", "", "The display name of the solution.")
	solutionCreateCmd.Flags().StringVar(&flagSnapshotID, "snapshot-id", "", "The ID of the snapshot to create the version branch from.")
	solutionCreateCmd.Flags().StringVar(&flagSourceBranchID, "source-branch-id", "", "The branch ID that contains the snapshot in its history.")
	solutionCreateCmd.Flags().StringVar(&flagTemplateID, "template-id", "", "The ID of the template to create a version branch from.")
	solutionCreateCmd.Flags().StringVar(&flagCommitTitle, "commit-title", "", "Optional commit title for the initial version.")
	solutionCreateCmd.Flags().StringVar(&flagCommitDescription, "commit-description", "", "Optional commit description for the initial version.")

	solutionCreateCmd.MarkFlagRequired("display-name")
	solutionCreateCmd.MarkFlagsMutuallyExclusive("template-id", "snapshot-id")
	solutionCreateCmd.MarkFlagsMutuallyExclusive("template-id", "source-branch-id")
	solutionCreateCmd.MarkFlagsRequiredTogether("snapshot-id", "source-branch-id")

	return solutionCreateCmd
}
