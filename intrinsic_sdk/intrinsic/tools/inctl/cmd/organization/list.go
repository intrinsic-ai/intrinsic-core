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
	"context"
	"fmt"
	"slices"
	"strings"
	"time"

	"intrinsic/tools/inctl/util/accounts/accounts"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"golang.org/x/sync/errgroup"

	accresourcemanager1pb "intrinsic/kubernetes/accounts/service/api/resourcemanager/v1/resourcemanager_go_proto"
)

func init() {
	organizationInit(organizationCmd)
}

func organizationInit(root *cobra.Command) {
	listCmd.Flags().BoolVar(&flagShowDeleted, "show-deleted", false, "If true, also return deleted organizations.")
	listCmd.Flags().StringVar(&flagParent, "parent", "", "If set, only return organizations that are children of the given parent organization.")
	root.AddCommand(listCmd)
}

func getOrgRowCommandPrinter(cmd *cobra.Command) printer.CommandPrinter {
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeText {
		ot = printer.OutputTypeTAB
	}
	cp, err := printer.NewPrinterOfType(
		ot,
		cmd,
		printer.WithDefaultsFromValue(&accresourcemanager1pb.Organization{}, func(columns []string) []string {
			return []string{"name", "display_name", "state", "parent_organization", "create_time", "delete_time", "membership_status"}
		}),
		printer.WithHeaderOverride(func(s string) string {
			return orgColumnNames[s]
		}),
	)
	if err != nil {
		cmd.PrintErrf("Error setting up output: %v\n", err)
		cp = printer.GetDefaultPrinter(cmd)
	}
	return cp
}

var orgColumnNames = map[string]string{
	"name":                "Name",
	"display_name":        "Display Name",
	"state":               "State",
	"parent_organization": "Parent",
	"create_time":         "Create Time",
	"delete_time":         "Delete Time",
	"membership_status":   "Membership Status",
}

type orgRow struct {
	Name             string `json:"name"`
	DisplayName      string `json:"display_name"`
	State            string `json:"state"`
	Parent           string `json:"parent_organization"`
	CreateTime       string `json:"create_time"`
	DeleteTime       string `json:"delete_time"`
	MembershipStatus bool   `json:"membership_status"`
}

func toOrgRow(o *accresourcemanager1pb.Organization, msMap map[string]bool) *orgRow {
	or := &orgRow{
		Name:             strings.TrimPrefix(o.GetName(), orgPrefix),
		DisplayName:      o.GetDisplayName(),
		State:            o.GetState().String(),
		Parent:           strings.TrimPrefix(o.GetParentOrganization(), orgPrefix),
		CreateTime:       o.GetCreateTime().AsTime().Format(time.RFC3339),
		DeleteTime:       o.GetDeleteTime().AsTime().Format(time.RFC3339),
		MembershipStatus: msMap[o.GetName()],
	}
	if !o.GetDeleteTime().IsValid() {
		or.DeleteTime = "n/a"
	}
	if or.State == "STATE_UNSPECIFIED" {
		or.State = "n/a"
	}
	return or
}

func printOrganizations(cmd *cobra.Command, orgs []*orgRow) error {
	prtr := getOrgRowCommandPrinter(cmd)
	var view printer.View = nil // this is to reuse reflectors in default views
	for _, o := range orgs {
		view = printer.NextView(o, view)
		prtr.Println(view)
	}
	return printer.Flush(prtr)
}

var listCmdHelp = `
List your organizations.

List all organizations you have at least metadata access to. The membership status indicates
whether you are also a member of the organization or if you are granted access to the organization
through a role binding without being a member.

Use the --parent flag to filter organizations by parent.

Example:

		inctl organization list --parent=myorg
`

var listCmd = &cobra.Command{
	Use:   "list",
	Short: "List organizations.",
	Long:  listCmdHelp,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		cl, err := newResourceManagerV1Client(ctx)
		if err != nil {
			return err
		}
		wg := errgroup.Group{}
		// list all the organizations the user has access to
		var os []*accresourcemanager1pb.Organization
		wg.Go(func() error {
			var err error
			os, err = listMyOrganizations(ctx, cl, flagShowDeleted, "")
			if err != nil {
				return err
			}
			return nil
		})
		// list a second time with the filter to get only the organizations the user is a member of
		var msOs []*accresourcemanager1pb.Organization
		wg.Go(func() error {
			var err error
			msOs, err = listMyOrganizations(ctx, cl, flagShowDeleted, "is_member()")
			if err != nil {
				return err
			}
			return nil
		})
		if err := wg.Wait(); err != nil {
			return err
		}
		msMap := make(map[string]bool)
		for _, o := range msOs {
			msMap[o.GetName()] = true
		}
		pp := "" // filter parent, with prefix
		if flagParent != "" {
			pp = addPrefix(flagParent, orgPrefix)
		}
		// convert to orgRows and sort them
		orgs := make([]*orgRow, 0, len(os))
		for _, o := range os {
			if pp != "" && o.GetParentOrganization() != pp {
				continue
			}
			orgs = append(orgs, toOrgRow(o, msMap))
		}
		slices.SortFunc(orgs, func(a, b *orgRow) int { return strings.Compare(a.Name, b.Name) })
		if err := printOrganizations(cmd, orgs); err != nil {
			return err
		}
		return nil
	},
}

func listMyOrganizations(ctx context.Context, cl accounts.ResourceManagerV1Client, showDeleted bool, filter string) ([]*accresourcemanager1pb.Organization, error) {
	req := &accresourcemanager1pb.ListOrganizationsRequest{
		ShowDeleted: showDeleted,
		Filter:      filter,
	}
	resp, err := cl.ListOrganizations(ctx, req)
	if err != nil {
		return nil, fmt.Errorf("failed to list organizations: %w", err)
	}
	return resp.GetOrganizations(), nil
}
