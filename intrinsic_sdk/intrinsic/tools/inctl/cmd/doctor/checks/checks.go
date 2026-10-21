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

// Package checks contains the implementation of checks as well as the interface for checks.
package checks

import (
	"intrinsic/tools/inctl/cmd/doctor/api/api"
	"intrinsic/tools/inctl/cmd/doctor/checks/cluster_check/clustercheck"
	"intrinsic/tools/inctl/cmd/doctor/checks/inctl_auth_check/inctlauthcheck"
	"intrinsic/tools/inctl/cmd/doctor/checks/solution_check/solutioncheck"
)

// Checks is a list of all the checks, but note that maps are not sorted by insert order, so
// the keys are manually sorted alphabetically wherever used.
var Checks = map[string]*api.DiagnosticCheck{
	inctlauthcheck.Check.Name: &inctlauthcheck.Check,
	solutioncheck.Check.Name:  &solutioncheck.Check,
	clustercheck.Check.Name:   &clustercheck.Check,
}
