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

package icon

import (
	"context"
	"fmt"

	typespb "intrinsic/icon/proto/v1/types_go_proto"

	"github.com/spf13/cobra"
)

func listActions(ctx context.Context, verbose bool) error {
	ctx, client, err := makeIconClient(ctx)
	if err != nil {
		return err
	}
	defer client.Close()

	actions, err := client.ActionSignatures(ctx)
	if err != nil {
		return err
	}

	for _, action := range actions {
		fmt.Println(action.ActionTypeName)
		if verbose {
			fmt.Printf("\t %s\n", action.GetTextDescription())
			var supportedOverrides []*typespb.ActionSignature_BehaviorOverrideInfo
			for _, info := range action.GetBehaviorOverrideInfos() {
				if info.GetOverrideRequest() != typespb.BehaviorOverrideRequest_BEHAVIOR_OVERRIDE_REQUEST_UNKNOWN {
					supportedOverrides = append(supportedOverrides, info)
				}
			}
			if len(supportedOverrides) > 0 {
				fmt.Printf("\t Supported Behavior Overrides:\n")
				for _, info := range supportedOverrides {
					if desc := info.GetTextDescription(); desc != "" {
						fmt.Printf("\t\t - %s: %s\n", info.GetOverrideRequest().String(), desc)
					} else {
						fmt.Printf("\t\t - %s\n", info.GetOverrideRequest().String())
					}
				}
			}
		}
	}

	return nil
}

var iconListActionsCmd = &cobra.Command{
	Use:   "list-actions",
	Short: "Print a list of available actions",
	RunE: func(cmd *cobra.Command, args []string) error {
		return listActions(cmd.Context(), flagVerbose)
	},
}

func init() {
	iconListActionsCmd.Flags().BoolVar(&flagVerbose, "verbose", false,
		"print a details to the list of actions")
	iconCmd.AddCommand(iconListActionsCmd)
}
