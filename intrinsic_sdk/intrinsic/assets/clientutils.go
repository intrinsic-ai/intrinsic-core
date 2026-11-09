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

// Package clientutils provides utils for supporting catalog clients and authentication.
package clientutils

import (
	"context"
	"fmt"
	"io/fs"
	"strings"

	"intrinsic/assets/baseclientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/config/environments"
	"intrinsic/kubernetes/acl/identity"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/vmalias" 

	clusterdiscoverypb "intrinsic/frontend/cloud/api/v1/clusterdiscovery_api_go_proto"
	solutiondiscoverygrpcpb "intrinsic/frontend/cloud/api/v1/solutiondiscovery_api_go_proto"
	solutiondiscoverypb "intrinsic/frontend/cloud/api/v1/solutiondiscovery_api_go_proto"

	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc" 
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
)

const (
	defaultCatalogProject = "intrinsic-assets-prod"
)

var (
	errNoInfoToAuthenticate = errors.New("GCP project or api-key is required for authentication, but neither could be determined (try providing `--org=<org>@<project>` to specify the project)")
)

// DialClusterFromInctl creates a connection to a cluster from an inctl command.
func DialClusterFromInctl(ctx context.Context, flags *cmdutils.CmdFlags) (context.Context, *grpc.ClientConn, string, error) {
	project := flags.GetFlagProject()
	org := flags.GetFlagOrganization()
	address, cluster, solution, err := flags.GetFlagsAddressClusterSolution()
	if err != nil {
		return ctx, nil, "", err
	}

	return DialCluster(ctx, project, org, address, cluster, solution)
}

// DialCluster creates a connection to the cluster uniquely identified by a
// combination of project, organization, address, cluster, and solution. These values may be
// different from those specified in command line flags.
//
// The following rules apply:
//  1. Address is prioritized over cluster, which is prioritized over solution.
//  2. Cluster must be specified if address is a project endpoint.
//  3. Org can be project qualified (i.e. org@project)
//  4. Project does not need to be specified if org is project qualified,
//     but is prioritized over the project qualified if both are specified.
func DialCluster(ctx context.Context, project, org, address, cluster, solution string) (context.Context, *grpc.ClientConn, string, error) {

	// resolve vm pool aliases if possible
	if resolved, err := vmalias.Resolve(cluster); err == nil {
		cluster = resolved.VM
		project = resolved.Project
	}


	// Only lookup cluster using the solution if cluster is not set.  We
	// probably shouldn't be in this scenario right now, but this isn't the
	// spot to enforce that.
	if solution != "" && cluster == "" {
		ctx, conn, _, err := dialConnectionCtx(ctx, dialInfoParams{
			Address: address,
			CredOrg: org,
			Project: project,
		})
		if err != nil {
			return ctx, nil, "", fmt.Errorf("could not create connection options for cluster: %v", err)
		}
		defer conn.Close()

		cluster, err = getClusterNameFromSolution(ctx, conn, solution)
		if err != nil {
			return ctx, nil, "", fmt.Errorf("could not get cluster name from solution: %v", err)
		}
	}

	ctx, conn, address, err := dialConnectionCtx(ctx, dialInfoParams{
		Address: address,
		Cluster: cluster,
		CredOrg: org,
		Project: project,
	})
	if err != nil {
		return ctx, nil, "", fmt.Errorf("could not create connection options for the installer: %v", err)
	}

	return ctx, conn, address, nil
}

// DialCatalogFromInctl creates a connection to an asset catalog service from an inctl command.
func DialCatalogFromInctl(ctx context.Context, flags *cmdutils.CmdFlags) (context.Context, *grpc.ClientConn, error) {
	return DialCatalog(ctx, DialCatalogOptions{
		Address: flags.GetFlagCatalogAddress(), 
		APIKey:  flags.GetFlagsCredentials(),   
		Env:     flags.GetFlagEnvironment(),
		Org:     flags.GetFlagOrganization(),
		Project: flags.GetFlagProject(),
	})
}

// DialCatalogOptions specifies the options for DialCatalog.
type DialCatalogOptions struct {
	Address string
	APIKey  string
	Env     string
	Org     string
	Project string // Optional project used to infer environment or load project credentials.
}

