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

package codeexecutionworker

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"time"

	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	anypb "google.golang.org/protobuf/types/known/anypb"

	"intrinsic/executive/code/jupyterclient"
	"intrinsic/executive/code/pycode"

	codeexecutionservicepb "intrinsic/executive/proto/code_execution_service_go_proto"
)

const (
	// deleteSessionTimeout is the maximum time allotted for deleting a Jupyter
	// session.
	deleteSessionTimeout = 10 * time.Second

	// resetJupyterSessionTimeout is the maximum time allotted for resetting the
	// Jupyter namespace.
	resetJupyterSessionTimeout = 3 * time.Second
)

// SessionStartWait is the time to wait before setting up the first Jupyter session,
// allowing the Jupyter server to come up.
//
// Variable to enable overriding in tests.
var SessionStartWait = 3 * time.Second

// WorkerErrorWait defines how long a worker should wait if it encountered an
// issue before trying again. This prevents, for example, the worker DoS'ing
// the Jupyter server when a session cannot be created.
//
// Variable to enable overriding in tests.
var WorkerErrorWait = 10 * time.Second

// TaskOptions specifies a task to execute code. Issued by an operation expecting
// a result on the result channel.
type TaskOptions struct {
	OperationCtx               context.Context
	Code                       string
	ReturnValueMessageFullName string
	Scope                      string

	// Channel for returning the result. Exactly one result (response or error)
	// is sent through this channel and afterwards it gets closed.
	Result chan<- TaskResult

	// Channel for messages emitted to stdout during execution. These are things
	// like the result of print() statements. Unlike the result, these would
	// appear _during_ execution. This channel gets closed after all stdout
	// messages have been emitted and before the result is returned via the
	// result channel, or if the operation gets cancelled.
	Stdout chan<- string
}

// TaskResult is the result of a code execution from Jupyter.
// Exactly one of the fields is always set.
type TaskResult struct {
	Response *anypb.Any
	Err      error
}

// CodeExecutionWorker represents a single code execution session worker from the
// worker pool.
//   - The worker waits for a new code execution job from the
//     codeExecutionTasks channel, or for a kernel reset signal from kernelResetCh.
//   - When a reset signal arrives on kernelResetCh, the worker cleans up the
//     current Jupyter session and starts a new one.
//   - When an execution request arrives:
//   - If the kernel has been used and the request has a different or empty
//     scope, the worker restarts the Jupyter session before execution.
//   - If the request shares the same scope (or the kernel is clean), the
//     existing Jupyter session is reused.
//   - The worker executes the request, reports the result back on the result
//     channel passed with the request, and keeps the session active for
//     subsequent requests.
//
// Communication happens via channels:
//   - codeExecutionTasks: channel created by the service where operations send
//     new code execution requests. When the worker is ready it receives a request,
//     which contains a result channel listened to by the operation.
//   - kernelResetCh: channel used by the service to trigger a Jupyter kernel
//     reset for this worker.
type CodeExecutionWorker struct {
	workerIndex int
	kernelDirty bool
	scope       string

	jupyterClient      *jupyterclient.Client
	codeExecutionTasks <-chan TaskOptions
	kernelResetCh      <-chan struct{}

	nextWorkerAvailable chan<- struct{}
}

// NewCodeExecutionWorker creates a new CodeExecutionWorker instance.
func NewCodeExecutionWorker(
	workerIndex int,
	jupyterServerHost string,
	codeExecutionTasks <-chan TaskOptions,
	kernelResetCh <-chan struct{},
	nextWorkerAvailable chan<- struct{},
) *CodeExecutionWorker {
	return &CodeExecutionWorker{
		workerIndex:         workerIndex,
		jupyterClient:       jupyterclient.New(jupyterServerHost, workerIndex),
		codeExecutionTasks:  codeExecutionTasks,
		kernelResetCh:       kernelResetCh,
		nextWorkerAvailable: nextWorkerAvailable,
	}
}

// Run starts the code execution worker loop.
func (w *CodeExecutionWorker) Run(ctx context.Context) {
	log.InfoContextf(ctx, "Started worker: %d", w.workerIndex)

	// Wait for a short time to allow the Jupyter server to come up. Otherwise the
	// first setupJupyterSession calls will always fail and we'd run into
	// WorkerErrorWait. If this is not large enough, the worst case is that it's
	// going to fallback to try again after an additional WorkerErrorWait.
	sleepContext(ctx, SessionStartWait)

	w.restartJupyterSession(ctx)

	// Main loop
	// Wait for a new task or kernel reset request
	// Alternatively, when the worker is canceled clean up the Jupyter session.
	for {
		log.InfoContextf(ctx, "Worker %d: Waiting for task...", w.workerIndex)
		w.signalWorkerAvailableForTesting()
		select {
		case <-ctx.Done():
			log.InfoContextf(ctx, "Worker %d: Shutting down: %v", w.workerIndex, ctx.Err())
			w.deleteJupyterSessionOrLogError(ctx)
			return
		case <-w.kernelResetCh:
			w.restartJupyterSession(ctx)
			continue
		case executeRequest := <-w.codeExecutionTasks:
			w.handleIncomingRequest(ctx, executeRequest)
		}
	}
}

