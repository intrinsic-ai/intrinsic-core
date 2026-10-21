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
	"google.golang.org/protobuf/encoding/prototext"
)

func dumpConfig(ctx context.Context) error {
	ctx, client, err := makeIconClient(ctx)
	if err != nil {
		return err
	}
	defer client.Close()

	partConfigs, serverConfig, err := client.Config(ctx)
	if err != nil {
		return fmt.Errorf("failed to get config: %w", err)
	}

	options := prototext.MarshalOptions{
		Multiline: true,
		Indent:    "  ",
	}

	fmt.Println("Server Config:")
	serverOut, err := options.Marshal(serverConfig)
	if err != nil {
		return fmt.Errorf("failed to marshal server config: %w", err)
	}
	fmt.Println(string(serverOut))

	fmt.Println("\nPart Configs:")
	for _, pc := range partConfigs {
		partOut, err := options.Marshal(pc)
		if err != nil {
			return fmt.Errorf("failed to marshal part config for %s: %w", pc.GetName(), err)
		}
		fmt.Println(string(partOut))
	}

	return nil
}

var iconConfigCmd = &cobra.Command{
	Use:   "config",
	Short: "Dump the static configuration of the ICON server and its parts",
	RunE: func(cmd *cobra.Command, args []string) error {
		return dumpConfig(cmd.Context())
	},
}

func init() {
	iconCmd.AddCommand(iconConfigCmd)
}