func dialCatalogOptions(ctx context.Context, opts DialCatalogOptions) (context.Context, []grpc.DialOption, string, error) {
	credProject := opts.Project
	if credProject == "" && opts.Org != "" {
		info, err := getOrgInfo(opts.Org)
		if err != nil {
			return nil, nil, "", fmt.Errorf("cannot resolve organization %q: %w", opts.Org, err)
		}
		credProject = info.Project
	}

	// TODO(b/410760339, b/530456416): Once inctl auth login persists Environment in
	// OrgInfo (and the local project->env index), read env from OrgInfo/index instead of
	// deriving it from credProject via environments.FromAnyProject.
	env := opts.Env
	if env == "" {
		if credProject != "" {
			env = environments.FromAnyProject(credProject)
		} else {
			env = environments.Prod
		}
	}

	address, err := resolveCatalogAddress(resolveCatalogAddressOptions{
		Address: opts.Address,
		Env:     env,
	})
	if err != nil {
		return nil, nil, "", fmt.Errorf("cannot resolve catalog address: %w", err)
	}

	dialCtx, dialOpts, err := getDialContextOptions(ctx, getDialContextOptionsOptions{
		Address:     address,
		APIKey:      opts.APIKey,
		Env:         env,
		CredOrg:     opts.Org,
		CredProject: credProject,
	})
	if err != nil {
		return nil, nil, "", fmt.Errorf("cannot get dial context options for catalog: %w", err)
	}
	return dialCtx, dialOpts, address, nil
}

// DialCatalog creates a connection to a asset catalog service.
func DialCatalog(ctx context.Context, opts DialCatalogOptions) (context.Context, *grpc.ClientConn, error) {
	dialCtx, dialOpts, address, err := dialCatalogOptions(ctx, opts)
	if err != nil {
		return nil, nil, err
	}
	conn, err := grpc.NewClient(address, dialOpts...)
	if err != nil {
		return nil, nil, fmt.Errorf("cannot dial catalog: %w", err)
	}
	return dialCtx, conn, nil
}

// ResolveCatalogProjectFromInctl returns the project to use for communicating with a catalog.
func ResolveCatalogProjectFromInctl(flags *cmdutils.CmdFlags) string {
	return ResolveCatalogProject(flags.GetFlagProject())
}

// ResolveCatalogProject returns the project to use for communicating with a catalog.
func ResolveCatalogProject(project string) string {
	if project == "" {
		return defaultCatalogProject
	}
	return project
}

// GetAssetCatalogProject returns the AssetCatalog project that the specified project uses.
func GetAssetCatalogProject(project string) string {
	// Check whether this is an asset project.
	if strings.HasPrefix(project, "intrinsic-assets-") {
		return project
	}

	return environments.AssetsProjectFromProject(project)
}

type resolveCatalogAddressOptions struct {
	Address string
	Env     string
}

func resolveCatalogAddress(opts resolveCatalogAddressOptions) (string, error) {
	// Check for user-provided address.
	if opts.Address != "" {
		return opts.Address, nil
	}

	// TODO(b/410760339): Once the environments package is removed, resolve the
	// catalog domain directly (defaulting to "assets.intrinsic.ai" in external SDK
	// builds and a 3-value --env switch in internal builds).
	env := opts.Env
	if env == "" {
		env = environments.Prod
	}
	domain := environments.AssetsDomain(env)
	if domain == "" {
		return "", fmt.Errorf("unknown environment %q; must be one of %s", env, strings.Join(environments.All, ", "))
	}
	return fmt.Sprintf("dns:///%s:443", domain), nil
}

type getDialContextOptionsOptions struct {
	Address string
	APIKey  string
	// Env is the target environment used for token exchange and per-environment
	// credential lookup. If empty, it is inferred from ClusterProject or CredProject.
	Env string
	// ClusterProject is the project in which the cluster being contacted is running. Used for
	// resolving the environment when Env is empty.
	ClusterProject string
	CredOrg        string
	CredProject    string
}

