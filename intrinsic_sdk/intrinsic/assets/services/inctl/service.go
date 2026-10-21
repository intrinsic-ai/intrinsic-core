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

// Package service contains all commands for handling service assets.
package service

import (
	"intrinsic/assets/services/inctl/add"
	"intrinsic/assets/services/inctl/create/create"
	deletecmd "intrinsic/assets/services/inctl/delete"
	servicestate "intrinsic/assets/services/inctl/state/state"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
)

func init() {
	cmd := cobrautil.ParentOfNestedSubcommands(root.ServiceCmdName, "Manage Service assets.")
	cmd.AddCommand(add.GetCommand())
	cmd.AddCommand(create.Command())
	cmd.AddCommand(deletecmd.GetCommand())
	cmd.AddCommand(servicestate.ServiceStateCmd)
	root.RootCmd.AddCommand(cmd)
}
