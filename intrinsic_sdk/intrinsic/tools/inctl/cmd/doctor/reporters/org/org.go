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

// Package org provides a reporter for the organization provided by the user.
package org

import (
	"strings"

	"intrinsic/tools/inctl/cmd/doctor/api/api"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"

	rpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

const (
	// ReporterName is the name of the reporter.
	ReporterName string = "org"
	// ReportPrefix is the prefix for the report entries.
	ReportPrefix string = "org_"
)

// Reporter is the DiagnosticInformationReporter that reports the organization details.
var Reporter = api.DiagnosticInformationReporter{
	Name:                               ReporterName,
	Description:                        "Reports the organization details.",
	GenerateInformation:                generateInformation,
	InformationReporterDependencyNames: []string{},
}

func generateInformation(cmd *cobra.Command, args []string, report *rpb.Report) (*[]*rpb.DiagnosticInformationEntry, error) {
	var entries []*rpb.DiagnosticInformationEntry

	orgNameName := ReportPrefix + "name"
	orgNameValue := orgutil.QualifiedOrg(api.CmdFlags.GetFlagProject(), api.CmdFlags.GetFlagOrganization())
	if strings.HasPrefix(orgNameValue, "@") || strings.HasSuffix(orgNameValue, "@") {
		// If either the org or project are missing, make the orgNameValue empty.
		orgNameValue = ""
	}
	orgName := &rpb.DiagnosticInformationEntry{
		Name:  &orgNameName,
		Value: &orgNameValue,
	}
	entries = append(entries, orgName)

	return &entries, nil
}
