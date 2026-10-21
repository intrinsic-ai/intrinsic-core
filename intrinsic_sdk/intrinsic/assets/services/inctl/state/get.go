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

// Package get contains commands for introspecting the state of a running service asset in a
// solution.
package get

import (
	"fmt"
	"os"
	"os/signal"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/services/inctl/state/stateutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	systemservicestategrpcpb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
	systemservicestatepb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
)

// Command returns a command to get the state of a running service asset in a solution.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "get <name>",
		Short: "Get the state of a running Service instance in a solution.",
		Long:  `Get the state of a running Service instance in a solution. The default output is a condensed view of the state. Use --output=json to get a more verbose output.`,
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
			res, err := client.GetInstanceState(ctx, &systemservicestatepb.GetInstanceStateRequest{
				Name: args[0],
			})
			if err != nil {
				return fmt.Errorf("could not get state: %w", err)
			}

			fmt.Println(&stateutils.StatePrinter{
				Proto:      res,
				OutputType: printer.GetFlagOutputType(cmd),
			})
			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	return cmd
}
