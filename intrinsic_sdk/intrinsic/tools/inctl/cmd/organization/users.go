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
	"context"
	"fmt"
	"slices"
	"strings"
	"text/tabwriter"
	"time"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/accounts/accounts"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	pb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
)

// membershipOpTimeout is the timeout for waiting on organization membership operations.
// Removing a member usually finishes within seconds; 10 minutes provides ample buffer
// for background propagation and replication without failing prematurely.
const membershipOpTimeout = 10 * time.Minute

func init() {
	addUserInit(organizationCmd)
	invitationsInit(organizationCmd)
}

func parseCSV(s string) []string {
	parts := strings.Split(s, ",")
	for idx := range parts {
		parts[idx] = strings.TrimSpace(parts[idx])
	}
	return slices.DeleteFunc(parts, func(p string) bool { return p == "" })
}

var membersCmd = cobrautil.ParentOfNestedSubcommands("members", "Manage organization members.")

func addUserInit(rootCmd *cobra.Command) {
	membersCmd.Aliases = []string{"member"}
	addUser.Use = "invite"
	addUser.Flags().StringVar(&flagEmail, "email", "", "The email address of the user to invite.")
	addUser.Flags().StringVar(&flagRoleCSV, "roles", "", "Optional comma-separated list of roles to assign to the user when they accept the invitation.")
	addUser.MarkFlagRequired("email")
	membersCmd.AddCommand(addUser)

	membersCmd.AddCommand(listMembersCmd)

	removeUser.Use = "remove"
	removeUser.Flags().StringVar(&flagEmail, "email", "", "The email address of the user to remove.")
	removeUser.MarkFlagRequired("email")
	membersCmd.AddCommand(removeUser)
	rootCmd.AddCommand(membersCmd)
}

var addUserHelp = `
Invite a user to an organization by email address (short-hand for 'inctl organization invitations create').

Use the --roles flag (comma-separated list) to assign roles to the user after they accept the invitation.

Example:

		inctl organization members invite --email=user@example.com --org=exampleorg --roles=owner
`

var addUser = &cobra.Command{
	Use:   "invite",
	Short: "Invite a user to an organization by email address (short-hand for 'inctl organization invitations create').",
	Long:  addUserHelp,
	RunE:  runCreateInvitation,
}

var removeUserHelp = `
Remove a user from an organization by email address.

Example:

		inctl organization members remove --email=user@example.com --org=exampleorg
`

var removeUser = &cobra.Command{
	Use:   "remove",
	Short: "Remove a user from an organization by email address.",
	Long:  removeUserHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		if err := agents.Check(cmd); err != nil {
			return err
		}
		org, err := processOrgFlag()
		if err != nil {
			return err
		}
		ctx := cmd.Context()
		cl, err := newAccessControlV1Client(ctx)
		if err != nil {
			return err
		}
		req := pb.DeleteOrganizationMembershipByEmailRequest{
			Parent: addPrefix(org, "organizations/"),
			Email:  flagEmail,
		}
		if flagDebugRequests {
			protoPrint(&req)
		}
		op, err := cl.DeleteOrganizationMembershipByEmail(ctx, &req)
		if err != nil {
			return fmt.Errorf("failed to remove member: %w", err)
		}
		if flagDebugRequests {
			protoPrint(op)
		}
		if op, err := accounts.WaitForOperation(ctx, cl.GetOperation, op, membershipOpTimeout); err != nil {
			return fmt.Errorf("failed to remove member (long operation): %w", err)
		} else {
			protoPrint(op)
		}
		return nil
	},
}

var listMembersHelp = `
List active members of an organization along with their assigned roles.

Example:

		inctl organization members list --org=exampleorg
`

var listMembersCmd = &cobra.Command{
	Use:   "list",
	Short: "List active members of an organization.",
	Long:  listMembersHelp,
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
		ms, err := listMemberships(ctx, cl, org)
		if err != nil {
			return err
		}
		rs, err := listRolesBindings(ctx, cl, org)
		if err != nil {
			return err
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.Print(&membersList{ms: ms, rs: rs})
		return nil
	},
}

type membersList struct {
	ms []*pb.OrganizationMembership
	rs []*pb.RoleBinding
}

func (ml *membersList) String() string {
	b := new(bytes.Buffer)
	w := tabwriter.NewWriter(b,
		/*minwidth=*/ 1 /*tabwidth=*/, 1 /*padding=*/, 1 /*padchar=*/, ' ' /*flags=*/, 0)
	fmt.Fprintf(w, "%s\t%s\n", "Email", "Roles")
	slices.SortFunc(ml.ms, func(a, b *pb.OrganizationMembership) int {
		return strings.Compare(a.GetEmail(), b.GetEmail())
	})
	urs := userRoles(ml.rs)
	for _, m := range ml.ms {
		roles := urs[m.GetEmail()]
		fmt.Fprintf(w, "%s\t%s\n", m.GetEmail(), formatRoles(roles))
	}
	w.Flush()
	return strings.TrimSuffix(b.String(), "\n")
}

func (ml *membersList) MarshalJSON() ([]byte, error) {
	return marshalProtoSlice(ml.ms)
}

func listMemberships(ctx context.Context, cl accounts.AccessControlV1Client, org string) ([]*pb.OrganizationMembership, error) {
	req := pb.ListOrganizationMembershipsRequest{
		Parent: addPrefix(org, "organizations/"),
	}
	if flagDebugRequests {
		protoPrint(&req)
	}
	op, err := cl.ListOrganizationMemberships(ctx, &req)
	if err != nil {
		return nil, fmt.Errorf("failed to list memberships: %w", err)
	}
	if flagDebugRequests {
		protoPrint(op)
	}
	return op.GetMemberships(), nil
}

func listRolesBindings(ctx context.Context, cl accounts.AccessControlV1Client, org string) ([]*pb.RoleBinding, error) {
	req := pb.ListOrganizationRoleBindingsRequest{
		Parent: addPrefix(org, "organizations/"),
	}
	if flagDebugRequests {
		protoPrint(&req)
	}
	op, err := cl.ListOrganizationRoleBindings(ctx, &req)
	if err != nil {
		return nil, fmt.Errorf("failed to list role bindings: %w", err)
	}
	if flagDebugRequests {
		protoPrint(op)
	}
	return op.GetRoleBindings(), nil
}

func userRoles(rs []*pb.RoleBinding) map[string][]string {
	roles := make(map[string][]string)
	for _, r := range rs {
		subject := strings.TrimPrefix(r.GetSubject(), "users/")
		roles[subject] = append(roles[subject], r.GetRole())
	}
	return roles
}

func formatRoles(rs []string) string {
	roles := []string{}
	for _, r := range rs {
		roles = append(roles, strings.TrimPrefix(r, "roles/"))
	}
	slices.Sort(roles)
	return strings.Join(roles, ", ")
}
