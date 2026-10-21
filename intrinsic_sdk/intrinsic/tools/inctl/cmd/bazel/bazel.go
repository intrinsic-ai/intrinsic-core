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

// Package bazel contains the 'inctl bazel' sub-command.
package bazel

import (
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
)

// bazelCmd is the super-command for managing Bazel workspaces when working
// with the Intrinsic SDK.
var bazelCmd = cobrautil.ParentOfNestedSubcommands("bazel", "Interact with Bazel workspaces")

func init() {
	root.RootCmd.AddCommand(bazelCmd)
}
