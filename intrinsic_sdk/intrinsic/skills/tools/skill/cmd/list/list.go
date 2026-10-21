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

// Package list defines the skill list command which lists skills in a registry.
package list

import (
	"context"
	"fmt"
	"strings"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/skills/tools/skill/cmd/listutil"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"

	skillregistrygrpcpb "intrinsic/skills/proto/skill_registry_go_proto"
	skillregistrypb "intrinsic/skills/proto/skill_registry_go_proto"
	spb "intrinsic/skills/proto/skills_go_proto"
)

const (
	keyFilter = "filter"

	sideloadedFilter = "sideloaded"
	releasedFilter   = "released"
)

var filterOptions = []string{sideloadedFilter, releasedFilter}

type listSkillsParams struct {
	filter   string
	printer  printer.Printer
	pageSize int32 // This can be set in tests to verify pagination behavior.
}

func listSkills(ctx context.Context, client skillregistrygrpcpb.SkillRegistryClient, params *listSkillsParams) error {
	filter := ""
	if params.filter == sideloadedFilter {
		filter = sideloadedFilter
	} else if params.filter == releasedFilter {
		filter = fmt.Sprintf("-%s", sideloadedFilter)
	}

	var (
		skills        []*spb.Skill
		nextPageToken string
	)
	for {
		resp, err := client.ListSkills(ctx, &skillregistrypb.ListSkillsRequest{
			Filter:    filter,
			PageSize:  params.pageSize,
			PageToken: nextPageToken,
		})
		if err != nil {
			return errors.Wrap(err, "could not list skills")
		}
		skills = append(skills, resp.GetSkills()...)
		nextPageToken = resp.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}

	params.printer.Print(listutil.SkillDescriptionsFromSkills(skills))

	return nil
}

// Command returns the command for listing skills.
func Command() *cobra.Command {
	cmdFlags := cmdutils.NewCmdFlags()
	listCmd := &cobra.Command{
		Use:   "list",
		Short: "List skills that are loaded into a solution.",
		Example: `List skills of a running solution (solution id, not display name)
$ inctl skill list --project my-project --solution my-solution-id

	To find a running solution's id, run:
	$ inctl solution list --project my-project --filter "running_on_hw,running_in_sim" --output json

Set the cluster on which the solution is running
$ inctl skill list --project my-project --cluster my-cluster
`,
		Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, _ []string) error {
			ctx := cmd.Context()

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, cmdFlags)
			if err != nil {
				return err
			}
			defer conn.Close()

			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}

			client := skillregistrygrpcpb.NewSkillRegistryClient(conn)
			err = listSkills(ctx, client, &listSkillsParams{
				filter:  cmdFlags.GetString(keyFilter),
				printer: prtr,
			})
			if err != nil {
				return err
			}

			return nil
		},
	}

	cmdFlags.SetCommand(listCmd)

	cmdFlags.AddFlagsAddressClusterSolution()
	cmdFlags.AddFlagsProjectOrg()

	cmdFlags.OptionalString(keyFilter, "", fmt.Sprintf("Filter skills by the way they where loaded into the solution. One of: %s.", strings.Join(filterOptions, ", ")))

	return listCmd
}
