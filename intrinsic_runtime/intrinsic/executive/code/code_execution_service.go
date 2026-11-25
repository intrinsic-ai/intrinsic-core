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

// Package codeexecutionservice implements the handlers for CodeExecutionService
// and CodeExecutionInfoService.
package codeexecutionservice

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"time"

	operationspb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	log "github.com/golang/glog"
	"github.com/pborman/uuid"
	statuspb "google.golang.org/genproto/googleapis/rpc/status"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	anypb "google.golang.org/protobuf/types/known/anypb"
	emptypb "google.golang.org/protobuf/types/known/emptypb"

	"intrinsic/executive/code/codeexecutionworker"
	"intrinsic/executive/code/jupyterclient"
	"intrinsic/executive/code/pycode"

	codeexecutioninfoservicepb "intrinsic/executive/proto/code_execution_info_service_go_proto"
	codeexecutionservicegrpcpb "intrinsic/executive/proto/code_execution_service_go_proto"
	codeexecutionservicepb "intrinsic/executive/proto/code_execution_service_go_proto"
)

const (
	// defaultPageSize is the default page size for ListOperations.
	defaultPageSize = 20
	// maxPageSize is the maximum page size for ListOperations.
	maxPageSize = 200
	// operationTimeout is the timeout for a code execution operation.
	operationTimeout = 60 * time.Second
)

func errorToOperationStatus(err error) *statuspb.Status {
	var pythonError *jupyterclient.PythonError
	if errors.As(err, &pythonError) {
		return pythonError.ToStatus()
	}
	statusProto := status.Convert(err).Proto()
	if errors.Is(err, context.Canceled) {
		statusProto.Code = int32(codes.Canceled)
	}
	return statusProto
}

// operation represents a long-running code execution operation.
type operation struct {
	proto *operationspb.Operation

	// cancelWorker cancels the context of the worker that is executing the
	// operation.
	cancelWorker context.CancelFunc

	// finished is closed when the worker has finished.
	finished chan struct{}

	// All responses received during the operation. Each new streaming request
	// will initially receive all of these.
	responses []*codeexecutionservicepb.StreamExecuteCodeResponseResponse
	// responseStream receives response messages from the code execution. It is
	// initially nil and will only be set if there is an active stream.
	responseStream chan *codeexecutionservicepb.StreamExecuteCodeResponseResponse
}

// CodeExecutionService implements the server interfaces for
// CodeExecutionService and CodeExecutionInfoService.
type CodeExecutionService struct {
	jupyterServerHost      string
	worldServiceAddress    string
	geometryServiceAddress string
	numJupyterSessions     int

	// operationsMutex protects operations.
	operationsMutex sync.Mutex
	// operations holds the operations which have not been deleted by the user.
	operations map[string]*operation

	// codeExecutionWorkerWg holds the currently active worker threads.
	codeExecutionWorkerWg sync.WaitGroup
	// codeExecutionTasks is the main channel that operations send code execution
	// requests to. There is one global channel that is intentionally unbuffered.
	// A worker receiving from the channel signals that the actual execution has
	// started.
	codeExecutionTasks chan codeexecutionworker.TaskOptions

	// If not nil one value will be sent on this channel after finishing each
	// operation and storing an operation result. Only use in tests.
	nextOperationResultAvailable chan<- struct{}
	// If not nil one value will be sent on this channel when a worker becomes
	// available. Only use in tests.
	nextWorkerAvailable chan<- struct{}

	// Index by workerIndex. Can be used to trigger a Jupyter kernel reset of a
	// specific worker
	kernelResetChs []chan struct{}
}

