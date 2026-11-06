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
	"errors"
	"fmt"
	"net/http"
	"os"

	env "intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/viperutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

func init() {
	AuthCmd.AddCommand(printAPIKeyCmd)
	printAPIKeyCmd.Flags().MarkHidden(orgutil.KeyProject)
	printAPIKeyCmd.Flags().MarkHidden(orgutil.KeyEnvironment)
	viperutil.BindFlags(printAPIKeyParams, printAPIKeyCmd.Flags(), viperutil.BindToListEnv(orgutil.KeyEnvironment))

	AuthCmd.AddCommand(printAccessTokenCmd)
	printAccessTokenCmd.Flags().MarkHidden(orgutil.KeyProject)
	printAccessTokenCmd.Flags().MarkHidden(orgutil.KeyEnvironment)
	viperutil.BindFlags(printAccessTokenParams, printAccessTokenCmd.Flags(), viperutil.BindToListEnv(orgutil.KeyEnvironment))
}

func loadCredentials(vipr *viper.Viper) (*auth.ProjectToken, string, error) {
	project := vipr.GetString(orgutil.KeyProject)
	flagEnv := vipr.GetString(orgutil.KeyEnvironment)
	if project == "" && flagEnv == "" {
		return nil, "", fmt.Errorf("expected --%s=<org>", orgutil.KeyOrganization)
	}

	// TODO(b/410760339, b/530456416): Once inctl auth login persists Environment in
	// OrgInfo (and the local project->env index), read env from OrgInfo/index instead of
	// deriving it from project via env.FromComputeProject, and drop the environments import.
	resolvedEnv := flagEnv
	if resolvedEnv == "" {
		resolvedEnv = env.FromComputeProject(project)
	}

	store, err := authStore.GetEnvConfiguration(resolvedEnv)
	// TODO(b/530456416): Remove fallback to per-project configuration.
	if errors.Is(err, os.ErrNotExist) && project != "" {
		store, err = authStore.GetProjectConfiguration(project)
	}
	if err != nil {
		return nil, "", fmt.Errorf("failed to get configuration for project %q: %v", project, err)
	}
	key, err := store.GetDefaultCredentials()
	if err != nil {
		return nil, "", fmt.Errorf("failed to get default API key for project %q: %v", project, err)
	}
	return key, resolvedEnv, nil
}

var printAPIKeyParams = viper.New()

var printAPIKeyCmd = orgutil.WrapCmdOptional(&cobra.Command{
	Use:   "print-api-key",
	Short: "Prints the API key for a project.",
	Long:  "Prints the API key for a project.",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		key, _, err := loadCredentials(printAPIKeyParams)
		if err != nil {
			return err
		}
		fmt.Fprintf(cmd.OutOrStdout(), "%s", key.APIKey)
		return nil
	},
}, printAPIKeyParams, orgutil.WithOrgExistsCheck(func() bool {
	return checkOrgExists && printAPIKeyParams.GetString(orgutil.KeyEnvironment) == ""
}))

var makeHTTPClient = func() *http.Client { // for unit-tests
	return &http.Client{}
}

var printAccessTokenHelp = `
Print an access token.

Can be used to authenticate with the Flowstate API.

Example:

		inctl auth print-access-token --org=myorganization

Example (curl):

		curl -s -X GET -H "Authorization: Bearer $(inctl auth print-access-token --org=myorganization)" https://flowstate.intrinsic.ai/api/v1/cloud-projects-orgs -H 'Content-Type: application/json'
`

var printAccessTokenParams = viper.New()

var printAccessTokenCmd = orgutil.WrapCmdOptional(&cobra.Command{
	Use:   "print-access-token",
	Short: "Print an access token.",
	Long:  printAccessTokenHelp,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		key, e, err := loadCredentials(printAccessTokenParams)
		if err != nil {
			return err
		}
		idToken, err := auth.ExchangeIDToken(cmd.Context(), makeHTTPClient(), e, key.APIKey)
		if err != nil {
			return err
		}
		fmt.Fprintf(cmd.OutOrStdout(), "%s", idToken)
		return nil
	},
}, printAccessTokenParams, orgutil.WithOrgExistsCheck(func() bool {
	return checkOrgExists && printAccessTokenParams.GetString(orgutil.KeyEnvironment) == ""
}))
