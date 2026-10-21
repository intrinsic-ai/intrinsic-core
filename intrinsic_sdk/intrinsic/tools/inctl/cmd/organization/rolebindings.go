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
	"time"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/accounts/accounts"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	pb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
)

// roleBindingOpTimeout is the timeout for waiting on role binding create/delete operations.
// Role binding propagation typically completes within a few seconds; 10 minutes provides
// ample buffer for Spanner replication or temporary backend delays without failing prematurely.
const roleBindingOpTimeout = 10 * time.Minute

var rolebindingsCmd = cobrautil.ParentOfNestedSubcommands("role-bindings", "List the role bindings on a given resource.")

func init() {
	organizationCmd.AddCommand(rolebindingsCmd)
	rolebindingsInit(rolebindingsCmd)
}

func rolebindingsInit(root *cobra.Command) {
	root.AddCommand(listRoleBindingsCmd)
	grantRoleBindingCmd.Flags().StringVar(&flagEmail, "email", "", "The email address of the user to grant the role to.")
	grantRoleBindingCmd.Flags().StringVar(&flagRole, "role", "", "The role to grant.")
	grantRoleBindingCmd.MarkFlagRequired("email")
	grantRoleBindingCmd.MarkFlagRequired("role")
	root.AddCommand(grantRoleBindingCmd)
	revokeRoleBindingCmd.Flags().StringVar(&flagName, "name", "", "The name of the role-binding to revoke taken from the output of the list command.")
	revokeRoleBindingCmd.MarkFlagRequired("name")
	root.AddCommand(revokeRoleBindingCmd)
}

var grantRoleBindingCmdHelp = `
Grant a user a role on an organization and all its descendants.

Example:

		inctl organization role-bindings grant --org=exampleorg --email=user@example.com --role=admin
`

var grantRoleBindingCmd = &cobra.Command{
	Use:   "grant",
	Short: "Grant a user a role on an organization.",
	Long:  grantRoleBindingCmdHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		org, err := processOrgFlag()
		if err != nil {
			return err
		}
		ctx := cmd.Context()
		cl, err := newAccessControlV1Client(ctx)
		if err != nil {
			return err
		}
		req := &pb.CreateRoleBindingRequest{
			RoleBinding: &pb.RoleBinding{
				Resource: addPrefix(org, "organizations/"),
				Role:     addPrefix(flagRole, "roles/"),
				Subject:  addPrefix(flagEmail, "users/"),
			},
		}
		if flagDebugRequests {
			protoPrint(req)
		}
		lrop, err := cl.CreateRoleBinding(ctx, req)
		if err != nil {
			return err
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		lrop, err = accounts.WaitForOperation(ctx, cl.GetOperation, lrop, roleBindingOpTimeout)
		if err != nil {
			return fmt.Errorf("failed to wait for operation: %w", err)
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		return nil
	},
}

var revokeRoleBindingCmdHelp = `
Revoke a role binding by its resource name.

Example:

		inctl organization role-bindings revoke --name=rolebindings/7iawfQMYZAMkx6XdmQdtqJfW+gCZeoT83PcYw0daIrg=
`

var revokeRoleBindingCmd = &cobra.Command{
	Use:   "revoke",
	Short: "Revoke a role binding by its resource name.",
	Long:  revokeRoleBindingCmdHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		if err := checkOrgNotIntrinsic(); err != nil {
			return err
		}
		ctx := cmd.Context()
		cl, err := newAccessControlV1Client(ctx)
		if err != nil {
			return err
		}
		req := &pb.DeleteRoleBindingRequest{
			Name: addPrefix(flagName, "rolebindings/"),
		}
		if flagDebugRequests {
			protoPrint(req)
		}
		lrop, err := cl.DeleteRoleBinding(ctx, req)
		if err != nil {
			return err
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		lrop, err = accounts.WaitForOperation(ctx, cl.GetOperation, lrop, roleBindingOpTimeout)
		if err != nil {
			return fmt.Errorf("failed to wait for operation: %w", err)
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		return nil
	},
}

type printableRoleBindings []*pb.RoleBinding

func (r printableRoleBindings) String() string {
	b := new(bytes.Buffer)
	w := tabwriter.NewWriter(b,
		/*minwidth=*/ 1 /*tabwidth=*/, 1 /*padding=*/, 1 /*padchar=*/, ' ' /*flags=*/, 0)
	fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", "Name", "Resource", "Role", "Subject")
	for _, rb := range r {
		fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", rb.GetName(), rb.GetResource(), rb.GetRole(), rb.GetSubject())
	}
	w.Flush()
	return strings.TrimSuffix(b.String(), "\n")
}

func (r printableRoleBindings) MarshalJSON() ([]byte, error) {
	return marshalProtoSlice(r)
}

var listRoleBindingsCmdHelp = `
List the role bindings on an organization.

Example:

		inctl organization role-bindings list --org=exampleorg
`

var listRoleBindingsCmd = &cobra.Command{
	Use:   "list",
	Short: "List the role bindings on an organization.",
	Long:  listRoleBindingsCmdHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		org, err := processOrgFlag()
		if err != nil {
			return err
		}
		ctx := cmd.Context()
		cOrg := addPrefix(org, "organizations/")
		cl, err := newAccessControlV1Client(ctx)
		if err != nil {
			return err
		}
		req := &pb.ListOrganizationRoleBindingsRequest{
			Parent: cOrg,
		}
		if flagDebugRequests {
			protoPrint(req)
		}
		ret, err := cl.ListOrganizationRoleBindings(ctx, req)
		if err != nil {
			return err
		}
		if flagDebugRequests {
			protoPrint(ret)
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.Print(printableRoleBindings(ret.GetRoleBindings()))
		return nil
	},
}
