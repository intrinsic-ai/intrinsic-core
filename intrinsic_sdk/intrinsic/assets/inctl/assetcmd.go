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

// Package assetcmd contains the root command for the inctl asset command.
package assetcmd

import (
	"intrinsic/assets/inctl/catalog"
	"intrinsic/assets/inctl/getreleased"
	"intrinsic/assets/inctl/install"
	"intrinsic/assets/inctl/instance"
	"intrinsic/assets/inctl/list"
	"intrinsic/assets/inctl/listreleased"
	"intrinsic/assets/inctl/listreleasedversions"
	"intrinsic/assets/inctl/release"
	"intrinsic/assets/inctl/uninstall"
	"intrinsic/assets/inctl/updatereleasemetadata"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
)

func init() {
	cmd := cobrautil.ParentOfNestedSubcommands(root.AssetCmdName, "Manage assets.")
	cmd.AddCommand(catalog.Command())
	cmd.AddCommand(getreleased.GetCommand())
	cmd.AddCommand(install.GetCommand())
	cmd.AddCommand(instance.Command())
	cmd.AddCommand(list.GetCommand(""))
	cmd.AddCommand(listreleased.GetCommand())
	cmd.AddCommand(listreleasedversions.GetCommand())
	cmd.AddCommand(release.GetCommand())
	cmd.AddCommand(uninstall.GetCommand())
	cmd.AddCommand(updatereleasemetadata.GetCommand())
	root.RootCmd.AddCommand(cmd)
}
