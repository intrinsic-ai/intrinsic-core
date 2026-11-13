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

	"intrinsic/util/pagination/pagetoken"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	epb "google.golang.org/protobuf/types/known/emptypb"
)

const (
	listMaxPageSize           = 5
	operationsDefaultPageSize = 50
	operationsMaxPageSize     = 50
)

var errOperationCanceledManually = errors.New("canceled manually")

type server struct {
	ops *Map
}

func validatePageSize(reqPageSize, defaultPageSize, maxPageSize int32) (int32, error) {
	if reqPageSize == 0 {
		return defaultPageSize, nil
	}
	if reqPageSize < 0 {
		return 0, fmt.Errorf("must be non-negative")
	}
	if reqPageSize > listMaxPageSize {
		return maxPageSize, nil
	}
	return reqPageSize, nil
}

func (s *server) ListOperations(ctx context.Context, req *lropb.ListOperationsRequest) (*lropb.ListOperationsResponse, error) {
	if req.GetName() != "" {
		return nil, status.Error(codes.InvalidArgument, "name must not be specified")
	}

	pageSize, err := validatePageSize(req.GetPageSize(), operationsDefaultPageSize, operationsMaxPageSize)
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "page_size is invalid: %v", err)
	}

	pt, err := pagetoken.Deopacify(req.GetPageToken())
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "page_token is invalid: %v", err)
	}
	if req.GetPageToken() != "" {
		pageTokenReq := &lropb.ListOperationsRequest{}
		if err := pagetoken.RecoverRequest(pt, pageTokenReq); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "page_token is invalid: %v", err)
		}
		if pageTokenReq.GetFilter() != req.GetFilter() {
			return nil, status.Error(codes.InvalidArgument, "page_token is invalid for the current request")
		}
	}

	opFilter, err := ParseFilterString(req.GetFilter())
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "ParseFilterString(%q) failed: %v", req.GetFilter(), err)
	}

	var filteredOps []*lropb.Operation
	for _, op := range s.ops.GetAll() {
		if opFilter.Match(op) {
			filteredOps = append(filteredOps, op.Proto())
		}
	}

	pageStart := 0
	if pt.GetStartAfterId() != "" {
		for idx, op := range filteredOps {
			if op.GetName() == pt.GetStartAfterId() {
				pageStart = idx + 1
				break
			}
		}
	}
	if pageStart >= len(filteredOps) {
		return &lropb.ListOperationsResponse{
			Operations: []*lropb.Operation{},
		}, nil
	}

	var nextPageToken string
	pageEnd := min(pageStart+int(pageSize), len(filteredOps))
	if pageEnd < len(filteredOps) {
		startAfterID := filteredOps[pageEnd-1].GetName()
		req.PageToken = ""
		if nextPageToken, err = pagetoken.Opacify(req, startAfterID, []string{}); err != nil {
			return nil, status.Errorf(codes.Internal, "pagetoken.Opacify failed: %v", err)
		}
	}

	return &lropb.ListOperationsResponse{
		Operations:    filteredOps[pageStart:pageEnd],
		NextPageToken: nextPageToken,
	}, nil
}

func (s *server) GetOperation(ctx context.Context, req *lropb.GetOperationRequest) (*lropb.Operation, error) {
	op := s.ops.Get(req.GetName())
	if op == nil {
		return nil, status.Errorf(codes.NotFound, "operation %q not found", req.GetName())
	}
	return op.Proto(), nil
}

func (s *server) DeleteOperation(ctx context.Context, req *lropb.DeleteOperationRequest) (*epb.Empty, error) {
	if ok := s.ops.Delete(req.GetName()); !ok {
		return nil, status.Errorf(codes.NotFound, "operation %q not found", req.GetName())
	}
	return &epb.Empty{}, nil
}

func (s *server) CancelOperation(ctx context.Context, req *lropb.CancelOperationRequest) (*epb.Empty, error) {
	op := s.ops.Get(req.GetName())
	if op == nil {
		return nil, status.Errorf(codes.NotFound, "operation %q not found", req.GetName())
	}
	if op.Done() {
		return nil, status.Errorf(codes.FailedPrecondition, "operation %q is already done", req.GetName())
	}
	op.Cancel(errOperationCanceledManually)
	return &epb.Empty{}, nil
}

func (s *server) WaitOperation(ctx context.Context, req *lropb.WaitOperationRequest) (*lropb.Operation, error) {
	op := s.ops.Get(req.GetName())
	if op == nil {
		return nil, status.Errorf(codes.NotFound, "operation %q not found", req.GetName())
	}
	if timeout := req.GetTimeout(); timeout != nil {
		op.WaitFor(timeout.AsDuration())
	} else {
		op.Wait()
	}
	return op.Proto(), nil
}

// NewServer constructs a gRPC service to handle long-running operations.
func NewServer(ops *Map) lrogrpcpb.OperationsServer {
	return &server{ops}
}
