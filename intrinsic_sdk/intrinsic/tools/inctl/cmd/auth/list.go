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

package auth

import (
	"fmt"
	"strings"

	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
)

var listCmdFlags = cmdutils.NewCmdFlags()

const (
	keyPrefixSearch = "prefix"
)

var listCmd = &cobra.Command{
	Use:     "list",
	Aliases: []string{"ls"},
	Short:   "Lists available credentials",
	Long: `
	Lists available credentials present for current user

	Note that the command only shows the locally stored credentials
	and that the output may contain invalidated credentials.

	Example:
	  inctl auth list
	  inctl auth list --prefix <prefix_to_search>
	`,
	Args: cobra.NoArgs,
	RunE: listCredentialsE,
	// This is required because AuthCmd PersistentPreRunE would throw error if neither
	// project nor org is set. And we want to allow listCmd to run without any flags set.
	PersistentPreRunE: func(command *cobra.Command, _ []string) error {
		out, err := printer.NewPrinter(root.FlagOutput)
		if err == nil {
			if command.HasParent() {
				command.Parent().SetOut(out)
			}
		} else if err != nil {
			return err
		}
		return nil
	},
}

func configListing(store *auth.Store) (*configListView, error) {
	configurations, err := store.ListConfigurations()
	if err != nil {
		return nil, fmt.Errorf("cannot list configurations: %w", err)
	}

	orgs, err := store.ListOrgs()
	if err != nil {
		return nil, fmt.Errorf("list orgs: %w", err)
	}

	projectName := listCmdFlags.GetString(keyPrefixSearch)

	result := &configListView{Configurations: make(map[string][]string, len(configurations)), Orgs: make([]auth.OrgInfo, 0, len(orgs))}
	for _, config := range configurations {
		if projectName != "" && !strings.HasPrefix(config, projectName) {
			continue
		}
		tokens, err := store.GetConfiguration(config)
		if err != nil {
			// we are going to fail early if we encounter issue
			// we could consider be more defensive
			return nil, fmt.Errorf("cannot read %s: %w", config, err)
		}
		result.Configurations[config] = mapToKeysArray(tokens.Tokens)
	}

	for _, org := range orgs {
		orgInfo, err := store.ReadOrgInfo(org)
		if err != nil {
			continue
		}

		result.Orgs = append(result.Orgs, orgInfo)
	}

	return result, nil
}

func runListCmd(prtr printer.Printer) error {
	result, err := configListing(authStore)
	if err != nil {
		return fmt.Errorf("get configs: %w", err)
	}

	prtr.Print(result)
	return nil
}

func listCredentialsE(cmd *cobra.Command, _ []string) error {
	out, ok := printer.AsPrinter(cmd.OutOrStdout(), printer.TextOutputFormat)
	if !ok {
		return fmt.Errorf("invalid output configuration")
	}

	return runListCmd(out)
}

type configListView struct {
	Configurations map[string][]string `json:"configurations"`
	Orgs           []auth.OrgInfo      `json:"orgs"`
}

// String is not a typical implementation of fmt.Stringer but implementation
// of view object designed for human output, which strongly deviates from
// usual fmt.Stringer implementation.
func (c *configListView) String() string {
	result := new(strings.Builder)

	if len(c.Orgs) > 0 {
		result.WriteString("The following organizations can be used:\n")
		for _, org := range c.Orgs {
			result.WriteString("  ")
			result.WriteString(org.Organization)
			result.WriteString("\n")
		}
	}

	if len(c.Configurations) > 0 {
		result.WriteString("The following projects can be used:\n")
		for project, configs := range c.Configurations {
			result.WriteString(fmt.Sprintf("  %s: %s\n", project, strings.Join(configs, ", ")))
		}
	}

	return result.String()
}

func mapToKeysArray[K comparable, V any](in map[K]V) []K {
	result := make([]K, 0, len(in))
	for key := range in {
		result = append(result, key)
	}
	return result
}

func init() {
	AuthCmd.AddCommand(listCmd)
	listCmdFlags.SetCommand(listCmd)
	listCmdFlags.OptionalString(keyPrefixSearch, "", "Prefix to search for in the list of credentials.")
}
