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
	"time"

	"intrinsic/tools/inctl/util/vmalias"

	log "github.com/golang/glog"
	"github.com/spf13/cobra"
	"go.opencensus.io/trace"

	leaseapigrpcpb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	leasepb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"

	tpb "google.golang.org/protobuf/types/known/timestamppb"
)

var extendDesc = `
Extend the expiration time of a lease by a duration relative to now.

Use the time units "m" and "h". Specify --extend-only to only update the lease expiration time if it
is longer than the current one.

Example:
	inctl vm expire-in vmp-3f30-x9t7q72u 1h --org <my-org>
` +
	``

var vmExpireInCmd = &cobra.Command{
	Use:   "expire-in",
	Short: "Extend the expiration time of a leased VM by a duration relative to now.",
	Long:  extendDesc,
	Args:  cobra.ExactArgs(2),
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx, span := trace.StartSpan(cmd.Context(), "inctl.vm.expire-in")
		span.AddAttributes(trace.StringAttribute("vm", args[0]))
		span.AddAttributes(trace.StringAttribute("org", vmCmdFlags.GetFlagOrganization()))
		defer span.End()
		cl, err := newLeaseClient(ctx)
		if err != nil {
			return err
		}
		return ExpireIn(ctx, cl, args[0], args[1], vmCmdFlags.GetFlagProject(), flagExtendOnly, flagServiceTag)
	},
}

// ExpireIn extends the expiration time of a lease by a duration relative to now.
func ExpireIn(ctx context.Context, cl leaseapigrpcpb.VMPoolLeaseServiceClient, vmArg, byStr, project string, extendOnly bool, serviceTag string) error {
	vm := vmalias.ResolvePrint(vmArg, project)
	byDur, err := time.ParseDuration(byStr)
	if err != nil {
		log.ExitContextf(ctx, "%v is not valid for time.ParseDuration: %v", byStr, err)
	}
	to := time.Now().Add(byDur)

	r, err := cl.ExtendTo(ctx, &leasepb.ExtendToRequest{
		Instance: vm, To: tpb.New(to), ExtendOnly: extendOnly, ServiceTag: serviceTag,
	})
	if err != nil {
		return fmt.Errorf("extending lease failed with: %v", err)
	}
	var expires time.Time = r.GetLease().Expires.AsTime()
	fmt.Printf("Lease extended to %s (in %s)\n", expires.Format(time.RFC3339), time.Until(expires).Round(time.Second))
	if expires.Before(to) {
		fmt.Printf("Warning: This is less than what you expected, you wanted %v\n", byStr)
	}
	if extendOnly && expires.After(to) {
		fmt.Printf("Warning: This is longer than requested due to --extend-only, you wanted %v\n", byStr)
	}
	return nil
}
