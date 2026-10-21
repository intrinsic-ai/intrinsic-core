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

// Package instance contains the inctl asset instance command.
package instance

import (
	"intrinsic/assets/inctl/instance/get"
	"intrinsic/assets/inctl/instance/list"

	"github.com/spf13/cobra"
)

// Command returns the parent command for asset instances and registers subcommands.
func Command() *cobra.Command {
	cmd := &cobra.Command{
		Use:     "instance",
		Aliases: []string{"instances"},
		Short:   "Manage Asset instances.",
	}
	cmd.AddCommand(get.Command())
	cmd.AddCommand(list.Command())
	return cmd
}
