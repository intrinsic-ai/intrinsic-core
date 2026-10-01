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

// Package solution contains all commands for solution handling.
package solution

import (
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/cmd/solution/create/create"
	"intrinsic/tools/inctl/cmd/solution/delete/delete"
	"intrinsic/tools/inctl/cmd/solution/get/get"
	"intrinsic/tools/inctl/cmd/solution/list/list"
	"intrinsic/tools/inctl/cmd/solution/migrate/migrate"
	"intrinsic/tools/inctl/cmd/solution/share/share"
	"intrinsic/tools/inctl/cmd/solution/start/start"
	"intrinsic/tools/inctl/cmd/solution/stop/stop"

	"github.com/spf13/cobra"
)

func init() {
	cmd := &cobra.Command{
		Use:                root.SolutionCmdName,
		Aliases:            []string{root.SolutionsCmdName},
		Short:              "Solution interacts with solutions",
		DisableFlagParsing: true,
	}

	cmd.AddCommand(get.NewCommand())
	cmd.AddCommand(list.NewCommand())
	cmd.AddCommand(create.NewCommand())
	cmd.AddCommand(share.NewCommand())
	cmd.AddCommand(start.NewCommand())
	cmd.AddCommand(stop.NewCommand())
	cmd.AddCommand(delete.NewCommand())
	cmd.AddCommand(migrate.NewCommand())

	root.RootCmd.AddCommand(cmd)
}
