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
	"fmt"

	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
)

const (
	keyRevokeAll = "all"
)

var revokeCmdFlags = cmdutils.NewCmdFlags()

var revokeCmd = &cobra.Command{
	Use:     "revoke",
	Aliases: []string{"ls"},
	Short:   "Removes local credentials",
	Long:    "Remove selected local credentials. Credentials are currently not revoked on server.",
	Args:    cobra.NoArgs,
	RunE:    revokeCredentialsE,
}

func revokeCredentialsE(cmd *cobra.Command, _ []string) error {
	isRevokeAll := revokeCmdFlags.GetBool(keyRevokeAll)
	credName, isOrg := getConfigurationName()
	if !isRevokeAll && credName == "" {
		return fmt.Errorf("either --%s or --%s needs to be specified", orgutil.KeyOrganization, keyRevokeAll)
	}

	isBatch := revokeCmdFlags.GetBool(keyBatch)

	rw := newReadWriterForCmd(cmd)
	if credName == "" && isRevokeAll {
		if !isBatch {
			resp, err := userPrompt(rw, "Are you sure you want to remove all authorizations?", 1, "yes", "NO")
			if err != nil {
				// this error means something terrible happened with terminal, aborting is really only option
				return fmt.Errorf("cannot continue: %w", err)
			}
			if resp != "yes" {
				return fmt.Errorf("aborted by user")
			}
		}
		return authStore.RemoveAllKnownCredentials()
	}
	if !isBatch {
		prompt := fmt.Sprintf("Are you sure you want to revoke all credentials for '%s'", credName)
		resp, err := userPrompt(rw, prompt, 1, "yes", "NO")
		if err != nil {
			return err
		} else if resp != "yes" {
			return fmt.Errorf("aborted by user")
		}
	}
	if isOrg {
		return authStore.RemoveOrganization(credName)
	}
	return authStore.RemoveConfiguration(credName)
}

func getConfigurationName() (name string, isOrg bool) {
	if orgName := revokeCmdFlags.GetFlagOrganization(); orgName != "" {
		return orgName, true
	}
	if projectName := revokeCmdFlags.GetFlagProject(); projectName != "" {
		return projectName, false
	}
	return "", false
}

func init() {
	AuthCmd.AddCommand(revokeCmd)

	revokeCmdFlags.SetCommand(revokeCmd)
	revokeCmdFlags.AddFlagsProjectOrgOptional()
	revokeCmdFlags.MarkHidden(orgutil.KeyProject)
	revokeCmdFlags.OptionalBool(keyRevokeAll, false, fmt.Sprintf("Revokes all existing credentials. If --%s is omitted, removes all known credentials", orgutil.KeyOrganization))
	revokeCmdFlags.OptionalBool(keyBatch, false, "Suppresses command prompts and assume Yes or default as an answer. Use with shell scripts.")
}
