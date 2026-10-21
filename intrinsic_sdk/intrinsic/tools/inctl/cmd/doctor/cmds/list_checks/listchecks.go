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

// Package listchecks contains the command for the inctl doctor list_checks command.
package listchecks

import (
	"fmt"
	"sort"

	"intrinsic/tools/inctl/cmd/doctor/checks/checks"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
)

// ListChecksCmd is the entry point for the inctl doctor list_checks command.
var ListChecksCmd = &cobra.Command{
	Use:   "list_checks",
	Short: "List all available checks",
	RunE:  listChecksCommandE,
}

func listChecksCommandE(cmd *cobra.Command, args []string) error {
	out, ok := printer.AsPrinter(cmd.OutOrStdout(), printer.TextOutputFormat)
	if !ok {
		return fmt.Errorf("invalid output configuration")
	}

	return runListChecksCmd(out)
}

func runListChecksCmd(prtr printer.Printer) error {
	// Map keys are not guaranteed to be sorted, so we sort them arbitrarily here.
	sortedKeys := make([]string, 0, len(checks.Checks))
	for k := range checks.Checks {
		sortedKeys = append(sortedKeys, k)
	}
	sort.Strings(sortedKeys)
	for _, checkName := range sortedKeys {
		check, ok := checks.Checks[checkName]
		if !ok {
			return fmt.Errorf("check '%s' unexpectedly not in the checks map", checkName)
		}
		prtr.Print(check.Name + ": " + check.Description)
	}
	return nil
}