// New creates a new CodeExecutionService instance.
func New(ctx context.Context, jupyterServerHost string, worldServiceAddress string, geometryServiceAddress string, numJupyterSessions int, nextOperationResultAvailable chan<- struct{}, nextWorkerAvailable chan<- struct{}) *CodeExecutionService {
	service := &CodeExecutionService{
		jupyterServerHost:            jupyterServerHost,
		worldServiceAddress:          worldServiceAddress,
		geometryServiceAddress:       geometryServiceAddress,
		numJupyterSessions:           numJupyterSessions,
		operations:                   make(map[string]*operation),
		codeExecutionTasks:           make(chan codeexecutionworker.TaskOptions),
		nextOperationResultAvailable: nextOperationResultAvailable,
		nextWorkerAvailable:          nextWorkerAvailable,
		kernelResetChs:               make([]chan struct{}, numJupyterSessions),
	}
	service.createSessionPool(ctx)
	return service
}

// WaitShutdown waits until all workers are shut down.
func (s *CodeExecutionService) WaitShutdown() {
	log.Info("Waiting for all active workers to end.")
	s.codeExecutionWorkerWg.Wait()
}

func (s *CodeExecutionService) generateCodeForExecutionFromRequest(request *codeexecutionservicepb.CodeExecutionRequest) (string, error) {
	var functionBody string
	switch code := request.Code.(type) {
	case *codeexecutionservicepb.CodeExecutionRequest_PythonCode:
		functionBody = code.PythonCode.FunctionBody
		if functionBody == "" {
			return "", status.Errorf(codes.InvalidArgument, "Python code must not be empty")
		}
	default:
		return "", status.Errorf(codes.InvalidArgument, "Python code is required")
	}

	if request.ParameterMessageFullName != "" && request.Parameters == nil {
		return "", status.Errorf(
			codes.InvalidArgument, "'parameters' is required if 'parameter_message_full_name' is set",
		)
	}

	return pycode.GenerateCodeForExecution(pycode.GenerateCodeForExecutionOptions{
		FunctionBody:               functionBody,
		ParamAny:                   request.Parameters,                 // can be nil
		FileDescriptorSet:          request.FileDescriptorSet,          // can be nil
		ParameterMessageFullName:   request.ParameterMessageFullName,   // can be ""
		ReturnValueMessageFullName: request.ReturnValueMessageFullName, // can be ""
		WorldID:                    request.WorldId,
		WorldServiceAddress:        s.worldServiceAddress,
		GeometryServiceAddress:     s.geometryServiceAddress,
	})
}

func (s *CodeExecutionService) createSessionPool(ctx context.Context) {
	log.InfoContextf(ctx, "Starting %d workers", s.numJupyterSessions)
	for i := range s.numJupyterSessions {
		s.kernelResetChs[i] = make(chan struct{}, 1)
		worker := codeexecutionworker.NewCodeExecutionWorker(i, s.jupyterServerHost, s.codeExecutionTasks, s.kernelResetChs[i], s.nextWorkerAvailable)
		s.codeExecutionWorkerWg.Add(1)
		go func() {
			defer s.codeExecutionWorkerWg.Done()
			worker.Run(ctx)
		}()
	}
}

// finishOperation sets the operation to done and fills the proto based on
// the given result.
func (s *CodeExecutionService) finishOperation(operation *operation, result *codeexecutionworker.TaskResult) {
	s.operationsMutex.Lock()
	operation.proto.Done = true
	if result.Err != nil {
		operation.proto.Result = &operationspb.Operation_Error{
			Error: errorToOperationStatus(result.Err),
		}
	} else {
		operation.proto.Result = &operationspb.Operation_Response{Response: result.Response}
	}
	s.operationsMutex.Unlock()

	close(operation.finished)

	// Signal that the operation result is available (for testing).
	if s.nextOperationResultAvailable != nil {
		s.nextOperationResultAvailable <- struct{}{}
	}
}

