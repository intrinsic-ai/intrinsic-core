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

	"intrinsic/icon/go/icon"

	"github.com/spf13/cobra"
)

var flagOperationalOnly bool

func disableIcon(ctx context.Context) error {
	ctx, client, err := makeIconClient(ctx)
	if err != nil {
		return err
	}
	defer client.Close()

	group := icon.AllHardware
	if flagOperationalOnly {
		group = icon.OperationalHardwareOnly
	}

	if err := client.Disable(ctx, group); err != nil {
		return fmt.Errorf("failed to disable ICON: %w", err)
	}

	fmt.Println("ICON disabled successfully.")
	return nil
}

var iconDisableCmd = &cobra.Command{
	Use:   "disable",
	Short: "Disable all parts on the ICON server",
	RunE: func(cmd *cobra.Command, args []string) error {
		return disableIcon(cmd.Context())
	},
}

func init() {
	iconDisableCmd.Flags().BoolVar(&flagOperationalOnly, "operational-only", false, "Disable only operational hardware modules")
	iconCmd.AddCommand(iconDisableCmd)
}
