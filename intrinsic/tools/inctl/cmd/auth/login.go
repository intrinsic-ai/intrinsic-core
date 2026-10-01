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

package auth

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/url"
	"os"
	"os/exec"
	"slices"
	"strings"

	envs "intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/viperutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	accdiscoverv1grpcpb "intrinsic/kubernetes/accounts/service/api/v1/discoveryapi_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

const (
	keyNoBrowser             = "no_browser"
	keyInternalMockDiscovery = "internal-mock-discovery"

	// We are going to use system defaults to ensure we open web-url correctly.
	// For dev container running via VS Code the sensible-browser redirects
	// call into code client from server to ensure URL is opened in valid
	// client browser.
	sensibleBrowser = "/usr/bin/sensible-browser"

	// placeholderorg is used when a user didn't provide an org.
	// It was introduced to make it possible to run inctl auth login
	// without the org and project flags, since the server returning
	// an API key doesn't actually require an org or a project.
	placeholderOrg = "-"
)

// Exposed for testing
var (
	discoverOrgsFunc = discoverOrganizations
)

var (
	loginParams = viper.New()
	loginCmd    = orgutil.WrapCmdOptional(
		&cobra.Command{
			Use:   "login",
			Short: "Logs in user into Flowstate",
			Long:  "Logs in user into Flowstate to allow interactions with solutions.",
			Args:  cobra.NoArgs,
			RunE:  loginCmdE,
		},
		loginParams,
		orgutil.WithOrgExistsCheck(func() bool {
			// The login command only creates the org, so we must disable the flag check which happens before.
			return false
		}),
	)
)

func readAPIKeyFromPipe(reader *bufio.Reader) (string, error) {
	fi, _ := os.Stdin.Stat()
	// Check if input comes from pipe. Taken from
	// https://www.socketloop.com/tutorials/golang-check-if-os-stdin-input-data-is-piped-or-from-terminal
	if (fi.Mode() & os.ModeCharDevice) == 0 {
		bytes, _, err := reader.ReadLine()
		if err != nil {
			return "", err
		}

		return strings.TrimSpace(string(bytes)), nil
	}
	return "", nil
}

func queryForAPIKey(ctx context.Context, writer io.Writer, in *bufio.Reader, organization, project, env string) (string, error) {

	// for assets and accounts `inctl accounts generate-api-key` is required
	if envs.IsGlobalProject(project) {
		return "", fmt.Errorf("Project %q not supported for `inctl auth login`. Use `inctl accounts tokens generate-api-key --org %q` instead.",
			project, organization) // organization is already in format "orgid@project" here
	}


	portal := envs.PortalDomain(env)
	if portal == "" {
		return "", fmt.Errorf("unknown environment %q", env)
	}
	targetOrg := placeholderOrg
	if organization != "" {
		targetOrg = url.PathEscape(organization)
	}

	authorizationURL := fmt.Sprintf("https://%s/o/%s/generate-keys", portal, targetOrg)
	fmt.Fprintf(writer, "Open URL in your browser to obtain authorization token: %s\n", authorizationURL)

	ignoreBrowser := loginParams.GetBool(keyNoBrowser)
	if !ignoreBrowser {
		_, _ = fmt.Fprintln(writer, "Attempting to open URL in your browser...")
		browser := exec.CommandContext(ctx, sensibleBrowser, authorizationURL)
		browser.Stdout = io.Discard
		browser.Stderr = io.Discard
		if err := browser.Start(); err != nil {
			fmt.Fprintf(writer, "Failed to open URL in your browser, please run command again with '--%s'.\n", keyNoBrowser)
			return "", fmt.Errorf("rerun with '--%s', got error %w", keyNoBrowser, err)
		}
	}
	fmt.Fprintf(writer, "\nPaste access token from website: ")

	apiKey, err := in.ReadString('\n')
	if err != nil {
		return "", fmt.Errorf("cannot read from input device: %w", err)
	}

	// Move the cursor back to the beginning of the line and clear the line
	fmt.Fprintf(writer, "\033[1A\033[2K")
	// Overwrite the line with a placeholder
	fmt.Fprintf(writer, "Paste access token from website: [redacted]\n")

	return strings.TrimSpace(apiKey), nil
}

