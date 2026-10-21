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

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/accounts/accounts"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	pb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
)

var invitationsCmd = cobrautil.ParentOfNestedSubcommands("invitations", "Manage organization invitations.")

func invitationsInit(rootCmd *cobra.Command) {
	invitationsCmd.Aliases = []string{"invitation"}

	createInvitationCmd.Flags().StringVar(&flagEmail, "email", "", "The email address of the user to invite.")
	createInvitationCmd.Flags().StringVar(&flagRoleCSV, "roles", "", "Optional comma-separated list of roles to assign to the user when they accept the invitation.")
	createInvitationCmd.MarkFlagRequired("email")
	invitationsCmd.AddCommand(createInvitationCmd)

	invitationsCmd.AddCommand(listInvitationsCmd)

	getInvitationCmd.Flags().StringVar(&flagInvitationToken, "token", "", "The token of the invitation to get.")
	invitationsCmd.AddCommand(getInvitationCmd)

	withdrawInvitation.Use = "withdraw"
	withdrawInvitation.Flags().StringVar(&flagInvitationToken, "token", "", "The token of the invitation to withdraw.")
	withdrawInvitation.MarkFlagRequired("token")
	invitationsCmd.AddCommand(withdrawInvitation)

	resendInvitation.Use = "resend"
	resendInvitation.Flags().StringVar(&flagInvitationToken, "token", "", "The token of the invitation to resend.")
	resendInvitation.MarkFlagRequired("token")
	invitationsCmd.AddCommand(resendInvitation)

	rootCmd.AddCommand(invitationsCmd)
}

var createInvitationHelp = `
Create an invitation for a user to join an organization by email address.

Use the --roles flag (comma-separated list) to assign roles to the user after they accept the invitation.

Example:

		inctl organization invitations create --email=user@example.com --org=exampleorg --roles=owner
`

var createInvitationCmd = &cobra.Command{
	Use:   "create",
	Short: "Create an invitation for a user to join an organization by email address.",
	Long:  createInvitationHelp,
	RunE:  runCreateInvitation,
}

func runCreateInvitation(cmd *cobra.Command, args []string) error {
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
	req := pb.CreateOrganizationInvitationRequest{
		Parent: addPrefix(org, "organizations/"),
		Invitation: &pb.OrganizationInvitation{
			Organization: org,
			Email:        flagEmail,
			Roles:        addPrefixes(parseCSV(flagRoleCSV), "roles/"),
		},
	}
	if flagDebugRequests {
		protoPrint(&req)
	}
	op, err := cl.CreateOrganizationInvitation(ctx, &req)
	if err != nil {
		return fmt.Errorf("failed to create organization invitation: %w", err)
	}
	if flagDebugRequests {
		protoPrint(op)
	}
	return nil
}

var listInvitationsHelp = `
List pending user invitations of an organization.

Example:

		inctl organization invitations list --org=exampleorg
`

var listInvitationsCmd = &cobra.Command{
	Use:   "list",
	Short: "List pending invitations of an organization.",
	Long:  listInvitationsHelp,
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
		ois, err := listInvitations(ctx, cl, org)
		if err != nil {
			return err
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.Print(&invitationsList{is: ois})
		return nil
	},
}

type invitationsList struct {
	is []*pb.OrganizationInvitation
}

func (il *invitationsList) String() string {
	b := new(bytes.Buffer)
	w := tabwriter.NewWriter(b,
		/*minwidth=*/ 1 /*tabwidth=*/, 1 /*padding=*/, 1 /*padchar=*/, ' ' /*flags=*/, 0)
	fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", "Email", "Roles", "Status", "Token")
	slices.SortFunc(il.is, func(a, b *pb.OrganizationInvitation) int {
		return strings.Compare(a.GetEmail(), b.GetEmail())
	})
	for _, o := range il.is {
		fmt.Fprintf(w, "%s\t%s\t%s\t%s\n", o.GetEmail(), formatRoles(o.GetRoles()), o.GetState(), extractToken(o.GetName()))
	}
	w.Flush()
	return strings.TrimSuffix(b.String(), "\n")
}

