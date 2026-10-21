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
	"sort"

	"github.com/spf13/cobra"
)

func showStatus(ctx context.Context) error {
	ctx, client, err := makeIconClient(ctx)
	if err != nil {
		return err
	}
	defer client.Close()

	opStatus, err := client.OperationalStatus(ctx)
	if err != nil {
		return fmt.Errorf("failed to get operational status: %w", err)
	}

	fmt.Printf("Operational Status: %s\n", opStatus.GetState())
	if opStatus.GetFaultReason() != "" {
		fmt.Printf("Fault Reason:      %s\n", opStatus.GetFaultReason())
	}

	status, err := client.Status(ctx)
	if err != nil {
		return fmt.Errorf("failed to get detailed status: %w", err)
	}

	if len(status.GetPartStatus()) > 0 {
		fmt.Println("\nPart Statuses:")
		// Sort part names for consistent output
		var partNames []string
		for name := range status.GetPartStatus() {
			partNames = append(partNames, name)
		}
		sort.Strings(partNames)

		for _, name := range partNames {
			partStatus := status.GetPartStatus()[name]
			fmt.Printf("  %s:\n", name)
			fmt.Printf("    State: %s\n", partStatus.GetOperationalStatus().GetState())
			if partStatus.GetOperationalStatus().GetFaultReason() != "" {
				fmt.Printf("    Fault: %s\n", partStatus.GetOperationalStatus().GetFaultReason())
			}
		}
	}

	if status.SafetyStatus != nil {
		fmt.Println("\nSafety Status:")
		fmt.Printf("  Mode of Safe Operation: %s\n", status.SafetyStatus.GetModeOfSafeOperation())
		fmt.Printf("  E-Stop Button Status:   %s\n", status.SafetyStatus.GetEstopButtonStatus())
		fmt.Printf("  Enable Button Status:   %s\n", status.SafetyStatus.GetEnableButtonStatus())
		fmt.Printf("  Requested Behavior:     %s\n", status.SafetyStatus.GetRequestedBehavior())
	}

	return nil
}

var iconStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "Show the current status of the ICON server and its parts",
	RunE: func(cmd *cobra.Command, args []string) error {
		return showStatus(cmd.Context())
	},
}

func init() {
	iconCmd.AddCommand(iconStatusCmd)
}
