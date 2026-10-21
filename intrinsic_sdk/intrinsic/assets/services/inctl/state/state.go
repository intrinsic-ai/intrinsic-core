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

// Package state contains commands for introspecting and modifying the state of a running
// service asset in a solution.
package state

import (
	"intrinsic/assets/services/inctl/state/disable"
	"intrinsic/assets/services/inctl/state/enable"
	"intrinsic/assets/services/inctl/state/get"
	"intrinsic/assets/services/inctl/state/list"
	"intrinsic/assets/services/inctl/state/restart"

	"github.com/spf13/cobra"
)

// ServiceStateCmd is the super-command for commands to introspect and modify the state of a
// running service asset in a solution.
var ServiceStateCmd = &cobra.Command{
	Use:   "state",
	Short: "Display or modify the state of a running Service instance in a solution.",
	Long:  `Display or modify the state of a running Service instance in a solution.`,
}

func init() {
	ServiceStateCmd.AddCommand(get.Command())
	ServiceStateCmd.AddCommand(list.Command())
	ServiceStateCmd.AddCommand(disable.Command())
	ServiceStateCmd.AddCommand(enable.Command())
	ServiceStateCmd.AddCommand(restart.Command())
}
