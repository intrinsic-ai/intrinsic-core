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
	"regexp"
	"strings"

	"intrinsic/assets/baseclientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/config/environments"
	"intrinsic/kubernetes/acl/identity"
	"intrinsic/kubernetes/config/crcconfig" 
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/gcpauth" 
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
	catalogAssetAddressRegex = regexp.MustCompile(`(^|/)assets[-]?([^\.]*)\.intrinsic\.ai`)

	assetAddressToProjectSuffix = map[string]string{
		"":    "prod",
		"dev": "dev",
		"qa":  "staging",
	}

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
		Org:     flags.GetFlagOrganization(),
		Project: flags.GetFlagProject(),
	})
}

// DialCatalogOptions specifies the options for DialCatalog.
type DialCatalogOptions struct {
	Address string
	APIKey  string
	Org     string
	Project string // Defaults to the global assets project.
}

func dialCatalogOptions(ctx context.Context, opts DialCatalogOptions) (context.Context, []grpc.DialOption, string, error) {
	catalogProject := ResolveCatalogProject(opts.Project)
	// Get the catalog address.
	address, err := resolveCatalogAddress(ctx, resolveCatalogAddressOptions{
		Address: opts.Address,
		Project: catalogProject,
	})
	if err != nil {
		return nil, nil, "", fmt.Errorf("cannot resolve catalog address: %w", err)
	}

	optsOpts := getDialContextOptionsOptions{
		Address:        address,
		APIKey:         opts.APIKey,
		ClusterProject: catalogProject,
		CredOrg:        opts.Org,
	}
	dialCtx, dialOpts, err := getDialContextOptions(ctx, optsOpts)
	if err != nil {
		// If we don't have enough info to authenticate, try one more time using the catalog project
		// as the credential project. (This supports mainly legacy usage where the project is specified
		// but not the org, and the user can authenticate in the catalog project.)
		if errors.Is(err, errNoInfoToAuthenticate) {
			optsOpts.CredProject = catalogProject
			var catalogProjectErr error
			if dialCtx, dialOpts, catalogProjectErr = getDialContextOptions(ctx, optsOpts); catalogProjectErr == nil {
				err = nil
			}
		}
		if err != nil {
			return nil, nil, "", fmt.Errorf("cannot get dial context options for catalog: %w", err)
		}
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
func GetAssetCatalogProject(project string) (string, error) {
	// Check whether this is an asset project.
	if strings.HasPrefix(project, "intrinsic-assets-") {
		return project, nil
	}

	// No custom address and not an asset project, so the project is using the prod asset project.
	return defaultCatalogProject, nil
}

type resolveCatalogAddressOptions struct {
	Address string
	Project string
}

func resolveCatalogAddress(ctx context.Context, opts resolveCatalogAddressOptions) (string, error) {
	// Check for user-provided address.
	if opts.Address != "" {
		return opts.Address, nil
	}

	// Derive the address from the project.
	if opts.Project == "" {
		return "", fmt.Errorf("cannot determine catalog address when project is empty")
	}
	address, err := getCatalogAddressForProject(ctx, opts)
	if err != nil {
		return "", err
	}

	addDNS := true
	if addDNS && !strings.HasPrefix(address, "dns:///") {
		address = fmt.Sprintf("dns:///%s", address)
	}

	return address, nil
}

func resolveTokenExchangeAddress(ctx context.Context, project string) (string, error) {
	var environment string
	var err error
	if environment, err = environments.FromProject(project); err != nil {
		environment = environments.FromComputeProject(project)
	}

	return environments.PortalDomain(environment), nil
}

func defaultGetCatalogAddressForProject(ctx context.Context, opts resolveCatalogAddressOptions) (address string, err error) {

	if clientOptions, err := gcpauth.ClientHTTPOptions(ctx); err != nil {
		return "", errors.Wrap(err, "get grpc options")
	} else if crcConfig, err := crcconfig.New(ctx, opts.Project, clientOptions...); err != nil {
		return "", errors.Wrap(err, "get cloud robotics config")
	} else if crcDomain, ok := crcConfig["CLOUD_ROBOTICS_DOMAIN"]; ok && crcDomain != "" {
		address = fmt.Sprintf("%s:443", crcDomain)
	} else {
		address = fmt.Sprintf("www.endpoints.%s.cloud.goog:443", opts.Project)
	}
	// TODO(b/312679307): Support nicer scrubbing options outside google3.






	return address, nil
}

var getCatalogAddressForProject = defaultGetCatalogAddressForProject

type getDialContextOptionsOptions struct {
	Address string
	APIKey  string
	// ClusterProject is the project in which the cluster being contacted is running. Used for
	// resolving the address that the cluster will use to contact the token exchange service.
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
		identityOpts := []identity.Option{identity.WithOrg(info.Organization)}
		// TODO(b/529359429): Remove identity.WithComputeProject option use.
		if info.Project != "" {
			identityOpts = append(identityOpts, identity.WithComputeProject(info.Project))
		}
		ctx, err = identity.AppendToOutgoingContext(ctx, identityOpts...)
		if err != nil {
			return nil, nil, err
		}
	}

	dialOpts := baseclientutils.BaseDialOptions()

	creds, err := getPerRPCCredentials(ctx, getPerRPCCredentialsOptions{
		Address:        opts.Address,
		APIKey:         opts.APIKey,
		ClusterProject: opts.ClusterProject,
		CredProject:    credProject,
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
	Address string
	APIKey  string
	// ClusterProject is the project in which the cluster being contacted is running. Used for
	// resolving the address that the cluster will use to contact the token exchange service.
	ClusterProject string
	CredProject    string
}

func getPerRPCCredentials(ctx context.Context, opts getPerRPCCredentialsOptions) (credentials.PerRPCCredentials, error) {
	addressIsInsecure := baseclientutils.UseInsecureCredentials(opts.Address) || baseclientutils.IsLocalAddress(opts.Address)

	var projectToken *auth.ProjectToken
	// User-provided api-key.
	if opts.APIKey != "" {
		projectToken = &auth.ProjectToken{APIKey: opts.APIKey}
	}
	// If not user-provided, load from CredProject
	if projectToken == nil && opts.CredProject != "" {
		configuration, err := auth.NewStore().GetConfiguration(opts.CredProject)
		if err != nil {
			return nil, err
		}

		projectToken, err = configuration.GetDefaultCredentials()
		if err != nil {
			return nil, err
		}
	}
	// if provided or load from credentials, exchange API key for ID token
	if projectToken != nil {
		// Convert the API token to a Google ID token. With this conversion, the JWT is included in the
		// credentials provided by the client (rather than requiring the auth-proxy to do it on
		// ingress.)
		tokenExchangeServiceAddress, err := resolveTokenExchangeAddress(ctx, opts.ClusterProject)
		if err != nil {
			return nil, fmt.Errorf("cannot resolve token exchange service address: %w", err)
		}
		opts := []auth.AsIDTokenCredentialsOption{
			auth.WithTokenExchangeServiceAddress(tokenExchangeServiceAddress),
		}
		if addressIsInsecure {
			opts = append(opts, auth.WithAPIKeyTokenSourceOptions(auth.WithAllowInsecure()))
		}
		return projectToken.AsIDTokenCredentials(opts...)
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