// writeStdoutToOperation writes the given stdout message to the operation's
// metadata. Appends new messages to existing ones.
func (s *CodeExecutionService) writeStdoutToOperation(operation *operation, stdout string) error {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()
	metadata := &codeexecutionservicepb.CodeExecutionMetadata{}
	if err := operation.proto.GetMetadata().UnmarshalTo(metadata); err != nil {
		return fmt.Errorf("failed unmarshalling metadata: %w", err)
	}
	metadata.Stdout = append(metadata.GetStdout(), stdout)
	updatedMetadataAny, err := anypb.New(metadata)
	if err != nil {
		return fmt.Errorf("failed packing metadata: %w", err)
	}
	operation.proto.Metadata = updatedMetadataAny
	streamResponse := &codeexecutionservicepb.StreamExecuteCodeResponseResponse{
		Type: &codeexecutionservicepb.StreamExecuteCodeResponseResponse_Stdout{
			Stdout: stdout,
		},
	}
	operation.responses = append(operation.responses, streamResponse)
	// Also write to the response stream if it's available.
	if operation.responseStream != nil {
		operation.responseStream <- streamResponse
	}
	return nil
}

func (s *CodeExecutionService) ExecuteCode(ctx context.Context, request *codeexecutionservicepb.CodeExecutionRequest) (*operationspb.Operation, error) {
	metadataAny, err := anypb.New(&codeexecutionservicepb.CodeExecutionMetadata{})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed packing metadata: %v", err)
	}

	code, err := s.generateCodeForExecutionFromRequest(request)
	if err != nil {
		return nil, err
	}

	operationProto := &operationspb.Operation{
		Name:     uuid.New(),
		Metadata: metadataAny,
		Done:     false,
	}

	deadline := time.Now().Add(operationTimeout)
	operationCtx, cancelOperation := context.WithDeadline(context.WithoutCancel(ctx), deadline)

	operation := &operation{
		proto:        proto.Clone(operationProto).(*operationspb.Operation),
		cancelWorker: cancelOperation,
		finished:     make(chan struct{}),
	}

	s.operationsMutex.Lock()
	s.operations[operation.proto.Name] = operation
	s.operationsMutex.Unlock()

	go func() {
		// This must be a buffered channel, so that the session worker thread is not blocked
		// waiting for a receive if the operation times out and this thread is gone.
		resultCh := make(chan codeexecutionworker.TaskResult, 1)

		// There's no need to buffer this channel since we always have the
		// goroutine below reading from it.
		stdoutCh := make(chan string)

		// Process stdout messages. This needs to happen in a separate goroutine
		// which does not quit when the operation is cancelled (see operationCtx)
		// so that the sender on stdoutCh cannot get blocked.
		var stdoutWg sync.WaitGroup
		stdoutWg.Go(
			func() {
				for {
					stdout, more := <-stdoutCh
					if !more {
						// Channel was closed, end goroutine.
						return
					}
					if err := s.writeStdoutToOperation(operation, stdout); err != nil {
						log.WarningContextf(operationCtx, "Failed writing stdout to operation: %v", err)
					}
				}
			})

		codeExecutionTask := codeexecutionworker.TaskOptions{
			OperationCtx:               operationCtx,
			Code:                       code,
			ReturnValueMessageFullName: request.ReturnValueMessageFullName,
			Result:                     resultCh,
			Stdout:                     stdoutCh,
			Scope:                      request.GetScope(),
		}

		// Step 1: Schedule: Wait until task is picked up by a worker.
		result := codeexecutionworker.TaskResult{}
		log.InfoContextf(operationCtx, "Scheduling code execution task...")
		select {
		case s.codeExecutionTasks <- codeExecutionTask:
			log.InfoContextf(operationCtx, "Task scheduled. Waiting for code execution...")
		case <-operationCtx.Done():
			log.WarningContextf(operationCtx, "Operation cancelled before worker ready")
			close(stdoutCh)
			s.finishOperation(operation, &codeexecutionworker.TaskResult{
				Err: operationCtx.Err(),
			})
			return
		}

		// Step 2: Receive execution result and save in operation.
		select {
		case result = <-resultCh:
			log.InfoContextf(operationCtx, "Code execution completed")
		case <-operationCtx.Done():
			log.WarningContextf(operationCtx, "Operation cancelled before result")
			result = codeexecutionworker.TaskResult{
				Err: operationCtx.Err(),
			}
		}

		// Make sure all stdout messages have been processed before we finish the
		// operation. Finishing the operation will stop the consumer thread in
		// StreamExecuteCodeResponse() below which is required for
		// writeStdoutToOperation() in the stdout thread not to get stuck.
		stdoutWg.Wait()

		s.finishOperation(operation, &result)
	}()

	return operationProto, nil
}

