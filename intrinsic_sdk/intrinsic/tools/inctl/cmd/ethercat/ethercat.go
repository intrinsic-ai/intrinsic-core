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

// Package ethercat contains the externally available commands for ethercat handling.
package ethercat

import (
	"intrinsic/assets/clientutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
)

var (
	// EtherCATCmd is the `inctl ethercat` command.
	EtherCATCmd = cobrautil.ParentOfNestedSubcommands(
		"ethercat", "Workcell EtherCAT handling")

	// Defined here, so that it can be replaced in tests.
	clientutilsDialClusterFromInctl = clientutils.DialClusterFromInctl
)

func init() {
	root.RootCmd.AddCommand(EtherCATCmd)
}