// getDialContextOptions returns metadata for dialing a gRPC connection to a cloud/on-prem cluster.
//
// It uses the provided ctx to manage the lifecycle of connection created. ctx may be modified, so
// the caller should use the returned context instead.
func getDialContextOptions(ctx context.Context, opts getDialContextOptionsOptions) (context.Context, []grpc.DialOption, error) {
	credProject := opts.CredProject
	if opts.CredOrg != "" {
		info, err := getOrgInfo(opts.CredOrg)
		if err != nil {
			return nil, nil, err
		}
		// If the credential project has not been specified, use the project from the org.
		if credProject == "" {
			credProject = info.Project
		}
		computeProject := info.Project
		// TODO(b/410760339, b/529359429): Remove IsGlobalProject check together with
		// identity.WithComputeProject once b/529359429 is complete and callers no longer
		// pass global intrinsic-assets-* project IDs in CredProject.
		if computeProject == "" && opts.CredProject != "" && !environments.IsGlobalProject(opts.CredProject) {
			computeProject = opts.CredProject
		}
		identityOpts := []identity.Option{identity.WithOptionalOrg(info.Organization)}
		// TODO(b/529359429): Remove identity.WithComputeProject option use.
		if computeProject != "" {
			identityOpts = append(identityOpts, identity.WithComputeProject(computeProject))
		}
		ctx, err = identity.AppendToOutgoingContext(ctx, identityOpts...)
		if err != nil {
			return nil, nil, err
		}
	}

	// TODO(b/410760339, b/530456416): Once inctl auth login persists Environment in
	// OrgInfo (and the local project->env index), read env from OrgInfo/index instead of
	// deriving it via environments.FromAnyProject.
	resolvedEnv := opts.Env
	if resolvedEnv == "" {
		switch {
		case opts.ClusterProject != "":
			resolvedEnv = environments.FromAnyProject(opts.ClusterProject)
		case credProject != "":
			resolvedEnv = environments.FromAnyProject(credProject)
		default:
			resolvedEnv = environments.Prod
		}
	}

	dialOpts := baseclientutils.BaseDialOptions()

	creds, err := getPerRPCCredentials(getPerRPCCredentialsOptions{
		Address:     opts.Address,
		APIKey:      opts.APIKey,
		Env:         resolvedEnv,
		CredProject: credProject,
	})
	if err != nil {
		return nil, nil, err
	}
	if creds != nil {
		dialOpts = append(dialOpts, grpc.WithPerRPCCredentials(creds))
	}

	var tcOption grpc.DialOption
	if baseclientutils.UseInsecureCredentials(opts.Address) {
		tcOption = grpc.WithTransportCredentials(insecure.NewCredentials())
	} else {
		var err error
		tcOption, err = baseclientutils.GetTransportCredentialsDialOption()
		if err != nil {
			return nil, nil, fmt.Errorf("cannot get transport credentials: %w", err)
		}
	}
	dialOpts = append(dialOpts, tcOption)

	return ctx, dialOpts, nil
}

func getOrgInfo(org string) (auth.OrgInfo, error) {
	// Try first to parse the org and project directly from the org string, so we don't read the
	// org info from disk unless we need to.
	orgAndProject := strings.Split(org, "@")
	if len(orgAndProject) == 2 {
		return auth.OrgInfo{
			Organization: orgAndProject[0],
			Project:      orgAndProject[1],
		}, nil
	}

	info, err := auth.NewStore().ReadOrgInfo(org)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return auth.OrgInfo{Organization: org}, nil
		}
		return auth.OrgInfo{}, err
	}
	return info, nil
}

type getPerRPCCredentialsOptions struct {
	Address     string
	APIKey      string
	Env         string
	CredProject string
}

func getPerRPCCredentials(opts getPerRPCCredentialsOptions) (credentials.PerRPCCredentials, error) {
	addressIsInsecure := baseclientutils.UseInsecureCredentials(opts.Address) || baseclientutils.IsLocalAddress(opts.Address)

	apiKey := opts.APIKey
	if apiKey == "" {
		store := auth.NewStore()
		cfg, err := store.GetEnvConfiguration(opts.Env)
		// TODO(b/530456416): Remove fallback to per-project configuration (and
		// environments.AssetsProject) once all credentials live in the per-environment store.
		if errors.Is(err, fs.ErrNotExist) {
			fallbackProject := opts.CredProject
			if fallbackProject == "" {
				fallbackProject = environments.AssetsProject(opts.Env)
			}
			if fallbackProject != "" {
				cfg, err = store.GetProjectConfiguration(fallbackProject)
			}
		}
		if err != nil {
			if opts.CredProject == "" && errors.Is(err, fs.ErrNotExist) {
				cfg = nil
			} else {
				return nil, err
			}
		}
		if cfg != nil {
			projectToken, err := cfg.GetDefaultCredentials()
			if err != nil {
				return nil, err
			}
			apiKey = projectToken.APIKey
		}
	}

	if apiKey != "" {
		var tokenOpts []auth.APIKeyTokenSourceOption
		if addressIsInsecure {
			tokenOpts = append(tokenOpts, auth.WithAllowInsecure())
		}
		return auth.NewEnvTokenCredentials(opts.Env, apiKey, tokenOpts...)
	}

	if addressIsInsecure {
		return nil, nil
	}

	return nil, errNoInfoToAuthenticate
}

