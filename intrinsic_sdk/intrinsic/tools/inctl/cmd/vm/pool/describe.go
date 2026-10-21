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

	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"go.opencensus.io/trace"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"

	vmpoolspb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"
)

var describeDesc = `
Describe a VM pool.

Example:
	inctl vm pool describe --pool my-pool --org <my-org>
`

var vmpoolsDescribeCmd = &cobra.Command{
	Use:   "describe",
	Short: "Describe a VM pool.",
	Long:  describeDesc,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, span := trace.StartSpan(ctx, "inctl.vmpools.describe")
		defer span.End()
		poolName := flagPool

		return describeVMPoolUserfacing(ctx, cmd, poolName)
	},
}

func describeVMPoolUserfacing(ctx context.Context, cmd *cobra.Command, poolName string) error {
	pools, err := fetchPoolsUserfacing(ctx)
	if err != nil {
		return err
	}

	var foundPool *vmpoolspb.Pool
	for _, p := range pools {
		if p.GetName() == poolName {
			foundPool = p
			break
		}
	}

	if foundPool == nil {
		return fmt.Errorf("pool %q not found", poolName)
	}

	return printPoolProto(cmd, foundPool)
}

func printPoolProto(cmd *cobra.Command, p proto.Message) error {
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeJSON {
		ms, err := protojson.MarshalOptions{
			Multiline:         true,
			UseProtoNames:     true,
			EmitUnpopulated:   true,
			EmitDefaultValues: true,
		}.Marshal(p)
		if err != nil {
			return err
		}
		fmt.Println(string(ms))
		return nil
	}

	// Text output (Pretty print proto)
	ms, err := prototext.MarshalOptions{
		Multiline: true,
	}.Marshal(p)
	if err != nil {
		return err
	}
	fmt.Println(string(ms))
	return nil
}
