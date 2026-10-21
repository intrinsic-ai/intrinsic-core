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

// Package restart provides a command to restart a running service asset in a solution.
package restart

import (
	"bufio"
	"fmt"
	"os"
	"os/signal"
	"strings"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"

	"github.com/spf13/cobra"

	systemservicestategrpcpb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
	systemservicestatepb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
)

var skipConfirmation = false

// Command returns a command to restart a running service asset in a solution.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "restart <instance_name>",
		Short: "Restart a running Service instance in a solution.",
		Long:  `Restart a running Service instance in a solution.`,
		Args:  cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, stop := signal.NotifyContext(cmd.Context(), os.Interrupt)
			defer stop()

			if !skipConfirmation {
				consoleIO := bufio.NewReadWriter(
					bufio.NewReader(cmd.InOrStdin()),
					bufio.NewWriter(cmd.OutOrStdout()),
				)

				fmt.Fprintf(consoleIO,
					"The service will be temporarily unavailable during a restart. Any ongoing work may be interrupted or lost.\n\nAre you sure you want to restart %q? [Y/n] ", args[0])
				consoleIO.Flush()
				response, err := consoleIO.ReadString('\n')
				if err != nil {
					return fmt.Errorf("read response: %w", err)
				}

				response = strings.ToLower(strings.TrimSpace(response))
				if response != "y" {
					return nil
				}
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return fmt.Errorf("could not create connection to cluster: %w", err)
			}
			defer conn.Close()

			client := systemservicestategrpcpb.NewSystemServiceStateClient(conn)
			name := args[0]
			if _, err := client.RestartService(ctx, &systemservicestatepb.RestartServiceRequest{
				Name: name,
			}); err != nil {
				return fmt.Errorf("could not restart service: %w", err)
			}

			fmt.Printf("Service %q is restarting. This may take a few seconds.\n", name)
			return nil
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	cmd.Flags().BoolVarP(&skipConfirmation, "skip_confirmation", "y", false, "Skip confirmation prompt")
	return cmd
}
