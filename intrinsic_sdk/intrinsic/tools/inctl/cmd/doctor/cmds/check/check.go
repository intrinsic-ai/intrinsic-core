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

// Package check contains the command for the inctl doctor check command.
package check

import (
	"fmt"
	"strings"

	"intrinsic/tools/inctl/cmd/doctor/api/api"
	"intrinsic/tools/inctl/cmd/doctor/checks/checks"
	"intrinsic/tools/inctl/cmd/doctor/reporters/reporters"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/encoding/protojson"

	reportpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

// CheckCmd is the entry point for the inctl doctor check command.
var CheckCmd = &cobra.Command{
	Use:   "check",
	Short: "Check your environment for common problems",
	RunE:  checkCommandE,
}

var doReport bool

func checkCommandE(cmd *cobra.Command, args []string) error {
	out, ok := printer.AsPrinter(cmd.OutOrStdout(), printer.TextOutputFormat)
	if !ok {
		return fmt.Errorf("invalid output configuration")
	}

	// Sort the checks and reporters in topological order.
	sortedChecks, err := topologicalSort(
		&checks.Checks,
		func(check *api.DiagnosticCheck) *[]string { return &check.CheckDependencyNames },
	)
	if err != nil {
		return err
	}
	sortedReporters, err := topologicalSort(
		&reporters.Reporters,
		func(reporter *api.DiagnosticInformationReporter) *[]string {
			return &reporter.InformationReporterDependencyNames
		},
	)
	if err != nil {
		return err
	}

	// Run the reporters.
	report := &reportpb.Report{}
	// TODO(wjwwood): reduce reporters that are executed to just the ones that are dependencies of
	// the checks, when the --report flag is not set.
	for _, reporter := range sortedReporters {
		entries, err := reporter.GenerateInformation(cmd, args, report)
		if err != nil {
			return err
		}
		report.Entries = append(report.GetEntries(), (*entries)...)
	}

	// Run the checks.
	var failures []*reportpb.DiagnosticCheck
	for _, check := range sortedChecks {
		out.PrintSf("Running check '%s' (%s)... ", check.Name, check.Description)
		result, err := check.ExecuteCheck(cmd, args, report)
		if err != nil {
			return err
		}
		if result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_UNSPECIFIED {
			return fmt.Errorf("check '%s' returned an unspecified result", check.Name)
		}
		report.Checks = append(report.GetChecks(), result)
		printOutput := result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_FAILED ||
			result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_WARNING ||
			result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_SKIPPED
		if printOutput {
			output := strings.TrimSpace(result.GetOutput())
			if output != "" {
				out.PrintSf("  Output: %s", result.GetOutput())
			}
			if len(result.GetDetails()) > 0 {
				out.PrintS("  Details:")
				for _, detail := range result.GetDetails() {
					out.PrintSf("    %s: %s", detail.GetName(), detail.GetValue())
				}
			}
		}
		if result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_FAILED {
			out.PrintS("FAILED")
			failures = append(failures, result)
		}
		if result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_WARNING {
			out.PrintS("WARNING")
		}
		if result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_OK {
			out.PrintS("OK")
		}
		if result.GetResult() == reportpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_SKIPPED {
			out.PrintS("SKIPPED")
		}
	}

	if doReport {
		json := protojson.Format(report)
		out.Print(json)
	}

	if len(failures) > 0 {
		return fmt.Errorf("doctor: %d check(s) failed", len(failures))
	}

	return nil
}

func init() {
	CheckCmd.Flags().BoolVarP(
		&doReport,
		"report",
		"r",
		false,
		"Generate a JSON report of the check results.")
}
