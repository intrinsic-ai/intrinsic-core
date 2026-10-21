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

// inctl_external is the subset of inctl that we release to external developers. 
package main

import (
	"intrinsic/tools/inctl/cmd/root"

	_ "intrinsic/assets/inctl/assetcmd"
	_ "intrinsic/assets/services/inctl/service"
	_ "intrinsic/skills/tools/skill/cmd/cmd"
	_ "intrinsic/tools/inctl/cmd/auth/auth"
	_ "intrinsic/tools/inctl/cmd/bazel/bazel"
	_ "intrinsic/tools/inctl/cmd/cluster/cluster"
	_ "intrinsic/tools/inctl/cmd/device/device"
	_ "intrinsic/tools/inctl/cmd/doctor/doctor"
	_ "intrinsic/tools/inctl/cmd/ethercat/ethercat"
	_ "intrinsic/tools/inctl/cmd/gpio"
	_ "intrinsic/tools/inctl/cmd/icon"
	_ "intrinsic/tools/inctl/cmd/logs/logs"
	_ "intrinsic/tools/inctl/cmd/markdown"
	_ "intrinsic/tools/inctl/cmd/notebook/notebook"
	_ "intrinsic/tools/inctl/cmd/organization/organization"
	_ "intrinsic/tools/inctl/cmd/process/process"
	_ "intrinsic/tools/inctl/cmd/recordings/recordings"
	_ "intrinsic/tools/inctl/cmd/solution/solution"
	_ "intrinsic/tools/inctl/cmd/solution_version/solutionversion"
	_ "intrinsic/tools/inctl/cmd/version/version"
	_ "intrinsic/tools/inctl/cmd/vm/vm"
	_ "intrinsic/tools/inctl/cmd/world"
)

func main() {
	root.Inctl()
}
