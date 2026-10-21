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
	"context"
	"time"

	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	vmpoolspb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"
)

var pollInterval = 10 * time.Second

func waitForPoolReady(ctx context.Context, cmd *cobra.Command, poolName string, desiredSpec *vmpoolspb.Spec) error {
	prtr := printer.GetDefaultPrinter(cmd)
	prtr.Printf("Waiting for VM pool %q to become ready ", poolName)

	for {
		select {
		case <-ctx.Done():
			prtr.Println()
			return ctx.Err()
		default:
		}

		pools, err := fetchPoolsUserfacing(ctx)
		if err != nil {
			prtr.Printf("? (error fetching pools: %v, retrying)", err)
			time.Sleep(pollInterval)
			continue
		}

		var found *vmpoolspb.Pool
		for _, p := range pools {
			if p.GetName() == poolName {
				found = p
				break
			}
		}

		if found == nil {
			// Pool not found yet (takes time to become visible after creation)
			prtr.Printf(".")
			time.Sleep(pollInterval)
			continue
		}

		status := found.GetCurrentStatus()
		spec := found.GetSpec()

		if status == "RUNNING" && !found.GetReconciling() && specsEqual(spec, desiredSpec) {
			prtr.Printf("\nVM pool %q is ready.\n", poolName)
			return nil
		}

		prtr.Printf(".")
		time.Sleep(pollInterval)
	}
}

// waitForPoolUpdateReady polls UpdatePool repeatedly until the pool is no longer reconciling
// and reports a RUNNING status, or until the context times out.
func waitForPoolUpdateReady(ctx context.Context, cmd *cobra.Command, cl vmpoolspb.VMPoolServiceClient, req *vmpoolspb.UpdatePoolRequest) error {
	prtr := printer.GetDefaultPrinter(cmd)
	prtr.Printf("Waiting for VM pool %q to become ready ", req.GetName())

	for {
		select {
		case <-ctx.Done():
			prtr.Println()
			return ctx.Err()
		default:
		}

		resp, err := cl.UpdatePool(ctx, req)
		if err != nil {
			prtr.Printf("? (error querying pool status: %v, retrying)", err)
			time.Sleep(pollInterval)
			continue
		}

		if resp.GetCurrentStatus() == "RUNNING" && !resp.GetReconciling() {
			prtr.Printf("\nVM pool %q is ready.\n", req.GetName())
			return nil
		}

		prtr.Printf(".")
		time.Sleep(pollInterval)
	}
}

func waitForPoolDeletion(ctx context.Context, cmd *cobra.Command, poolName string) error {
	prtr := printer.GetDefaultPrinter(cmd)
	prtr.Printf("Waiting for VM pool %q to be deleted ", poolName)

	for {
		select {
		case <-ctx.Done():
			prtr.Println()
			return ctx.Err()
		default:
		}

		pools, err := fetchPoolsUserfacing(ctx)
		if err != nil {
			prtr.Printf("? (error fetching pools: %v, retrying)", err)
			time.Sleep(pollInterval)
			continue
		}

		var found *vmpoolspb.Pool
		for _, p := range pools {
			if p.GetName() == poolName {
				found = p
				break
			}
		}

		if found == nil {
			prtr.Printf("\nVM pool %q has been deleted.\n", poolName)
			return nil
		}

		if found.GetCurrentStatus() == "DELETED" {
			prtr.Printf("\nVM pool %q is DELETED.\n", poolName)
			return nil
		}

		prtr.Printf(".")
		time.Sleep(pollInterval)
	}
}

func specsEqual(a, b *vmpoolspb.Spec) bool {
	if a == nil || b == nil {
		return a == b
	}
	return a.GetRuntime() == b.GetRuntime() &&
		a.GetIntrinsicOs() == b.GetIntrinsicOs() &&
		a.GetHardwareTemplate() == b.GetHardwareTemplate() &&
		a.GetPoolTier() == b.GetPoolTier()
}
