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

// Package catalog contains the inctl asset catalog command.
package catalog

import (
	"intrinsic/assets/inctl/catalog/get"
	"intrinsic/assets/inctl/catalog/list"
	"intrinsic/assets/inctl/catalog/updatereleasemetadata"
	"intrinsic/tools/inctl/util/cobrautil"

	"github.com/spf13/cobra"
)

// Command returns the parent command for the asset catalog and registers subcommands.
func Command() *cobra.Command {
	cmd := cobrautil.ParentOfNestedSubcommands("catalog", "Manage Assets in the Asset Catalog.")
	cmd.AddCommand(get.Command())
	cmd.AddCommand(list.Command())
	cmd.AddCommand(updatereleasemetadata.Command())
	return cmd
}