func (w *CodeExecutionWorker) handleIncomingRequest(ctx context.Context, executeRequest TaskOptions) {
	log.InfoContextf(ctx, "Worker %d: Starting task execution.", w.workerIndex)

	if w.kernelDirty && (executeRequest.Scope != w.scope || w.scope == "") {
		// Safety check, this should not happen in practice if executive restarted the kernels after an operation
		w.restartJupyterSession(ctx)
	}
	w.scope = executeRequest.Scope
	w.kernelDirty = true

	// Create a context for calling the code execution. This context will be
	// canceled when the operationCtx is cancelled or when the ctx is canceled.
	executionContext, cancelExecution := context.WithCancel(context.Background())
	defer cancelExecution()

	go func() {
		select {
		case <-executeRequest.OperationCtx.Done():
			cancelExecution()
		case <-ctx.Done():
			cancelExecution()
		case <-executionContext.Done():
			// exit case as under normal operation neither ctx nor operationCtx are
			// expected to be canceled.
		}
	}()

	executeResult, err := w.jupyterClient.ExecuteCode(
		executionContext, executeRequest.Code, executeRequest.Stdout,
	)

	// Signal that no more stdout messages are to be expected.
	close(executeRequest.Stdout)

	result := TaskResult{}
	if err != nil {
		result.Err = err
	} else {
		result.Response, result.Err = computeCodeExecutionResponse(executeResult, executeRequest.ReturnValueMessageFullName)
	}

	// Send back the result to the operation. This is not a blocking call. The
	// result channel is a buffered channel and this is the only place that sends
	// to that channel at most once.
	executeRequest.Result <- result
	close(executeRequest.Result)

	if ctx.Err() != nil {
		return
	}

	// In case of a Python error or if the operation was canceled (e.g. due to a
	// timeout), restart the session to ensure the Jupyter kernel is not stuck
	// executing user code.
	if err != nil {
		log.WarningContextf(ctx, "Worker %d: Task failed; restarting session to prevent the kernel from hanging", w.workerIndex)
		w.restartJupyterSession(ctx)
		return
	}

	// Perform a lightweight reset after execution so the kernel's namespace is clean for subsequent requests.
	if err := w.resetJupyterSession(ctx); err != nil {
		log.WarningContextf(ctx, "Worker %d: Failed to reset session: %v, restarting session", w.workerIndex, err)
		w.restartJupyterSession(ctx)
	}
}

func (w *CodeExecutionWorker) setupJupyterSession(ctx context.Context) error {
	// Create a jupyter session that is now owned by this worker
	err := w.jupyterClient.CreateSession(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "Worker %d: Failed to create Jupyter session: %v", w.workerIndex, err)
		sleepContext(ctx, WorkerErrorWait)
		return err
	}

	// Seed the jupyter session with standard imports
	err = w.seedCode(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "Worker %d: Failed seeding code: %v", w.workerIndex, err)

		var pythonError *jupyterclient.PythonError
		if errors.As(err, &pythonError) {
			// Assumption: A Python error resulting from the seed code is not
			// transient and retrying will not help. E.g., there is a bug in the
			// seed code or the Python environment in the Jupyter server image is
			// not set up correctly. Surface this error as result for all operations.
			log.ErrorContextf(ctx, "Worker %d: Python error in seed code, going into error state", w.workerIndex)
			w.returnErrorForAllTasksUntilCancelled(ctx, pythonError)

			// should be called after returnErrorForAllTasksUntilCancelled so
			// ctx is canceled and we don't sleep in case of failures
			w.deleteJupyterSessionOrLogError(ctx)
			return err
		}

		// Potentially transient error, so sleep and try again.
		sleepContext(ctx, WorkerErrorWait)
		w.deleteJupyterSessionOrLogError(ctx)
		return err
	}
	w.kernelDirty = false
	w.scope = ""
	return nil
}

func (w *CodeExecutionWorker) seedCode(ctx context.Context) error {
	seedCode, err := pycode.GenerateCodeForSeeding()
	if err != nil {
		return err
	}
	log.InfoContextf(ctx, "Worker %d: Seeding code", w.workerIndex)
	executeResult, err := w.jupyterClient.ExecuteCode(
		ctx,
		seedCode,
		nil,
	)
	if err != nil {
		return err
	}
	if executeResult != "" {
		return fmt.Errorf("worker %d: seeding code produced unexpected result: %s (expected '')", w.workerIndex, executeResult)
	}
	return nil
}

func (w *CodeExecutionWorker) deleteJupyterSessionOrLogError(ctx context.Context) {
	deleteCtx, cancelDelete := context.WithTimeout(context.WithoutCancel(ctx), deleteSessionTimeout)
	defer cancelDelete()
	err := w.jupyterClient.DeleteSession(deleteCtx)
	if err != nil {
		log.ErrorContextf(ctx, "Worker %d: Failed deleting session: %v", w.workerIndex, err)
		sleepContext(ctx, WorkerErrorWait)
	}
}