func (s *CodeExecutionService) ListOperations(_ context.Context, request *operationspb.ListOperationsRequest) (*operationspb.ListOperationsResponse, error) {
	if request.Name != "" {
		return nil, status.Errorf(codes.InvalidArgument, "parent resource names are not supported")
	}
	if request.Filter != "" {
		return nil, status.Errorf(codes.InvalidArgument, "filters are not supported")
	}

	pageSize := defaultPageSize
	if request.PageSize < 0 {
		return nil, status.Errorf(
			codes.InvalidArgument, "page size must be positive but got %d", request.PageSize,
		)
	}
	if request.PageSize > 0 {
		pageSize = min(int(request.PageSize), maxPageSize)
	}
	if request.PageToken != "" {
		return nil, status.Errorf(codes.Unimplemented, "page token not supported yet")
	}

	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()

	if n := len(s.operations); n > pageSize {
		return nil, status.Errorf(
			codes.Unimplemented,
			"operations (n = %d) do no fit into single page (requested page size = %d), "+
				"pagination not supported yet",
			n, pageSize,
		)
	}

	response := &operationspb.ListOperationsResponse{}
	for _, operation := range s.operations {
		response.Operations = append(
			response.Operations,
			proto.Clone(operation.proto).(*operationspb.Operation),
		)
	}

	return response, nil
}

func (s *CodeExecutionService) GetOperation(_ context.Context, request *operationspb.GetOperationRequest) (*operationspb.Operation, error) {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()

	operation, ok := s.operations[request.Name]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "operation '%s' not found", request.Name)
	}

	return proto.Clone(operation.proto).(*operationspb.Operation), nil
}

func (s *CodeExecutionService) DeleteOperation(_ context.Context, request *operationspb.DeleteOperationRequest) (*emptypb.Empty, error) {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()

	operation, ok := s.operations[request.Name]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "operation '%s' not found", request.Name)
	}

	if !operation.proto.Done {
		return nil, status.Errorf(
			codes.FailedPrecondition, "operation '%s' has not finished yet",
			request.Name,
		)
	}

	delete(s.operations, request.Name)

	return &emptypb.Empty{}, nil
}

func (s *CodeExecutionService) CancelOperation(_ context.Context, request *operationspb.CancelOperationRequest) (*emptypb.Empty, error) {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()

	operation, ok := s.operations[request.Name]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "operation '%s' not found", request.Name)
	}

	operation.cancelWorker()

	return &emptypb.Empty{}, nil
}

func (s *CodeExecutionService) WaitOperation(ctx context.Context, request *operationspb.WaitOperationRequest) (*operationspb.Operation, error) {
	s.operationsMutex.Lock()
	operation, ok := s.operations[request.Name]
	s.operationsMutex.Unlock()
	if !ok {
		return nil, status.Errorf(codes.NotFound, "operation '%s' not found", request.Name)
	}

	var timeoutChannel <-chan time.Time
	if request.Timeout != nil {
		duration := request.Timeout.AsDuration()
		if duration <= 0 {
			return nil, status.Errorf(
				codes.InvalidArgument, "timeout must be positive but got %v", request.Timeout,
			)
		}
		timeoutChannel = time.After(duration)
	}

	// Wait for the first of the following events:
	// - Operation gets finished or is already finished.
	// - RPC context gets canceled.
	// - User-specified timeout gets reached (if provided).
	select {
	case <-operation.finished:
	case <-ctx.Done():
	case <-timeoutChannel:
	}

	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()
	operation, ok = s.operations[request.Name]
	if !ok {
		return nil, status.Errorf(
			codes.NotFound, "operation '%s' not found after waiting", request.Name,
		)
	}

	return proto.Clone(operation.proto).(*operationspb.Operation), nil
}

