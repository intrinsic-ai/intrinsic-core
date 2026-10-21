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

	"github.com/spf13/cobra"
)

func clearFaults(ctx context.Context) error {
	ctx, client, err := makeIconClient(ctx)
	if err != nil {
		return err
	}
	defer client.Close()

	if err := client.ClearFaults(ctx); err != nil {
		return fmt.Errorf("failed to clear faults: %w", err)
	}

	fmt.Println("Faults cleared successfully.")
	return nil
}

var iconClearFaultsCmd = &cobra.Command{
	Use:   "clear-faults",
	Short: "Clear all faults on the ICON server",
	RunE: func(cmd *cobra.Command, args []string) error {
		return clearFaults(cmd.Context())
	},
}

func init() {
	iconCmd.AddCommand(iconClearFaultsCmd)
}
