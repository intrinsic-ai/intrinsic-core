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
	"fmt"

	"intrinsic/kubernetes/vmpool/service/pkg/defaults"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"go.opencensus.io/trace"
	"google.golang.org/protobuf/encoding/prototext"
	fmpb "google.golang.org/protobuf/types/known/fieldmaskpb"

	vmpoolspb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"
)

func textForUpsert(command string) string {
	return fmt.Sprintf(`You can specify:
	- Runtime
	- IntrinsicOS
	- tier (preset for pool size, e.g. how many VMs ready anticipated and maximum amount)
	- hardware template (which hardware to use for the VMs inside that pool)

 Example fully specified:
	 inctl vm pool %s \
	 --pool=usecase1 \
	 --tier=%s \
	 --hwtemplate=%s \
	 --runtime=intrinsic.platform.20241108.RC00 \
	 --intrinsic-os=20241108.RC00 \
	 --org=<my-org>

 Example pool using default values (current runtime & os + default tier & hwtemplate):
	 inctl vm pool %s \
	 --pool=usecase2 \
	 --org=<my-org>
	`, command, defaults.Tier, defaults.HardwareTemplate, command)
}

var createDesc = `Create a new VM pool.

After creating the pool, it will need some time to become ready. You can check the status with:
	inctl vm pool list
Pools become visible in the list command once they reach INITIALIZING, which can take 1-3 minutes.

` + textForUpsert("create")

func validateUpsertParams() error {
	if flagTier == "" {
		flagTier = defaults.Tier
	}
	if flagHardwareTemplate == "" {
		flagHardwareTemplate = defaults.HardwareTemplate
	}
	return nil
}

func getCreatePoolRequest() *vmpoolspb.CreatePoolRequest {
	return &vmpoolspb.CreatePoolRequest{
		Name: flagPool,
		Spec: &vmpoolspb.Spec{
			Runtime:          flagRuntime,
			IntrinsicOs:      flagIntrinsicOS,
			PoolTier:         flagTier,
			HardwareTemplate: flagHardwareTemplate,
		},
	}
}

var vmpoolsCreateCmd = &cobra.Command{
	Use:   "create",
	Short: "Create a new VM pool.",
	Long:  createDesc,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, span := trace.StartSpan(ctx, "inctl.vmpools.create")
		defer span.End()
		prtr := printer.GetDefaultPrinter(cmd)
		if err := validateUpsertParams(); err != nil {
			return err
		}
		cl, err := newVmpoolsClient(ctx)
		if err != nil {
			return err
		}
		resp, err := cl.CreatePool(ctx, getCreatePoolRequest())
		if err != nil {
			return err
		}
		prtr.Println("VM pool created.")
		prtr.Println(prototext.MarshalOptions{Multiline: true}.Format(resp))

		if flagWaitTimeout > 0 {
			waitCtx, cancel := context.WithTimeout(ctx, flagWaitTimeout)
			defer cancel()
			if err := waitForPoolReady(waitCtx, cmd, resp.GetName(), resp.GetSpec()); err != nil {
				return fmt.Errorf("failed to wait for pool readiness: %w", err)
			}
		}
		return nil
	},
}

func getUpdatePoolRequest(cmd *cobra.Command) *vmpoolspb.UpdatePoolRequest {
	mask := &fmpb.FieldMask{}
	if cmd.Flags().Changed("runtime") {
		mask.Paths = append(mask.Paths, "spec.runtime")
	}
	if cmd.Flags().Changed("intrinsic-os") {
		mask.Paths = append(mask.Paths, "spec.intrinsic_os")
	}
	if cmd.Flags().Changed("tier") {
		mask.Paths = append(mask.Paths, "spec.pool_tier")
	}
	if cmd.Flags().Changed("hwtemplate") {
		mask.Paths = append(mask.Paths, "spec.hardware_template")
	}
	return &vmpoolspb.UpdatePoolRequest{
		Name: flagPool,
		Spec: &vmpoolspb.Spec{
			Runtime:          flagRuntime,
			IntrinsicOs:      flagIntrinsicOS,
			PoolTier:         flagTier,
			HardwareTemplate: flagHardwareTemplate,
		},
		UpdateMask: mask,
	}
}

var updateDesc = `Update an existing VM pool.

- If no flag is specified, no fields will be updated.
- If a flag is specified, that field will be updated with the value of the flag.
- If a flag is specified with an empty string as value, the endpoint will use the default value (e.g., latest runtime version or latest Intrinsic OS version).

Example: Update a pool named 'usecase0' to the latest runtime and Intrinsic OS versions:
	inctl vm pool update --pool usecase0 --runtime "" --intrinsic-os "" --org=<my-org>

` + textForUpsert("update")

var vmpoolsUpdateCmd = &cobra.Command{
	Use:   "update",
	Short: "Update an existing VM pool.",
	Long:  updateDesc,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, span := trace.StartSpan(ctx, "inctl.vmpools.update")
		defer span.End()
		prtr := printer.GetDefaultPrinter(cmd)
		if err := validateUpsertParams(); err != nil {
			return err
		}
		cl, err := newVmpoolsClient(ctx)
		if err != nil {
			return err
		}
		resp, err := cl.UpdatePool(ctx, getUpdatePoolRequest(cmd))
		if err != nil {
			return err
		}
		if resp.GetName() == "" {
			prtr.Println("VM pool unchanged.")
			return nil
		}
		if resp.GetReconciling() == true {
			prtr.Println("VM Pool update in progress...")
		} else {
			prtr.Println("VM pool updated.")
		}
		prtr.Println(prototext.MarshalOptions{Multiline: true}.Format(resp))

		if flagWaitTimeout > 0 {
			waitCtx, cancel := context.WithTimeout(ctx, flagWaitTimeout)
			defer cancel()
			if err := waitForPoolUpdateReady(waitCtx, cmd, cl, getUpdatePoolRequest(cmd)); err != nil {
				return fmt.Errorf("failed to wait for pool readiness: %w", err)
			}
		}
		return nil
	},
}
