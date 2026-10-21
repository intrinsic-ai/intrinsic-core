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

// Package share provides a command to share a solution.
package share

import (
	"encoding/json"
	"fmt"

	solutionversionservicepb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

type shareResult struct {
	SolutionID string `json:"solutionId"`
	TargetOrg  string `json:"targetOrg"`
}

func printResult(cmd *cobra.Command, result *shareResult) error {
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeJSON {
		b, err := json.Marshal(result)
		if err != nil {
			return err
		}
		cmd.Println(string(b))
	} else {
		color.C.Blue().Printf("Successfully shared solution %q with organization %q\n", result.SolutionID, result.TargetOrg)
	}
	return nil
}

// NewCommand returns the share command.
func NewCommand() *cobra.Command {
	viperLocal := viper.New()

	solutionShareCmd := orgutil.WrapCmd(&cobra.Command{
		Use:   "share <solution_id>",
		Short: "Share a solution with the organization",
		Long:  `Share a solution with the active organization. You must be the owner of the solution.`,
		Example: `  # Share a solution with the active organization
  inctl solution share <solution_id> --org <org>`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			solutionID := args[0]
			ctx := cmd.Context()
			conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(viperLocal))
			if err != nil {
				return err
			}
			defer conn.Close()

			client := solutionversionservicepb.NewSolutionVersionServiceClient(conn)
			req := &solutionversionservicepb.ShareBranchRequest{
				BranchId: solutionID,
			}

			if _, err := client.ShareBranch(ctx, req); err != nil {
				return fmt.Errorf("failed to share solution %j: %w", solutionID, err)
			}

			return printResult(cmd, &shareResult{solutionID, viper.GetString(orgutil.KeyOrganization)})
		},
	}, viperLocal)

	return solutionShareCmd
}