func loadMockDiscovery(mockPath string) (map[string][]string, error) {
	data, err := os.ReadFile(mockPath)
	if err != nil {
		return nil, fmt.Errorf("read mock discovery file: %w", err)
	}
	var mockResp map[string][]string
	if err := json.Unmarshal(data, &mockResp); err != nil {
		return nil, fmt.Errorf("unmarshal mock discovery response: %w", err)
	}
	return mockResp, nil
}

// discoverOrganizations discovers orgs and projects the given API key has access to.
func discoverOrganizations(ctx context.Context, apiKey, env string) (map[string][]string, error) {
	accProject := envs.AccountsProjectFromEnv(env)
	conn, err := auth.NewCloudConnection(ctx, auth.WithProject(accProject), auth.WithAPIKey(apiKey), auth.WithEnv(env))
	if err != nil {
		return nil, err
	}
	defer conn.Close()

	client := accdiscoverv1grpcpb.NewAccountsDiscoveryServiceClient(conn)
	resp, err := client.ListOrganizations(ctx, &emptypb.Empty{})
	if err != nil {
		fmt.Println("Could not find the project for this token. Please restart the login process and make sure to provide the exact key shown by the portal.")
		return nil, fmt.Errorf("failed to list organizations: %w", err)
	}

	orgToProjects := make(map[string][]string)

	for _, o := range resp.GetOrganizations() {
		orgToProjects[o.GetName()] = append(orgToProjects[o.GetName()], o.GetProject())
	}

	return orgToProjects, nil
}

func loginCmdE(cmd *cobra.Command, _ []string) (err error) {
	if err := agents.Check(cmd); err != nil {
		return err
	}
	writer := cmd.OutOrStdout()
	projectName := loginParams.GetString(orgutil.KeyProject)
	orgName := loginParams.GetString(orgutil.KeyOrganization)
	org := orgutil.QualifiedOrg(projectName, orgName)
	in := bufio.NewReader(cmd.InOrStdin())
	isBatch := loginParams.GetBool(keyBatch)

	env := loginParams.GetString(orgutil.KeyEnvironment)
	if env == "" {
		env = envs.FromComputeProject(projectName)
	}

	apiKey, err := readAPIKeyFromPipe(in)
	if err != nil {
		return err
	}

	if apiKey == "" && !isBatch {
		apiKey, err = queryForAPIKey(cmd.Context(), writer, in, org, projectName, env)
		if err != nil {
			return err
		}
	}

	if apiKey == "" {
		return fmt.Errorf("API key is empty. Please provide an API key")
	}

	var orgToProjects map[string][]string
	if mockPath := loginParams.GetString(keyInternalMockDiscovery); mockPath != "" {
		orgToProjects, err = loadMockDiscovery(mockPath)
	} else {
		orgToProjects, err = discoverOrgsFunc(cmd.Context(), apiKey, env)
	}
	if err != nil {
		return fmt.Errorf("query project: %w", err)
	}

	if len(orgToProjects) == 0 {
		fmt.Fprintln(writer, "\nWarning: No organizations were found associated with this API key")
	} else if (orgName != "" || projectName != "") && !hasRequestedAccess(orgToProjects, orgName, projectName) {
		requested := orgName
		if requested == "" {
			requested = projectName
		} else if projectName != "" {
			requested += "@" + projectName
		}
		fmt.Fprintf(writer, "\nWarning: The requested target %q was not found among the organizations associated with your API key. Please double check for typos", requested)
		fmt.Fprintln(writer)
	}

	if err := writeOrganizations(orgToProjects, apiKey); err != nil {
		return fmt.Errorf("write organizations: %w", err)
	}

	if err := reconcileInvalidOrgConfigs(writer, authStore, orgName, orgToProjects); err != nil {
		return fmt.Errorf("reconcile invalid org configs: %w", err)
	}

	if err := authStore.UpsertEnvConfig(env, auth.AliasDefaultToken, apiKey); err != nil {
		return fmt.Errorf("error upserting env config: %w", err)
	}

	if len(orgToProjects) > 0 {
		fmt.Fprintln(writer, "Successfully logged in!")

		var targets []string
		for o, ps := range orgToProjects {
			if len(ps) == 1 {
				targets = append(targets, o)
			} else {
				for _, p := range ps {
					targets = append(targets, fmt.Sprintf("%s@%s", o, p))
				}
			}
		}
		// most of the users have access to just one org-project, so it doesn't make sense
		// to repeat the target here when it matches the requested --org.
		if len(targets) == 1 && targets[0] == orgName && hasRequestedAccess(orgToProjects, orgName, projectName) {
			return nil
		}
		slices.Sort(targets)

		fmt.Fprintln(writer, "Wrote credentials for the following targets:")
		for _, t := range targets {
			fmt.Fprintf(writer, "  - %s\n", t)
		}
	}
	return nil
}

