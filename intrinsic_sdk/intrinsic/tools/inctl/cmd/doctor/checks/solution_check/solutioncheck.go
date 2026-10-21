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

// Package solutioncheck implements a DiagnosticCheck that checks if the user's solution is running.
package solutioncheck

import (
	"fmt"

	"intrinsic/tools/inctl/cmd/doctor/api/api"

	"github.com/spf13/cobra"

	rpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

const (
	checkName string = "solution_check"
)

// Check is the DiagnosticCheck that checks if the user's solution is running.
var Check = api.DiagnosticCheck{
	Name:                               checkName,
	Description:                        "Checks if the user's solution is running",
	ExecuteCheck:                       checkE,
	CheckDependencyNames:               []string{},
	InformationReporterDependencyNames: []string{"solution"},
}

func checkE(cmd *cobra.Command, args []string, report *rpb.Report) (*rpb.DiagnosticCheck, error) {
	result := &rpb.DiagnosticCheck{}
	localCheckName := checkName
	result.Name = &localCheckName

	// Get some information from the report.
	var solutionID string
	var solutionState string
	var solutionIssue string
	for _, entry := range report.GetEntries() {
		if entry.GetName() == "solution_id" {
			solutionID = entry.GetValue()
		} else if entry.GetName() == "solution_state" {
			solutionState = entry.GetValue()
		} else if entry.GetName() == "solution_issue" {
			solutionIssue = entry.GetValue()
		}
	}

	// If the solution id is not found, then skip the rest of the checks.
	if solutionID == "" {
		resultOutput := fmt.Sprintf(
			"The solution id is not found in the report, skipping this check. " +
				"Use the --solution flag to specify the solution id to enable this check.",
		)
		result.Output = &resultOutput
		resultResult := rpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_SKIPPED
		result.Result = &resultResult

		return result, nil
	}

	// If the solution is not found, then return an error.
	if solutionIssue != "none" {
		resultOutput := fmt.Sprintf(
			"There was an issue getting the solution details, "+
				"which could be due to the solution not being found in the given org or "+
				"a different underlying issue: %s",
			solutionIssue,
		)
		result.Output = &resultOutput
		resultResult := rpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_FAILED
		result.Result = &resultResult

		return result, nil
	}

	// If the solution is not running, then return an error.
	if solutionState == "SOLUTION_STATE_NOT_RUNNING" {
		resultOutput := fmt.Sprintf(
			"The solution is not running. " +
				"This may be expected if you are not currently running the solution, but " +
				"it could also indicate that there is an issue with the solution itself.",
		)
		result.Output = &resultOutput
		resultResult := rpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_WARNING
		result.Result = &resultResult

		return result, nil
	}

	resultResult := rpb.DiagnosticCheckResult_DIAGNOSTIC_CHECK_RESULT_OK
	result.Result = &resultResult

	return result, nil
}