// restartJupyterSession deletes the current session (if set) and creates a new
// one, starting a fresh Jupyter kernel.
func (w *CodeExecutionWorker) restartJupyterSession(ctx context.Context) {
	w.deleteJupyterSessionOrLogError(ctx)
	// This method will not return until we have a new Jupyter session created,
	// e.g. if the Jupyter server is not fully ready or took too long to create
	// the session, we'll try again after a short wait (see setupJupyterSession).
	for {
		log.InfoContextf(ctx, "Worker %d: Starting new session...", w.workerIndex)

		if err := ctx.Err(); err != nil {
			log.InfoContextf(ctx, "Worker %d: End: %v", w.workerIndex, err)
			return
		}

		err := w.setupJupyterSession(ctx)
		if err == nil {
			break
		}
	}
}

// resetJupyterSession calls the IPython %reset magic command to remove names
// defined by the user. See
// https://ipython.readthedocs.io/en/stable/api/generated/IPython.core.magics.namespace.html#IPython.core.magics.namespace.NamespaceMagics.reset
func (w *CodeExecutionWorker) resetJupyterSession(ctx context.Context) error {
	log.InfoContextf(ctx, "Worker %d: Resetting namespace", w.workerIndex)
	resetCtx, cancelReset := context.WithTimeout(ctx, resetJupyterSessionTimeout)
	defer cancelReset()

	executeResult, err := w.jupyterClient.ExecuteCode(
		resetCtx,
		pycode.ResetSessionCommand,
		nil,
	)
	if err != nil {
		return fmt.Errorf("failed to execute reset command: %w", err)
	}
	if executeResult != "" {
		return fmt.Errorf("resetting session produced unexpected result: %q (expected empty)", executeResult)
	}
	return nil
}

func (w *CodeExecutionWorker) returnErrorForAllTasksUntilCancelled(ctx context.Context, err error) {
	w.signalWorkerAvailableForTesting()
	for {
		select {
		case executeRequest := <-w.codeExecutionTasks:
			close(executeRequest.Stdout)
			executeRequest.Result <- TaskResult{Err: err}
			close(executeRequest.Result)
		case <-ctx.Done():
			return
		}
	}
}

// computeCodeExecutionResponse takes the executeResult as a string from Jupyter
// and packs that into the ReturnValue field of a CodeExecutionResponse. The
// CodeExecutionResponse is then returned as a packed Any.
func computeCodeExecutionResponse(executeResult string, returnValueMessageFullName string) (*anypb.Any, error) {
	if executeResult == "" && returnValueMessageFullName != "" {
		return nil, status.Errorf(
			codes.InvalidArgument,
			"compute function returned None but expected message with type %q "+
				"(as indicated by 'return_value_message_full_name')",
			returnValueMessageFullName,
		)
	}

	response := &codeexecutionservicepb.CodeExecutionResponse{}

	if executeResult != "" {
		if returnValueMessageFullName == "" {
			// This should never happen because
			// "code_execution.serialize_return_value_message(...)" should already fail
			// on the Jupyter server. But we check here just in case.
			return nil, status.Error(
				codes.InvalidArgument,
				"compute function returned a value but expected no return value "+
					"(as indicated by empty 'return_value_message_full_name')",
			)
		}

		resultBytes, err := pycode.PythonBytesLiteralToGoBytes(executeResult)
		if err != nil {
			return nil, fmt.Errorf("failed converting execute result to bytes: %w", err)
		}

		resultAny := &anypb.Any{}
		if err := proto.Unmarshal(resultBytes, resultAny); err != nil {
			return nil, fmt.Errorf("failed unmarshalling execute result: %w", err)
		}

		if !strings.HasSuffix(resultAny.TypeUrl, returnValueMessageFullName) {
			// This should never happen because
			// "code_execution.serialize_return_value_message(...)" should already fail
			// on the Jupyter server. But we check here just in case.
			return nil, status.Errorf(
				codes.InvalidArgument,
				"compute function returned message with type %q but expected message with type %q "+
					"(as indicated by 'return_value_message_full_name')",
				resultAny.TypeUrl, returnValueMessageFullName,
			)
		}

		response.ReturnValue = resultAny
	}

	anyResponse, err := anypb.New(response)
	if err != nil {
		return nil, fmt.Errorf("failed packing response: %w", err)
	}

	return anyResponse, nil
}

func (w *CodeExecutionWorker) signalWorkerAvailableForTesting() {
	if w.nextWorkerAvailable != nil {
		w.nextWorkerAvailable <- struct{}{}
	}
}

// Returns when the context is done or the duration has passed, whichever happens first.
func sleepContext(ctx context.Context, duration time.Duration) {
	select {
	case <-ctx.Done():
		return
	case <-time.After(duration):
		return
	}
}
