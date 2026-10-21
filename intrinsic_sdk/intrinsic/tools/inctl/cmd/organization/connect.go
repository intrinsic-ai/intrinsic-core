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

// File connect.go provides helpers for establishing connections to the accountsservice.
// Note: We are not using the inctl/util/accounts/clients.go package because it does not
// yet resolve --env/--project/--org correctly for the inctl organization command.

import (
	"context"
	"errors"
	"fmt"

	"intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/viperutil"

	"google.golang.org/grpc"

	accaccesscontrolgrpcpb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
	accinvitationsgrpcpb "intrinsic/kubernetes/accounts/service/api/invitations/v1/invitations_go_proto"
	accresourcemanagergrpcpb "intrinsic/kubernetes/accounts/service/api/resourcemanager/v1/resourcemanager_go_proto"
)

func connectInit() {
	organizationCmd.PersistentFlags().BoolVar(&flagDebugRequests, "debug-requests", false, "If true, print the full request and response for each API call.")
	_ = organizationCmd.PersistentFlags().MarkHidden("debug-requests")

	// Register --org and --env flags.
	// We are not using orgutil.Wrap here because that enforced org->project validation
	// which we do not require here.
	organizationCmd.PersistentFlags().String(orgutil.KeyOrganization, "", "The Intrinsic organization to use (can also be set via INTRINSIC_ORG env var).")
	organizationCmd.PersistentFlags().String(orgutil.KeyEnvironment, "", "Auth environment to use (can also be set via INTRINSIC_ENV env var).")
	// Bind --org and --env flags to INTRINSIC_ORG and INTRINSIC_ENV environment variables.
	viperutil.BindFlags(vipr, organizationCmd.PersistentFlags(), viperutil.BindToListEnv(orgutil.KeyOrganization, orgutil.KeyEnvironment))
}

// newResourceManagerV1Client creates a new secure ResourceManagerServiceClient for organization commands.
func newResourceManagerV1Client(ctx context.Context) (accresourcemanagergrpcpb.ResourceManagerServiceClient, error) {
	conn, err := newConn(ctx)
	if err != nil {
		return nil, err
	}
	return accresourcemanagergrpcpb.NewResourceManagerServiceClient(conn), nil
}

// newAccessControlV1Client creates a new secure AccessControlServiceClient for organization commands.
func newAccessControlV1Client(ctx context.Context) (accaccesscontrolgrpcpb.AccessControlServiceClient, error) {
	conn, err := newConn(ctx)
	if err != nil {
		return nil, err
	}
	return accaccesscontrolgrpcpb.NewAccessControlServiceClient(conn), nil
}

// newInvitationsV1Client creates a new secure InvitationsServiceClient for organization commands.
func newInvitationsV1Client(ctx context.Context) (accinvitationsgrpcpb.InvitationsServiceClient, error) {
	conn, err := newConn(ctx)
	if err != nil {
		return nil, err
	}
	return accinvitationsgrpcpb.NewInvitationsServiceClient(conn), nil
}

// newConn creates a new secure connection to the accounts service for organization commands.
func newConn(ctx context.Context) (*grpc.ClientConn, error) {
	project := vipr.GetString(orgutil.KeyProject)
	env := vipr.GetString(orgutil.KeyEnvironment)

	var accountsProject string
	if env != "" {
		accountsProject = environments.AccountsProjectFromEnv(env)
	} else if project != "" {
		accountsProject = environments.AccountsProjectFromProject(project)
	}
	if accountsProject == "" { // always fallback to prod
		accountsProject = environments.AccountsProjectProd
	}

	var connOpts []auth.ConnectionOptsFunc
	if env != "" {
		connOpts = append(connOpts, auth.WithEnv(env))
	}
	connOpts = append(connOpts, auth.WithProject(accountsProject))

	conn, err := auth.NewCloudConnection(ctx, connOpts...)

	if authErr, ok := errors.AsType[*auth.ErrorDetails](err); ok {
		// We are not printing authErr directly here because it mentions the accounts project, which might confuse users.
		return nil, fmt.Errorf("%v: %v", authErr.Message, authErr.Help)
	}
	return conn, err
}