// hasRequestedAccess checks if the org/project given by a user match the data on the backend.
// it's used to print a warning if user-supplied arguments to login don't match the discovered
// orgs.
func hasRequestedAccess(orgToProjects map[string][]string, org, project string) bool {
	switch {
	case org != "" && project != "":
		// Case 1: Both org and project specified (qualified org)
		return slices.Contains(orgToProjects[org], project)
	case org != "":
		// Case 2: Only org specified
		return len(orgToProjects[org]) > 0
	case project != "":
		// Case 3: Only project specified
		for _, ps := range orgToProjects {
			if slices.Contains(ps, project) {
				return true
			}
		}
	}
	return false
}

// writeOrganizations saves org to project mapping to the local auth storage.
func writeOrganizations(orgToProjects map[string][]string, apiKey string) error {
	for o, ps := range orgToProjects {
		for _, p := range ps {
			// Always write the fully-qualified name: org@project.json
			if err := authStore.WriteOrgInfo(&auth.OrgInfo{
				Organization: o + "@" + p,
				Project:      p,
			}); err != nil {
				return fmt.Errorf("write org info: %w", err)
			}
			// For single-project orgs, also write the short-name alias: org.json
			if len(ps) == 1 {
				if err := authStore.WriteOrgInfo(&auth.OrgInfo{
					Organization: o,
					Project:      p,
				}); err != nil {
					return fmt.Errorf("write org info: %w", err)
				}
			}
			if err := authStore.UpsertProjectConfig(p, auth.AliasDefaultToken, apiKey); err != nil {
				return fmt.Errorf("error upserting project config: %w", err)
			}
		}
	}
	return nil
}

// reconcileInvalidOrgConfigs cleans up stale or ambiguous organization configurations.
func reconcileInvalidOrgConfigs(writer io.Writer, store *auth.Store, orgName string, orgToProjects map[string][]string) error {
	hasRemoved := false
	// 1. Clean up stale config for discovered orgs
	for o, ps := range orgToProjects {
		if len(ps) > 1 {
			// Multi-project org. Delete legacy short-name config if it is stale (points to a project they lost access to).
			orgInfo, err := store.ReadOrgInfo(o)
			if err != nil {
				// the file either doesn't exist, unreadable or corrupted
				_ = store.RemoveOrgInfo(o)
				continue
			}

			if slices.Contains(ps, orgInfo.Project) {
				continue
			}

			if err := store.RemoveOrgInfo(o); err != nil {
				return fmt.Errorf("remove stale org config %q: %w", o, err)
			}
			if !hasRemoved {
				fmt.Fprintln(writer, "\nCleaned up configurations:")
				hasRemoved = true
			}
			fmt.Fprintf(writer, "  - Removed stale organization default %q (was pointing to %q)\n", o, orgInfo.Project)
		}
	}

	// 2. Clean up requested org if it was not discovered at all
	if orgName != "" {
		if _, discovered := orgToProjects[orgName]; !discovered {
			// Requested org was not found. Delete its single-project config if it exists.
			if err := store.RemoveOrgInfo(orgName); err != nil {
				return fmt.Errorf("remove stale requested org config %q: %w", orgName, err)
			}
			if !hasRemoved {
				fmt.Fprintln(writer, "\nCleaned up configurations:")
				hasRemoved = true
			}
			fmt.Fprintf(writer, "  - Removed stale requested organization configuration %q\n", orgName)
		}
	}
	if hasRemoved {
		fmt.Fprintln(writer) // trailing newline
	}
	return nil
}

func init() {
	AuthCmd.AddCommand(loginCmd)

	flags := loginCmd.Flags()
	// we will use viper to fetch data, we do not need local variables
	flags.Bool(keyNoBrowser, false, "Disables attempt to open login URL in browser automatically")
	flags.Bool(keyBatch, false, "Suppresses command prompts and assume Yes or default as an answer. Use with shell scripts.")
	flags.String(keyInternalMockDiscovery, "", "Internal use only: path to JSON file containing mock discovery response")

	flags.MarkHidden(orgutil.KeyProject)
	flags.MarkHidden(keyInternalMockDiscovery)

	viperutil.BindFlags(loginParams, flags, nil)
}
