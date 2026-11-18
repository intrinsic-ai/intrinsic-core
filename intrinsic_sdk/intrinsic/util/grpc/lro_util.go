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

// Package lroutil provides helpers for waiting on long-running gRPC operations.
package lroutil

import (
	"context"
	"fmt"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/types/known/durationpb"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// RelaySafeWaitTimeout stays below the 60s request timeout enforced by HTTP relays.
const RelaySafeWaitTimeout = 30 * time.Second

// WaitOperation polls client.WaitOperation in RelaySafeWaitTimeout slices until
// op is done. It returns an error if a poll fails or if the operation finished
// with an error. The last known operation is returned in both cases, so callers
// can tell the two apart with GetDone and can still inspect or cancel it.
func WaitOperation(ctx context.Context, client lropb.OperationsClient, op *lropb.Operation, opts ...grpc.CallOption) (*lropb.Operation, error) {
	name := op.GetName()
	for !op.GetDone() {
		next, err := client.WaitOperation(ctx, &lropb.WaitOperationRequest{
			Name:    name,
			Timeout: durationpb.New(RelaySafeWaitTimeout),
		}, opts...)
		if err != nil {
			return op, fmt.Errorf("waiting for operation %q: %w", name, err)
		}
		op = next
	}
	return op, status.ErrorProto(op.GetError())
}