func (il *invitationsList) MarshalJSON() ([]byte, error) {
	return marshalProtoSlice(il.is)
}

func listInvitations(ctx context.Context, cl accounts.AccessControlV1Client, org string) ([]*pb.OrganizationInvitation, error) {
	req := pb.ListOrganizationInvitationsRequest{
		Parent: addPrefix(org, "organizations/"),
	}
	if flagDebugRequests {
		protoPrint(&req)
	}
	op, err := cl.ListOrganizationInvitations(ctx, &req)
	if err != nil {
		return nil, fmt.Errorf("failed to list organization invitations: %w", err)
	}
	if flagDebugRequests {
		protoPrint(op)
	}
	return op.GetInvitations(), nil
}

var withdrawInvitationHelp = `
Withdraw an invitation sent to an email address. This command marks an invitation as cancelled
making it no longer available to the user.

Example:

		inctl organization invitations withdraw --org=exampleorg --token=24d7ab1f-8c55-4903-9352-4ce421bef264
`

var withdrawInvitation = &cobra.Command{
	Use:   "withdraw",
	Short: "Withdraw an invitation",
	Long:  withdrawInvitationHelp,
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
		name := addPrefix(org, "organizations/") + "/" + addPrefix(flagInvitationToken, "invitations/")
		req := pb.CancelOrganizationInvitationRequest{
			Name: name,
		}
		if flagDebugRequests {
			protoPrint(&req)
		}
		_, err = cl.CancelOrganizationInvitation(ctx, &req)
		if err != nil {
			return err
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.PrintSf("Invitation %s was successfully withdrawn.", name)
		return nil
	},
}

var resendInvitationHelp = `
Resend an existing invitation to an email address.

Example:

		inctl organization invitations resend --org=exampleorg --token=24d7ab1f-8c55-4903-9352-4ce421bef264
`

var resendInvitation = &cobra.Command{
	Use:   "resend",
	Short: "Resend an invitation",
	Long:  resendInvitationHelp,
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
		name := addPrefix(org, "organizations/") + "/" + addPrefix(flagInvitationToken, "invitations/")
		req := pb.ResendOrganizationInvitationRequest{
			Name: name,
		}
		if flagDebugRequests {
			protoPrint(&req)
		}
		_, err = cl.ResendOrganizationInvitation(ctx, &req)
		if err != nil {
			return err
		}
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		prtr.PrintSf("Invitation %s was successfully resent.", name)
		return nil
	},
}

func extractToken(name string) string {
	parts := strings.Split(name, "/")
	return parts[len(parts)-1]
}

var getInvitationHelp = `
Get details of a pending user invitation by token.

Example:

		inctl organization invitations get 24d7ab1f-8c55-4903-9352-4ce421bef264 --org=exampleorg
		inctl organization invitations get --token=24d7ab1f-8c55-4903-9352-4ce421bef264 --org=exampleorg
`

var getInvitationCmd = &cobra.Command{
	Use:   "get [token]",
	Short: "Get details of a pending invitation.",
	Long:  getInvitationHelp,
	Args:  cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		token, err := resolveInvitationTokenArgOrFlag(args)
		if err != nil {
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
		// Use list here as accesscontrol  service does not implement a get method yet
		invs, err := listInvitations(ctx, cl, org)
		if err != nil {
			return err
		}
		for _, inv := range invs {
			if extractToken(inv.GetName()) == token {
				protoPrint(inv)
				return nil
			}
		}
		return fmt.Errorf("invitation %q not found", token)
	},
}

func resolveInvitationTokenArgOrFlag(args []string) (string, error) {
	if len(args) > 0 && args[0] != "" {
		return args[0], nil
	}
	if flagInvitationToken != "" {
		return flagInvitationToken, nil
	}
	return "", fmt.Errorf("invitation token is required via positional argument or --token flag")
}
