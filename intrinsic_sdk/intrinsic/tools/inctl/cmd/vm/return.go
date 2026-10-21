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

package vm

import (
	"context"
	"fmt"

	"intrinsic/tools/inctl/util/vmalias"

	"github.com/spf13/cobra"
	"go.opencensus.io/trace"

	leaseapigrpcpb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	leasepb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
)

var returnDesc = `
Return a leased VM back to the pool.

All data on the VM will be lost.

Example:
	inctl vm return vmp-3f30-x9t7q72u --org <my-org>
` +
	``

var vmReturnCmd = &cobra.Command{
	Use:   "return",
	Short: "Return a leased VM back to the pool.",
	Long:  returnDesc,
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx, span := trace.StartSpan(cmd.Context(), "inctl.vm.return")
		span.AddAttributes(trace.StringAttribute("vm", args[0]))
		span.AddAttributes(trace.StringAttribute("org", vmCmdFlags.GetFlagOrganization()))
		defer span.End()
		cl, err := newLeaseClient(ctx, withLeaseRetry())
		if err != nil {
			return err
		}

		return Return(ctx, cl, args[0], vmCmdFlags.GetFlagProject(), flagServiceTag)
	},
}

// Return returns a leased VM back to the pool.
func Return(ctx context.Context, cl leaseapigrpcpb.VMPoolLeaseServiceClient, vmArg, project string, serviceTag string) error {
	vmID := vmalias.ResolvePrint(vmArg, project)
	if _, err := cl.Return(ctx, &leasepb.ReturnRequest{Instance: vmID, ServiceTag: serviceTag}); err != nil {
		return fmt.Errorf("return failed with: %v", err)
	}
	fmt.Printf("VM %s returned. Thank you.\n", vmID)
	return nil
}
