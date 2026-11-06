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

package accounts

import (
	"context"
	"fmt"
	"io"
	"strings"

	"intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/viper"
	"google.golang.org/grpc"

	accaccesscontrolgrpcpb "intrinsic/kubernetes/accounts/service/api/accesscontrol/v1/accesscontrol_go_proto"
	accinvitationsgrpcpb "intrinsic/kubernetes/accounts/service/api/invitations/v1/invitations_go_proto"
	accresourcemanagergrpcpb "intrinsic/kubernetes/accounts/service/api/resourcemanager/v1/resourcemanager_go_proto"
	accdiscoverv1grpcpb "intrinsic/kubernetes/accounts/service/api/v1/discoveryapi_go_proto"
)

// NewResourceManagerV1Client creates a new secure ResourceManagerServiceClient using API keys.
// Aliases for convenience
var NewResourceManagerV1Client = func(ctx context.Context, vipr *viper.Viper) (ResourceManagerV1Client, error) {
	conn, err := newConn(ctx, vipr)
	if err != nil {
		return nil, err
	}
	return accresourcemanagergrpcpb.NewResourceManagerServiceClient(conn), nil
}

// NewAccessControlV1Client creates a new secure AccessControlServiceClient using API keys.
// Aliases for convenience
var NewAccessControlV1Client = func(ctx context.Context, vipr *viper.Viper) (AccessControlV1Client, error) {
	conn, err := newConn(ctx, vipr)
	if err != nil {
		return nil, err
	}
	return accaccesscontrolgrpcpb.NewAccessControlServiceClient(conn), nil
}

// NewInvitationsV1Client creates a new secure InvitationsServiceClient using API keys.
// Aliases for convenience
var NewInvitationsV1Client = func(ctx context.Context, vipr *viper.Viper) (InvitationsV1Client, error) {
	conn, err := newConn(ctx, vipr)
	if err != nil {
		return nil, err
	}
	return accinvitationsgrpcpb.NewInvitationsServiceClient(conn), nil
}

// discoveryRetryPolicy is the gRPC retry policy for AccountsDiscoveryService.
// It retries read-only discovery calls on UNAVAILABLE errors up to 5 times.
const discoveryRetryPolicy = `{
  "methodConfig": [{
    "name": [{"service": "intrinsic_proto.accountsdiscovery.v1.AccountsDiscoveryService"}],
    "retryPolicy": {
      "maxAttempts": 5,
      "initialBackoff": "1s",
      "maxBackoff": "10s",
      "backoffMultiplier": 2.0,
      "retryableStatusCodes": ["UNAVAILABLE"]
    }
  }]
}`

// DialDiscoveryV1Client dials the accounts discovery service for the given environment using an API key.
// The caller must close the returned io.Closer when done.
func DialDiscoveryV1Client(ctx context.Context, env, apiKey string) (DiscoveryV1Client, io.Closer, error) {
	conn, err := dialAccounts(
		ctx,
		env,
		auth.WithAPIKey(apiKey),
		auth.WithDialOptions(grpc.WithDefaultServiceConfig(discoveryRetryPolicy)),
	)
	if err != nil {
		return nil, nil, err
	}
	return accdiscoverv1grpcpb.NewAccountsDiscoveryServiceClient(conn), conn, nil
}

// newConn creates a new secure connection to the global accounts service using auth.NewCloudConnection.
// The target accounts domain and environment are resolved from --env, --project, or --org (defaulting to prod).
func newConn(ctx context.Context, vipr *viper.Viper) (*grpc.ClientConn, error) {
	env, project := resolveEnvAndProject(vipr, auth.DefaultStore)
	var opts []auth.ConnectionOptsFunc
	if project != "" {
		opts = append(opts, auth.WithProject(project))
	}
	return dialAccounts(ctx, env, opts...)
}

func dialAccounts(ctx context.Context, env string, extraOpts ...auth.ConnectionOptsFunc) (*grpc.ClientConn, error) {
	// TODO(b/410760339): Once the environments package is removed, resolve the
	// accounts domain for --env via inctl/auth (defaulting to "accounts.intrinsic.ai").
	addr := environments.AccountsDomain(env)
	if addr == "" {
		return nil, fmt.Errorf("unknown environment %q; must be one of %s", env, strings.Join(environments.All, ", "))
	}
	opts := append([]auth.ConnectionOptsFunc{
		auth.WithAddress(addr),
		auth.WithEnv(env),
	}, extraOpts...)
	return auth.NewCloudConnection(ctx, opts...)
}

func resolveEnvAndProject(vipr *viper.Viper, store *auth.Store) (string, string) {
	env := vipr.GetString(orgutil.KeyEnvironment)
	project := vipr.GetString(orgutil.KeyProject)
	if project == "" {
		if org := vipr.GetString(orgutil.KeyOrganization); org != "" {
			project = orgutil.ProjectFromOrg(org)
			if project == "" && store != nil {
				if info, err := store.ReadOrgInfo(org); err == nil {
					project = info.Project
				}
			}
		}
	}
	// TODO(b/410760339, b/530456416): Once inctl auth login persists Environment in
	// OrgInfo, read info.Environment directly from store.ReadOrgInfo(org) (defaulting to
	// "prod") instead of inferring it from project via environments.FromAnyProject.
	if env == "" {
		if project != "" {
			env = environments.FromAnyProject(project)
		} else {
			env = environments.Prod
		}
	}
	return env, project
}

// ResourceManagerV1Client is the client for the ResourceManagerService.
// Aliases for convenience
type ResourceManagerV1Client = accresourcemanagergrpcpb.ResourceManagerServiceClient

// AccessControlV1Client is the client for the AccessControlService.
// Aliases for convenience
type AccessControlV1Client = accaccesscontrolgrpcpb.AccessControlServiceClient

// InvitationsV1Client is the client for the InvitationsService.
// Aliases for convenience
type InvitationsV1Client = accinvitationsgrpcpb.InvitationsServiceClient

// DiscoveryV1Client is the client for the AccountsDiscoveryService.
// Aliases for convenience
type DiscoveryV1Client = accdiscoverv1grpcpb.AccountsDiscoveryServiceClient
