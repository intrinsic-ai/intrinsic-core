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

// Package doctor contains the root command for the inctl doctor command.
package doctor

import (
	"intrinsic/tools/inctl/cmd/doctor/api/api"
	"intrinsic/tools/inctl/cmd/doctor/cmds/check/check"
	"intrinsic/tools/inctl/cmd/doctor/cmds/list_checks/listchecks"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
)

func setPrinterFromOutputFlag(command *cobra.Command, args []string) (err error) {
	if out, err := printer.NewPrinter(root.FlagOutput); err == nil {
		command.SetOut(out)
	}
	return
}

var doctorCmd = &cobra.Command{
	Use:               root.DoctorCmdName,
	Short:             check.CheckCmd.Short,
	Long:              "Check your environment for common problems and return a non-zero exit code if any problems are found.",
	PersistentPreRunE: setPrinterFromOutputFlag,
}

func init() {
	api.CmdFlags.SetCommand(doctorCmd)
	api.CmdFlags.AddFlagsAddressClusterSolution()
	api.CmdFlags.AddFlagsProjectOrgOptional()

	doctorCmd.AddCommand(check.CheckCmd)
	doctorCmd.AddCommand(listchecks.ListChecksCmd)

	root.RootCmd.AddCommand(doctorCmd)
}
