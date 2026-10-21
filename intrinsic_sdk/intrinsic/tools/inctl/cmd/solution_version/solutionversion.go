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

// Package solutionversion contains all commands for solution version handling.
package solutionversion

import (
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

var viperLocal = viper.New()

// SolutionVersionCmd is the `inctl solution_version` command.
var SolutionVersionCmd = orgutil.WrapCmd(&cobra.Command{
	Use:                root.SolutionVersionCmdName,
	Aliases:            []string{"sv"},
	Short:              "SolutionVersion interacts with solution versions",
	DisableFlagParsing: true,
}, viperLocal)

func init() {
	root.RootCmd.AddCommand(SolutionVersionCmd)
}
