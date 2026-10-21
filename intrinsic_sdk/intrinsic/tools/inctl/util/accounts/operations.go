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

package accounts

import (
	"context"
	"fmt"
	"time"

	"google.golang.org/grpc"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// GetOperationFunc is a function that gets a long running operation.
type GetOperationFunc func(ctx context.Context, in *lropb.GetOperationRequest, opts ...grpc.CallOption) (*lropb.Operation, error)

const (
	pollInterval = time.Second * 5
)

// WaitForOperation waits for a long running operation to complete.
func WaitForOperation(ctx context.Context, getLongOp GetOperationFunc, lro *lropb.Operation, timeout time.Duration) (*lropb.Operation, error) {
	if lro == nil {
		return nil, fmt.Errorf("no operation to wait for")
	}
	if lro.Done {
		fmt.Printf("Operation (%q) completed\n", lro.Name)
		return lro, nil
	}

	fmt.Printf("Waiting for operation (%q) to complete (%.1f seconds timeout, %v poll interval).\n",
		lro.Name, timeout.Seconds(), pollInterval)
	ts := time.Now()
	defer func() {
		fmt.Printf("Waited %.1f seconds for operation.\n", time.Since(ts).Seconds())
	}()

	ctx, stop := context.WithTimeout(ctx, timeout)
	defer stop()
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	req := lropb.GetOperationRequest{Name: lro.Name}
	for {
		select {
		case <-ticker.C:
			lro, err := getLongOp(ctx, &req)
			if err != nil {
				return nil, err
			}
			if !lro.GetDone() {
				continue
			}
			if lro.GetError() != nil {
				return nil, fmt.Errorf("operation %q failed: %v", lro.GetName(), lro.GetError())
			}
			return lro, nil
		case <-ctx.Done():
			return nil, fmt.Errorf("operation %q timed out", lro.GetName())
		}
	}
}
