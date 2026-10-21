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

package organization

import (
	"bytes"
	"fmt"
	"strings"
	"text/tabwriter"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	accaccesscontrolv1pb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
)

var rolesCmd = cobrautil.ParentOfNestedSubcommands("roles", "List available roles.")

func init() {
	organizationCmd.AddCommand(rolesCmd)
	rolesInit(rolesCmd)
}

func rolesInit(root *cobra.Command) {
	root.AddCommand(listRolesCmd)
}

var listRolesCmdHelp = `
List available roles.

Example:
  inctl organization roles list
`

type printableRoles []*accaccesscontrolv1pb.Role

func (r printableRoles) String() string {
	b := new(bytes.Buffer)
	w := tabwriter.NewWriter(b,
		/*minwidth=*/ 1 /*tabwidth=*/, 1 /*padding=*/, 1 /*padchar=*/, ' ' /*flags=*/, 0)
	fmt.Fprintf(w, "%s\t%s\t%s\n", "Name", "Display Name", "Description")
	for _, role := range r {
		fmt.Fprintf(w, "%s\t%s\t%s\n", role.GetName(), role.GetDisplayName(), role.GetDescription())
	}
	w.Flush()
	return strings.TrimSuffix(b.String(), "\n")
}

func (r printableRoles) MarshalJSON() ([]byte, error) {
	return marshalProtoSlice(r)
}

var listRolesCmd = &cobra.Command{
	Use:   "list",
	Short: "List available roles.",
	Long:  listRolesCmdHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		if err := checkOrgNotIntrinsic(); err != nil {
			return err
		}
		ctx := cmd.Context()
		cl, err := newAccessControlV1Client(ctx)
		if err != nil {
			return err
		}
		rs, err := cl.ListRoles(ctx, &accaccesscontrolv1pb.ListRolesRequest{})
		if err != nil {
			return err
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.Print(printableRoles(rs.GetRoles()))
		return nil
	},
}