// operationForResponseStream returns the operation for a response stream.
// Returns the operation if it is valid for streaming. The operation will not
// have a response stream channel. Returns an error if there is an issue
// preventing streaming on the operation.
func (s *CodeExecutionService) operationForResponseStream(operationName string) (*operation, error) {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()
	op, ok := s.operations[operationName]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "operation %q not found", operationName)
	}
	// Don't stream anything if the operation is already done.
	if op.proto.GetDone() {
		return nil, nil
	}
	// Only allow a single active stream per operation.
	if op.responseStream != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "operation %q already has an active response stream", operationName)
	}
	return op, nil
}

// closeResponseStream closes the response stream for the given operation.
func (s *CodeExecutionService) closeResponseStream(op *operation) {
	s.operationsMutex.Lock()
	defer s.operationsMutex.Unlock()
	if op.responseStream != nil {
		close(op.responseStream)
		op.responseStream = nil
	}
}

func (s *CodeExecutionService) StreamExecuteCodeResponse(request *codeexecutionservicepb.StreamExecuteCodeResponseRequest, stream codeexecutionservicegrpcpb.CodeExecutionService_StreamExecuteCodeResponseServer) error {
	op, err := s.operationForResponseStream(request.GetOperationName())
	if err != nil {
		return err
	}
	if op == nil {
		return nil
	}
	defer s.closeResponseStream(op)
	// Send all responses collected so far.
	s.operationsMutex.Lock()
	for _, response := range op.responses {
		if err := stream.Send(response); err != nil {
			s.operationsMutex.Unlock()
			return err
		}
	}
	// Create and assign the response stream. This must happen immediately before
	// unlocking and starting the read loop below so that attempts to write into
	// the response stream channel will never block with a locked mutex.
	responseStream := make(chan *codeexecutionservicepb.StreamExecuteCodeResponseResponse)
	op.responseStream = responseStream
	s.operationsMutex.Unlock()
	for {
		select {
		case response := <-op.responseStream:
			if err := stream.Send(response); err != nil {
				return err
			}
		case <-op.finished:
			return nil
		case <-stream.Context().Done():
			return nil
		}
	}
}

func (s *CodeExecutionService) GetPythonTemplate(ctx context.Context, request *codeexecutioninfoservicepb.GetPythonTemplateRequest) (*codeexecutioninfoservicepb.GetPythonTemplateResponse, error) {
	codeTemplate, err := pycode.GenerateCodeForDisplay(pycode.GenerateCodeForDisplayOptions{
		FunctionBody:               request.GetPythonCode().GetFunctionBody(), // can be ""
		FileDescriptorSet:          request.FileDescriptorSet,                 // can be nil
		ParameterMessageFullName:   request.ParameterMessageFullName,          // can be ""
		ReturnValueMessageFullName: request.ReturnValueMessageFullName,        // can be ""
	})
	if err != nil {
		return nil, fmt.Errorf("Failed to generate code template: %w", err)
	}

	return &codeexecutioninfoservicepb.GetPythonTemplateResponse{CodeTemplate: codeTemplate}, nil
}

// ResetKernels signals all workers to restart their Jupyter kernels.
func (s *CodeExecutionService) ResetKernels(ctx context.Context, request *codeexecutionservicepb.ResetKernelsRequest) (*codeexecutionservicepb.ResetKernelsResponse, error) {
	log.InfoContextf(ctx, "Received request to restart all Jupyter kernels")
	for i := range s.numJupyterSessions {
		select {
		case s.kernelResetChs[i] <- struct{}{}:
		default:
			// channel already contains a reset kernel message, nothing to do
		}
	}
	return &codeexecutionservicepb.ResetKernelsResponse{}, nil
}
