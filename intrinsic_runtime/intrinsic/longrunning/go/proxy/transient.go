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

package proxy

import (
	"context"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/types/known/emptypb"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

type transientProxy struct {
	client lrogrpcpb.OperationsClient
}

// NewTransientProxy wraps an existing OperationsClient to coerce UNAVAILABLE errors.
func NewTransientProxy(client lrogrpcpb.OperationsClient) lrogrpcpb.OperationsClient {
	return &transientProxy{client: client}
}

func coerceUnavailableToOk(resp *lropb.ListOperationsResponse, err error) (*lropb.ListOperationsResponse, error) {
	if status.Code(err) == codes.Unavailable {
		return &lropb.ListOperationsResponse{}, nil
	}
	return resp, err
}

func coerceUnavailableToNotFound(err error) error {
	if status.Code(err) == codes.Unavailable {
		return status.Error(codes.NotFound, status.Convert(err).Message())
	}
	return err
}

func (p *transientProxy) ListOperations(ctx context.Context, req *lropb.ListOperationsRequest, opts ...grpc.CallOption) (*lropb.ListOperationsResponse, error) {
	resp, err := p.client.ListOperations(ctx, req, opts...)
	return coerceUnavailableToOk(resp, err)
}

func (p *transientProxy) GetOperation(ctx context.Context, req *lropb.GetOperationRequest, opts ...grpc.CallOption) (*lropb.Operation, error) {
	resp, err := p.client.GetOperation(ctx, req, opts...)
	err = coerceUnavailableToNotFound(err)
	return resp, err
}

func (p *transientProxy) DeleteOperation(ctx context.Context, req *lropb.DeleteOperationRequest, opts ...grpc.CallOption) (*emptypb.Empty, error) {
	resp, err := p.client.DeleteOperation(ctx, req, opts...)
	err = coerceUnavailableToNotFound(err)
	return resp, err
}

func (p *transientProxy) CancelOperation(ctx context.Context, req *lropb.CancelOperationRequest, opts ...grpc.CallOption) (*emptypb.Empty, error) {
	resp, err := p.client.CancelOperation(ctx, req, opts...)
	err = coerceUnavailableToNotFound(err)
	return resp, err
}

func (p *transientProxy) WaitOperation(ctx context.Context, req *lropb.WaitOperationRequest, opts ...grpc.CallOption) (*lropb.Operation, error) {
	resp, err := p.client.WaitOperation(ctx, req, opts...)
	err = coerceUnavailableToNotFound(err)
	return resp, err
}
