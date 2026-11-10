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

// Package status contains the inctl solution status command.
package status

import (
	"fmt"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/encoding/protojson"

	ssgrpcpb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	sspb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	statuspb "intrinsic/frontend/solution_service/proto/status_go_proto"
)

type statusView struct {
	status *statuspb.Status
}

// viewOf wraps the given Status proto message in a printer.View.
func viewOf(status *statuspb.Status) printer.View {
	return &statusView{status: status}
}

// MarshalJSON delegates JSON marshalling to protojson.
func (v *statusView) MarshalJSON() ([]byte, error) {
	if v.status == nil {
		return []byte("null"), nil
	}
	return protojson.Marshal(v.status)
}

// Tabulated returns nil as tabular output is not supported.
func (v *statusView) Tabulated([]string) []string {
	return nil
}

// String formats the Status as a structured, human-readable text presentation.
func (v *statusView) String() string {
	if v.status == nil {
		return ""
	}
	return fmt.Sprintf(`Solution:   %v
ID:         %v

State:      %v
Reason:     %v

Simulation: %v
Platform:   %v
Cluster:    %v`,
		v.status.GetDisplayName(),
		v.status.GetName(),
		v.status.GetState(),
		v.status.GetStateReason(),
		v.status.GetSimulated(),
		v.status.GetPlatformVersion(),
		v.status.GetClusterName(),
	)
}

// NewCommand returns the solution status subcommand.
func NewCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "status",
		Short: "Print the status of a running solution",
		RunE: func(cmd *cobra.Command, args []string) error {
			prtr, err := printer.NewPrinterFromCommand(cmd)
			if err != nil {
				return err
			}
			defer printer.Flush(prtr)

			ctx := cmd.Context()

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			client := ssgrpcpb.NewSolutionServiceClient(conn)
			resp, err := client.GetStatus(ctx, &sspb.GetStatusRequest{})
			if err != nil {
				return fmt.Errorf("unable to get solution status: %w", err)
			}
			prtr.Println(viewOf(resp))

			return nil
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagsProjectOrgOptional()
	flags.AddFlagsAddressClusterSolution()

	return cmd
}
