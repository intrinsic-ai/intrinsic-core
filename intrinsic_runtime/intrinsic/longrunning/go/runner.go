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

package operations

import (
	"context"
)

// Runner combines a Map and a Queue to provide a single point of interaction for
// enqueuing operations and adding them to the map.
type Runner struct {
	m *Map
	q *Queue
}

// NewRunner creates a new Runner.
func NewRunner(m *Map, q *Queue) *Runner {
	return &Runner{m: m, q: q}
}

// Schedule enqueues an operation to be executed. The provided context is detached from the parent
// (removes cancellation and deadline) and passed to the executionFn. This context may be canceled
// using [Operation.Cancel]. The queue will gracefully handle operation cancellation and will update
// the operation in the [Map] with the result from the executionFn.
//
// If enqueuing is successful, the operation is added to the map.
//
// Returns [ErrQueueFull] if the queue is full. Returns an error if the operation is already done.
func (r *Runner) Schedule(ctx context.Context, op *Operation, executionFn ExecutionFn) error {
	if err := r.q.Enqueue(ctx, op, executionFn); err != nil {
		return err
	}
	r.m.Add(op)
	return nil
}

// ScheduleNew creates a new operation and uses that to enqueue a new execution
// function.  The given prefix is joined with a UUID as "{prefix}/{UUID}".
func (r *Runner) ScheduleNew(ctx context.Context, prefix string, executionFn ExecutionFn) (*Operation, error) {
	op := NewWithPrefix(prefix)
	if err := r.Schedule(ctx, op, executionFn); err != nil {
		return nil, err
	}
	return op, nil
}
