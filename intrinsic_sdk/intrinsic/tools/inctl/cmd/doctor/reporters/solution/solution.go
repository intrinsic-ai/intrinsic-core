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

// Package solution envvars provides a reporter for the solution details.
package solution

import (
	"encoding/json"
	"fmt"
	"os/exec"

	"intrinsic/tools/inctl/cmd/doctor/api/api"

	"github.com/spf13/cobra"

	rpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

const (
	// ReporterName is the name of the reporter.
	ReporterName string = "solution"
	// ReportPrefix is the prefix for the report entries.
	ReportPrefix string = "solution_"
)

// Reporter is the DiagnosticInformationReporter that reports the solution details.
var Reporter = api.DiagnosticInformationReporter{
	Name:                               ReporterName,
	Description:                        "Reports the solution details if a solution id is provided.",
	GenerateInformation:                generateInformation,
	InformationReporterDependencyNames: []string{"inctl_details", "org"},
}

func generateInformation(cmd *cobra.Command, args []string, report *rpb.Report) (*[]*rpb.DiagnosticInformationEntry, error) {
	var entries []*rpb.DiagnosticInformationEntry

	// Check if the user has provided a solution id.
	solutionID, err := cmd.Flags().GetString("solution")
	if err != nil {
		return nil, fmt.Errorf("failed to get solution id: %w", err)
	}
	solutionIDName := ReportPrefix + "id"
	solutionIDEntry := &rpb.DiagnosticInformationEntry{
		Name:  &solutionIDName,
		Value: &solutionID,
	}
	entries = append(entries, solutionIDEntry)

	solutionIssueName := ReportPrefix + "issue"
	solutionIssue := "none"

	// If no solution id is provided, then just return an empty name, but not the solution details.
	if solutionID == "" {
		solutionIssue = "no solution id provided"
		solutionIssueEntry := &rpb.DiagnosticInformationEntry{
			Name:  &solutionIssueName,
			Value: &solutionIssue,
		}
		entries = append(entries, solutionIssueEntry)
		return &entries, nil
	}

	// Get the inctl path from the report.
	var inctlPath string
	for _, entry := range report.GetEntries() {
		if entry.GetName() == "inctl_path" {
			inctlPath = entry.GetValue()
		}
	}
	if inctlPath == "" {
		return nil, fmt.Errorf("inctl_path not found in report")
	}
	// Get the org name from the report.
	var orgName string
	for _, entry := range report.GetEntries() {
		if entry.GetName() == "org_name" {
			orgName = entry.GetValue()
		}
	}
	// If no org name is provided, then just return the name, but not the solution details.
	if orgName == "" {
		solutionIssue = "no org name provided, solution details can not be retrieved"
		solutionIssueEntry := &rpb.DiagnosticInformationEntry{
			Name:  &solutionIssueName,
			Value: &solutionIssue,
		}
		entries = append(entries, solutionIssueEntry)
		return &entries, nil
	}

	// Capture the solution details using the inctl solution list command.
	solutionListCmd := exec.Command(inctlPath, "solution", "--org="+orgName, "list", "--output=json")
	out, err := solutionListCmd.CombinedOutput()
	if err != nil {
		solutionIssue = fmt.Sprintf("failed to run 'inctl solution list' command: %v: %s", err, out)
		solutionIssueEntry := &rpb.DiagnosticInformationEntry{
			Name:  &solutionIssueName,
			Value: &solutionIssue,
		}
		entries = append(entries, solutionIssueEntry)
		return &entries, nil
	}
	var solutionListJSON map[string]any
	if err := json.Unmarshal(out, &solutionListJSON); err != nil {
		solutionIssue = fmt.Sprintf("failed to unmarshal solution list json: %v", err)
		solutionIssueEntry := &rpb.DiagnosticInformationEntry{
			Name:  &solutionIssueName,
			Value: &solutionIssue,
		}
		entries = append(entries, solutionIssueEntry)
		return &entries, nil
	}
	solutions := solutionListJSON["solutions"].([]any)
	solutionDisplayName := ""
	solutionState := ""
	solutionClusterID := ""
	solutionIssue = "not found in solution list"
	for _, solutionInfoJSON := range solutions {
		solutionInfo := solutionInfoJSON.(map[string]any)
		if solutionInfo["name"] == solutionID {
			solutionDisplayName = solutionInfo["displayName"].(string)
			solutionState = solutionInfo["state"].(string)
			solutionClusterIDJSON, ok := solutionInfo["clusterName"]
			if ok {
				solutionClusterID = solutionClusterIDJSON.(string)
			}
			solutionIssue = "none"
		}
	}

	solutionDisplayNameName := ReportPrefix + "display_name"
	solutionDisplayNameEntry := &rpb.DiagnosticInformationEntry{
		Name:  &solutionDisplayNameName,
		Value: &solutionDisplayName,
	}
	entries = append(entries, solutionDisplayNameEntry)
	solutionStateName := ReportPrefix + "state"
	solutionStateEntry := &rpb.DiagnosticInformationEntry{
		Name:  &solutionStateName,
		Value: &solutionState,
	}
	entries = append(entries, solutionStateEntry)
	solutionClusterIDName := ReportPrefix + "cluster_id"
	solutionClusterIDEntry := &rpb.DiagnosticInformationEntry{
		Name:  &solutionClusterIDName,
		Value: &solutionClusterID,
	}
	entries = append(entries, solutionClusterIDEntry)
	solutionIssueEntry := &rpb.DiagnosticInformationEntry{
		Name:  &solutionIssueName,
		Value: &solutionIssue,
	}
	entries = append(entries, solutionIssueEntry)

	return &entries, nil
}
