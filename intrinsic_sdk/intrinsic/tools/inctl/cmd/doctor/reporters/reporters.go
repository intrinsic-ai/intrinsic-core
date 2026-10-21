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

// Package reporters contains all the reporters for the doctor command.
package reporters

import (
	"intrinsic/tools/inctl/cmd/doctor/api/api"
	"intrinsic/tools/inctl/cmd/doctor/reporters/cluster/cluster"
	"intrinsic/tools/inctl/cmd/doctor/reporters/env_vars/envvars"
	"intrinsic/tools/inctl/cmd/doctor/reporters/inctl_details/inctldetails"
	"intrinsic/tools/inctl/cmd/doctor/reporters/org/org"
	"intrinsic/tools/inctl/cmd/doctor/reporters/solution/solution"
)

// Reporters is a list of all the reporters, but note that maps are not sorted by insert order, so
// the keys are manually sorted alphabetically wherever used.
var Reporters = map[string]*api.DiagnosticInformationReporter{
	envvars.ReporterName:      &envvars.Reporter,
	inctldetails.ReporterName: &inctldetails.Reporter,
	org.ReporterName:          &org.Reporter,
	cluster.ReporterName:      &cluster.Reporter,
	solution.ReporterName:     &solution.Reporter,
}
