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

package pool

import (
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"go.opencensus.io/trace"

	vmpoolspb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"
)

var vmpoolsLeasesCmd = cobrautil.ParentOfNestedSubcommands("leases", "Administer leases on your VM pools")

var leasesListDesc = `
List all VM leases for a VM pool.

Example:
	inctl vm pool leases list --pool my-pool --org <my-org>
`

type leasesRow struct {
	Idx     uint16 `json:"idx"`
	Name    string `json:"name"`
	Pool    string `json:"pool"`
	Expires string `json:"expires"`
}

func getLeasesRowCommandPrinter(cmd *cobra.Command) printer.CommandPrinter {
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeText {
		ot = printer.OutputTypeTAB
	}
	cp, err := printer.NewPrinterOfType(
		ot,
		cmd,
		printer.WithDefaultsFromValue(&leasesRow{}, func(columns []string) []string {
			return []string{"idx", "name", "pool", "expires"}
		}),
	)
	if err != nil {
		cmd.PrintErrf("Error setting up output: %v\n", err)
		cp = printer.GetDefaultPrinter(cmd)
	}
	return cp
}

func asLeasesRow(l *vmpoolspb.Lease, idx int) *leasesRow {
	return &leasesRow{
		Idx:     uint16(idx),
		Name:    l.GetName(),
		Pool:    l.GetPoolName(),
		Expires: l.GetExpirationTime().AsTime().String(),
	}
}

var vmpoolsLeasesListCmd = &cobra.Command{
	Use:   "list",
	Short: "List all VM leases for a VM pool.",
	Long:  leasesListDesc,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, span := trace.StartSpan(ctx, "inctl.vmpools.leases.list")
		defer span.End()
		prtr := getLeasesRowCommandPrinter(cmd)
		cl, err := newVmpoolsClient(ctx)
		if err != nil {
			return err
		}
		resp, err := cl.ListLeases(ctx, &vmpoolspb.ListLeasesRequest{PoolName: flagPool})
		if err != nil {
			return err
		}
		var view printer.View = nil // this is to reuse reflectors in default views
		for i, l := range resp.GetLeases() {
			view = printer.NextView(asLeasesRow(l, i), view)
			prtr.Println(view)
		}
		printer.Flush(prtr)
		return nil
	},
}

var leasesStopDesc = `
Stop a VM lease.

Example:
	# find the lease that you want to stop
	inctl vm pool leases list --pool my-pool --org <my-org>
	# stop the lease
	inctl vm pool leases stop --lease vmp-my-lease --org <my-org>
`

var vmpoolsLeasesStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop a VM lease.",
	Long:  leasesStopDesc,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, span := trace.StartSpan(ctx, "inctl.vmpools.leases.stop")
		defer span.End()
		prtr := printer.GetDefaultPrinter(cmd)
		cl, err := newVmpoolsClient(ctx)
		if err != nil {
			return err
		}
		_, err = cl.StopLease(ctx, &vmpoolspb.StopLeaseRequest{LeaseName: flagLease})
		if err != nil {
			return err
		}
		prtr.Printf("VM lease %s will stop.\n", flagLease)
		return nil
	},
}
