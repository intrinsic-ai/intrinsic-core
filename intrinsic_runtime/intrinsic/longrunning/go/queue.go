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
	"errors"
	"fmt"
	"sync"

	log "github.com/golang/glog"
	"github.com/pborman/uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
)

var (
	// ErrQueueFull is returned by [Queue.Enqueue] if the queue buffer is full.
	ErrQueueFull = errors.New("queue is full")

	// uuidNew is mocked out for tests.
	uuidNew = uuid.New
)

// ExecutionFn executes an operation as part of a [Queue]. The provided context is cancellable using
// [Operation.Cancel] and should be used during execution.
type ExecutionFn = func(context.Context) (proto.Message, error)

// queueOperation encapsulates information used by the queue workers to execute operations.
type queueOperation struct {
	op *Operation
	fn func() (proto.Message, error)
}

// Queue is a fixed-length queue of operations with support for parallel execution using multiple
// workers. It offers support for ergonomic context cancellation and will update a [Map] in place.
// Processing of the queue can be started and stopped arbitrarily.
type Queue struct {
	q chan *queueOperation

	mu   sync.Mutex
	stop chan struct{}

	wgWorkers sync.WaitGroup
}

// NewQueue creates a new queue with the give size that operates on the opMap. Note that the size of
// the queue is eagerly allocated and should be chosen carefully. Sizes <0 are coerced to 0.
func NewQueue(size int) *Queue {
	if size < 0 {
		size = 0
	}
	return &Queue{
		q: make(chan *queueOperation, size),
	}
}

// Start begins processing the queue with the specified number of workers. Returns one all workers
// are started. This is a no-op if the queue is already started. The number of workers can be set to
// 1 for strictly sequential processing. Values for numWorkers <=0 will be coerced to 1.
func (q *Queue) Start(numWorkers int) {
	q.mu.Lock()
	defer q.mu.Unlock()
	// The queue is already running when the stop channel is set. Don't start more workers.
	if q.stop != nil {
		return
	}
	if numWorkers <= 0 {
		numWorkers = 1
	}
	q.stop = make(chan struct{})
	q.wgWorkers.Add(numWorkers)
	// Track the startup of each Goroutine. Not doing this would mean returning after scheduling.
	// Goroutines may not be running yet. Returning after all Goroutines are started is more
	// deterministic and makes testing simpler.
	var wgStart sync.WaitGroup
	wgStart.Add(numWorkers)
	for i := 0; i < numWorkers; i++ {
		go func() {
			defer q.wgWorkers.Done()
			wgStart.Done()
			startQueueWorker(i, q.q, q.stop)
		}()
	}
	wgStart.Wait()
}

// Stop shuts down all queue workers. Blocks until all workers are successfully stopped. Any running
// operations must finish before the corresponding worker is considered stopped. This may block for
// a substantial amount of time. Operations are not canceled implicitly and the queue is not
// emptied. This is a no-op if the queue is not started.
func (q *Queue) Stop() {
	q.mu.Lock()
	defer q.mu.Unlock()
	if q.stop == nil {
		return
	}
	close(q.stop)
	// Wait for the workers to actually shut down. This lets callers be sure that a item enqueued
	// after stopping will not be processed.
	q.wgWorkers.Wait()
	q.stop = nil
}

// Enqueue enqueues an operation to be executed. The provided context is detached from the parent
// (removes cancellation and deadline) and passed to the executionFn. This context may be canceled
// using [Operation.Cancel]. The queue will gracefully handle operation cancellation and will update
// the operation in the [Map] with the result from the executionFn.
//
// Returns [ErrQueueFull] if the queue is full. Returns an error if the operation is already done.
func (q *Queue) Enqueue(ctx context.Context, op *Operation, executionFn ExecutionFn) error {
	// Don't try to enqueue an operation that's already marked as done. If the operation is done there
	// is nothing to do anymore.
	if op.Done() {
		return fmt.Errorf("operation %q is already done", op.Name())
	}
	// Detach the context used in the operation from the parent context. The detached context's cancel
	// function is set on the operation to enable [Operation.Cancel].
	opCtx, cancel := context.WithCancelCause(context.WithoutCancel(ctx))
	op.setCancel(cancel)
	select {
	case q.q <- &queueOperation{
		op: op,
		fn: func() (proto.Message, error) {
			// Wrap the execution function in another function. This allows us to interrogate the context
			// error for cancellation/deadline after execution.
			res, err := executionFn(opCtx)
			// Remove the cancel function so that [Operation.Cancel] becomes a no-op.
			op.setCancel(nil)
			if err != nil {
				return nil, propagateContextStatus(opCtx, err)
			}
			return res, nil
		},
	}:
		return nil
	default:
		return ErrQueueFull
	}
}

// startQueueWorker waits for [queueOperation] on the in channel until the stop channel is closed.
// The worker executes received operations sequentially. It writes the results of the execution
// function to the operation.
func startQueueWorker(num int, in <-chan *queueOperation, stop <-chan struct{}) {
	log.V(1).Infof("worker #%d: start", num)
	for {
		select {
		case qop := <-in:
			if res, err := qop.fn(); err != nil {
				qop.op.SetError(err)
			} else if err := qop.op.SetResponse(res); err != nil {
				qop.op.SetError(status.Error(codes.Internal, err.Error()))
			}
		case <-stop:
			log.V(1).Infof("worker #%d: stop", num)
			return
		}
	}
}

// propagateContextStatus modifies the status code of the error to Canceled if the given context is
// canceled. Otherwise the status is taken directly from the given error.
func propagateContextStatus(ctx context.Context, err error) error {
	switch ctx.Err() {
	case context.Canceled:
		return status.Error(codes.Canceled, err.Error())
	default:
		return status.Convert(err).Err()
	}
}
