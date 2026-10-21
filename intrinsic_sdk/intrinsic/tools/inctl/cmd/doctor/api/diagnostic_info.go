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

package api

import (
	"github.com/spf13/cobra"

	reportpb "intrinsic/tools/inctl/cmd/doctor/proto/v1/report_go_proto"
)

// DiagnosticInformationReporter is an interface for a class that generates information entry for the report.
type DiagnosticInformationReporter struct {
	Name                string
	Description         string
	GenerateInformation func(
		cmd *cobra.Command,
		args []string,
		report *reportpb.Report,
	) (*[]*reportpb.DiagnosticInformationEntry, error)
	InformationReporterDependencyNames []string
}
