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

// Package gcpauth contains utilities for connecting to cloud services.
package gcpauth

import (
	"context"
	"crypto/tls"
	"fmt"
	"os"
	"os/user"
	"path/filepath"

	"intrinsic/tools/inctl/auth/auth"

	log "github.com/golang/glog"
	"github.com/google/go-containerregistry/pkg/v1/google"
	"github.com/google/go-containerregistry/pkg/v1/remote"
	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc"
	"golang.org/x/oauth2"
	googleoauth "golang.org/x/oauth2/google"
	"google.golang.org/api/option"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/oauth"
)

var testExecutorProjects = map[string]bool{
	"giza-workcells":              true,
	"intrinsic-integration-tests": true,
	"intrinsic-staging":           true,
}

// ResetGoogleCredentials sets the local gcloud credentials correctly when run under bazel run/test.
func ResetGoogleCredentials() error {
	usr, err := user.Current()
	if err != nil {
		return errors.Wrap(err, "user.Current()")
	}

	// Explicitly set GOOGLE_APPLICATION_CREDENTIALS for the k8s client to
	// be able to authenticate against GCP.
	// Normally, Kubernetes will look for credentials in
	// $HOME/.config/gcloud/application_default_credentials.json, but $HOME is set
	// differently when run through bazel. Basing the path on $USER still works
	// although it is not clear why as usr.Homedir looks like it should be the same
	// as os.UserHomeDir() (https://golang.org/src/os/user/lookup_stubs.go),
	// but it is not. Also see the TestGoogleApplicationCredsIsNotBazelPath test case.
	credentialsFile := filepath.Join(usr.HomeDir, ".config/gcloud/application_default_credentials.json")
	if _, err := os.Stat(credentialsFile); err == nil {
		os.Setenv("GOOGLE_APPLICATION_CREDENTIALS", credentialsFile)
	}
	return nil
}

// TokenSource creates a suitable token source.
func TokenSource(ctx context.Context, scopes []string) (oauth2.TokenSource, error) {
	ts, err := googleoauth.DefaultTokenSource(ctx)
	if err != nil {
		return nil, err
	}
	return ts, nil
}

// CredentialOption encodes a tristate of authentications we might use.
// If APIKey is set, it will be used on every RPC as Bearer token.
// Otherwise
type CredentialOption struct {
	APIKey string
}

// TokenOption provides grpc.DialOption for the provided credentials.
func TokenOption(ctx context.Context, cred CredentialOption, scopes []string) (credentials.PerRPCCredentials, error) {
	if cred.APIKey != "" {
		return &auth.ProjectToken{APIKey: cred.APIKey}, nil
	}

	ts, err := TokenSource(ctx, scopes)
	if err != nil {
		return nil, err
	}
	return oauth.TokenSource{TokenSource: ts}, nil
}

// ClientHTTPOptions returns the options with a set of credentials for connecting to HTTP based GCP services.
func ClientHTTPOptions(ctx context.Context) ([]option.ClientOption, error) {
	return []option.ClientOption{}, nil
}

// ClientGRPCOptions returns the options with a set of credentials for connecting to gRPC based GCP services.
func ClientGRPCOptions(ctx context.Context, scopes []string) ([]option.ClientOption, error) {
	return []option.ClientOption{}, nil
}

// AuthOption returns the authentication to use with GCP services.
func AuthOption(ctx context.Context) (remote.Option, error) {
	return remote.WithAuthFromKeychain(google.Keychain), nil
}

var (
	// ClientGRPCDialOptions is the helper to configure grpc dial options. The default is suitable for
	// calling cloud grpc services.
	ClientGRPCDialOptions = clientGRPCDialOptions
	// ClientGRPCDial is the helper to actually establish the connection. The default is suitable for
	// calling cloud grpc services.
	ClientGRPCDial = grpcDial
)

// clientGRPCDialOptions returns the options with a set of credentials for connecting to gRPC based GCP services.
func clientGRPCDialOptions(ctx context.Context, cred CredentialOption, addr string, scopes []string) ([]grpc.DialOption, error) {
	t, err := TokenOption(ctx, cred, scopes)
	if err != nil {
		return nil, err
	}
	to := grpc.WithPerRPCCredentials(t)
	cert := grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{}))
	return []grpc.DialOption{to, cert}, nil
}

func grpcDial(ctx context.Context, cred CredentialOption, addr string, opts ...grpc.DialOption) (*grpc.ClientConn, error) {
	return grpc.NewClient(addr, opts...)
}

func tryLoadAPIKey(project string) error {
	cfg, err := auth.NewStore().GetConfiguration(project)
	if err != nil {
		return err
	}
	c, err := cfg.GetDefaultCredentials()
	if err != nil {
		return err
	}
	if err := c.Validate(); err != nil {
		return err
	}
	return nil
}

// AuthForCLICommands returns a gRPC connection to the "cloud-robotics" GKE cluster. It is intended
// to be used for inctl CLI commands that are either ran by humans or the test-executor in the
// Cloud Guitar tests. If no api-key is available it provides a fallback to use the "robot-service"
// account credentials. This is deprecated and we're actively trying to remove it. Any new use is
// not supported.
//
// DEPRECATED: Use auth.NewCloudConnection directly instead. Only use this if you require fallback
// to the robot-service account credentials.
//
// IMPORTANT: This function is not suitable for use on Borg! Use other helper functions from this
// package instead.
func AuthForCLICommands(ctx context.Context, project string) (*grpc.ClientConn, error) {
	loadErr := tryLoadAPIKey(project)
	if loadErr != nil { // failed to load API key
		if !testExecutorProjects[project] {
			return nil, fmt.Errorf("no valid credentials found for project %q and not part of testExecutorProjects: %w", project, loadErr)
		}
		// TODO: b/343342802 - Delete this branch.
		// search logs for: jsonPayload.message:"AuthForCLICommands"
		return defaultTokenSourceFallback(ctx, project)
	}
	fmt.Printf("AuthForCLICommands: Using API-key credentials for project %q\n", project)
	log.InfoContextf(ctx, "AuthForCLICommands: Using API-key credentials for project %q", project)
	return auth.NewCloudConnection(ctx, auth.WithProject(project))
}

// TODO: b/343342802 - Delete this branch.
// search logs for: jsonPayload.message:"AuthForCLICommands"
func defaultTokenSourceFallback(ctx context.Context, project string) (*grpc.ClientConn, error) {
	fmt.Printf("AuthForCLICommands: Falling back to robot-service account credentials for project %q\n", project)
	log.WarningContextf(ctx, "AuthForCLICommands: Falling back to robot-service account credentials for project %q", project)
	ts, err := googleoauth.DefaultTokenSource(ctx)
	if err != nil {
		return nil, fmt.Errorf("unable to retrieve token: %w", err)
	}
	creds := oauth.TokenSource{TokenSource: ts}
	opts := []grpc.DialOption{
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithTransportCredentials(credentials.NewTLS(new(tls.Config))),
		grpc.WithPerRPCCredentials(creds),
	}
	addr := fmt.Sprintf("www.endpoints.%s.cloud.goog:443", project)
	conn, err := grpc.NewClient(addr, opts...)
	if err != nil {
		return nil, fmt.Errorf("failed to establish connection to %q: %w", addr, err)
	}
	return conn, nil
}