type dialInfoParams struct {
	Address   string // The address of a cloud/on-prem cluster
	Cluster   string // The name of the server to install to
	CredOrg   string // Optional the org-id header to set
	CredToken string // Optional the credential value itself. This bypasses the store
	Project   string // The current GCP project
}

func dialConnectionCtx(ctx context.Context, params dialInfoParams) (context.Context, *grpc.ClientConn, string, error) {
	address, err := resolveClusterAddress(params.Address, params.Project)
	if err != nil {
		return ctx, nil, "", err
	}

	ctx, dialOpts, err := getDialContextOptions(ctx, getDialContextOptionsOptions{
		Address:        address,
		APIKey:         params.CredToken,
		ClusterProject: params.Project,
		CredOrg:        params.CredOrg,
		CredProject:    params.Project,
	})
	if err != nil {
		return ctx, nil, "", fmt.Errorf("cannot get dial context options for cluster: %w", err)
	}

	if params.Cluster != "" {
		ctx = metadata.AppendToOutgoingContext(ctx, "x-server-name", params.Cluster)
		dialOpts = append(dialOpts, grpc.WithUnaryInterceptor(remoteAccessErrorContextInterceptor()))
	}


	// Add a stats handler to connect traces between client and server.
	dialOpts = append(dialOpts, grpc.WithStatsHandler(&ocgrpc.ClientHandler{}))


	conn, err := grpc.NewClient(address, dialOpts...)
	if err != nil {
		return ctx, nil, "", fmt.Errorf("could not dial context: %w", err)
	}

	return ctx, conn, address, nil
}

// remoteAccessErrorContextInterceptor returns a unary gRPC interceptor that
// wraps PermissionDenied errors with a user-friendly message that describes
// the potential for disabled remote access.
func remoteAccessErrorContextInterceptor() grpc.UnaryClientInterceptor {
	return func(ctx context.Context, method string, req, reply any, cc *grpc.ClientConn, invoker grpc.UnaryInvoker, opts ...grpc.CallOption) error {
		err := invoker(ctx, method, req, reply, cc, opts...)
		if err != nil {
			st, ok := status.FromError(err)
			if ok && st.Code() == codes.PermissionDenied {
				return status.Errorf(codes.PermissionDenied, "%s\nRemote access might be disabled. Please contact your IPC operator for assistance.", st.Message())
			}
		}
		return err
	}
}

func resolveClusterAddress(address string, project string) (string, error) {
	if address != "" {
		return address, nil
	}

	if project == "" {
		return "", fmt.Errorf("project is required if no address is specified")
	}

	return fmt.Sprintf("dns:///www.endpoints.%s.cloud.goog:443", project), nil
}

// getClusterNameFromSolution returns the cluster in which a solution currently runs.
func getClusterNameFromSolution(ctx context.Context, conn *grpc.ClientConn, solutionName string) (string, error) {
	solution, err := getSolution(ctx, conn, solutionName)
	if err != nil {
		return "", fmt.Errorf("failed to get solution: %w", err)
	}
	if solution.GetState() == clusterdiscoverypb.SolutionState_SOLUTION_STATE_NOT_RUNNING {
		return "", fmt.Errorf("solution is not running")
	}
	if solution.GetClusterName() == "" {
		return "", fmt.Errorf("unknown error: solution is running but cluster is empty")
	}
	return solution.GetClusterName(), nil
}

// getSolution gets solution data by name
func getSolution(ctx context.Context, conn *grpc.ClientConn, solutionName string) (*solutiondiscoverypb.SolutionDescription, error) {
	client := solutiondiscoverygrpcpb.NewSolutionDiscoveryServiceClient(conn)
	req := &solutiondiscoverypb.GetSolutionDescriptionRequest{Name: solutionName}
	resp, err := client.GetSolutionDescription(ctx, req)
	if err != nil {
		return nil, fmt.Errorf("failed to get solution description: %w", err)
	}

	return resp.GetSolution(), nil
}
