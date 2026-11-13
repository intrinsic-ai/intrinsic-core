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

// Package proxy provides tools to create an operations server that proxies to
// many different backends.
package proxy

import (
	"context"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/emptypb"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

type ProxySet struct {
	proxies []lrogrpcpb.OperationsClient
}

// NewProxySet creates a new ProxySet implementing lrogrpcpb.OperationsServer.
func NewProxySet(proxies []lrogrpcpb.OperationsClient) lrogrpcpb.OperationsServer {
	return &ProxySet{proxies: proxies}
}

func (s *ProxySet) ListOperations(ctx context.Context, req *lropb.ListOperationsRequest) (*lropb.ListOperationsResponse, error) {
	mergedResponse := &lropb.ListOperationsResponse{}
	for _, p := range s.proxies {
		resp, err := p.ListOperations(ctx, req)
		if err != nil {
			if status.Code(err) != codes.Unimplemented {
				return nil, err
			}
			continue
		}
		proto.Merge(mergedResponse, resp)
	}
	return mergedResponse, nil
}

func isNotFoundOrUnimplemented(err error) bool {
	code := status.Code(err)
	return code == codes.NotFound || code == codes.Unimplemented
}

func (s *ProxySet) GetOperation(ctx context.Context, req *lropb.GetOperationRequest) (*lropb.Operation, error) {
	for _, p := range s.proxies {
		resp, err := p.GetOperation(ctx, req)
		if err == nil {
			return resp, nil
		}
		if !isNotFoundOrUnimplemented(err) {
			return nil, err
		}
	}
	return nil, status.Errorf(codes.NotFound, "Operation %q not found", req.GetName())
}

func (s *ProxySet) DeleteOperation(ctx context.Context, req *lropb.DeleteOperationRequest) (*emptypb.Empty, error) {
	for _, p := range s.proxies {
		resp, err := p.DeleteOperation(ctx, req)
		if err == nil {
			return resp, nil
		}
		if !isNotFoundOrUnimplemented(err) {
			return nil, err
		}
	}
	return nil, status.Errorf(codes.NotFound, "Cannot delete unknown operation %q", req.GetName())
}

func (s *ProxySet) CancelOperation(ctx context.Context, req *lropb.CancelOperationRequest) (*emptypb.Empty, error) {
	for _, p := range s.proxies {
		resp, err := p.CancelOperation(ctx, req)
		if err == nil {
			return resp, nil
		}
		if !isNotFoundOrUnimplemented(err) {
			return nil, err
		}
	}
	return nil, status.Errorf(codes.NotFound, "Cannot cancel unknown operation %q", req.GetName())
}

func (s *ProxySet) WaitOperation(ctx context.Context, req *lropb.WaitOperationRequest) (*lropb.Operation, error) {
	for _, p := range s.proxies {
		resp, err := p.WaitOperation(ctx, req)
		if err == nil {
			return resp, nil
		}
		if !isNotFoundOrUnimplemented(err) {
			return nil, err
		}
	}
	return nil, status.Errorf(codes.NotFound, "Operation %q not found", req.GetName())
}
