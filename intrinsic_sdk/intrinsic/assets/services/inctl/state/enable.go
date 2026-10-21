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

// Package enable provides a command to enable a running service asset in a solution.
package enable

import (
	"fmt"
	"os"
	"os/signal"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"

	"github.com/spf13/cobra"

	systemservicestategrpcpb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
	systemservicestatepb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
)

// Command returns a command to enable a running service asset in a solution.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "enable <name>",
		Short: "Enable a running Service instance in a solution.",
		Long:  `Enable a running Service instance in a solution.`,
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, stop := signal.NotifyContext(cmd.Context(), os.Interrupt)
			defer stop()

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("could not create connection to cluster: %w", err)
			}
			defer conn.Close()

			client := systemservicestategrpcpb.NewSystemServiceStateClient(conn)
			name := args[0]
			if _, err := client.EnableService(ctx, &systemservicestatepb.EnableServiceRequest{
				Name: name,
			}); err != nil {
				return fmt.Errorf("could not enable service: %w", err)
			}

			fmt.Printf("Service %q has been enabled.\n", name)

			return nil
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	return cmd
}
