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

// Package envvars provides a reporter for the environment variables.
package envvars

import (
	"os"

	"intrinsic/tools/inctl/cmd/doctor/api/api"

	"github.com/spf13/cobra"

	rpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

const (
	// ReporterName is the name of the reporter.
	ReporterName string = "env_vars"
	// ReportPrefix is the prefix for the report entries.
	ReportPrefix string = "env_"
)

// Reporter is the DiagnosticInformationReporter that reports the environment variables.
var Reporter = api.DiagnosticInformationReporter{
	Name:                               ReporterName,
	Description:                        "Reports the environment variables.",
	GenerateInformation:                generateInformation,
	InformationReporterDependencyNames: []string{},
}

func generateInformation(cmd *cobra.Command, args []string, report *rpb.Report) (*[]*rpb.DiagnosticInformationEntry, error) {
	var entries []*rpb.DiagnosticInformationEntry
	envVars := []string{
		"INTRINSIC_ORG",
		"INTRINSIC_PROJECT",
	}
	for _, env := range envVars {
		name := ReportPrefix + env
		value := os.Getenv(env)
		entry := &rpb.DiagnosticInformationEntry{
			Name:  &name,
			Value: &value,
		}
		entries = append(entries, entry)
	}
	return &entries, nil
}
