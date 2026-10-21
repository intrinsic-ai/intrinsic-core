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

// Package cmd contains the root command for the skill installer tool.
package cmd

import (
	"intrinsic/skills/tools/skill/cmd/create/create"
	"intrinsic/skills/tools/skill/cmd/install/install"
	"intrinsic/skills/tools/skill/cmd/list/list"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
)

func init() {
	cmd := cobrautil.ParentOfNestedSubcommands(root.SkillCmdName, "Manage Skill assets.")
	cmd.AddCommand(create.Command())
	cmd.AddCommand(install.Command())
	cmd.AddCommand(list.Command())
	root.RootCmd.AddCommand(cmd)
}
