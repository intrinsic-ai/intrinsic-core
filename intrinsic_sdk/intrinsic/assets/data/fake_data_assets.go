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

// Package fakedataassets provides a fake DataAssets service.
package fakedataassets

import (
	"context"
	"slices"
	"strings"
	"testing"

	"intrinsic/assets/idutils"
	"intrinsic/testing/grpctest"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/local"
	"google.golang.org/grpc/status"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dasgrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	daspb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	mpb "intrinsic/assets/proto/metadata_go_proto"
)

const defaultPageSize = 20

// Service is a fake implementation of the DataAssets service.
type Service struct {
	dataAssets map[string]*dapb.DataAsset
}

func (s *Service) ListDataAssets(ctx context.Context, req *daspb.ListDataAssetsRequest) (*daspb.ListDataAssetsResponse, error) {
	var filteredAssets []*dapb.DataAsset
	for _, da := range s.dataAssets {
		if req.GetStrictFilter().GetProtoName() != "" {
			typeURL := da.GetData().GetTypeUrl()
			if !strings.HasSuffix(typeURL, req.GetStrictFilter().GetProtoName()) {
				continue
			}
		}
		filteredAssets = append(filteredAssets, da)
	}

	// Sort by ID for consistent pagination.
	slices.SortFunc(filteredAssets, func(a, b *dapb.DataAsset) int {
		idA := idutils.IDFromProtoUnchecked(a.GetMetadata().GetIdVersion().GetId())
		idB := idutils.IDFromProtoUnchecked(b.GetMetadata().GetIdVersion().GetId())
		return strings.Compare(idA, idB)
	})

	// Determine the start of the page.
	offset := 0
	if req.GetPageToken() != "" {
		assetFound := false
		for i, da := range filteredAssets {
			id := idutils.IDFromProtoUnchecked(da.GetMetadata().GetIdVersion().GetId())
			if id == req.GetPageToken() {
				assetFound = true
				offset = i
				break
			}
		}
		if !assetFound {
			return nil, status.Errorf(codes.InvalidArgument, "invalid page token: %q", req.GetPageToken())
		}
	}

	pageSize := defaultPageSize
	if req.GetPageSize() > 0 {
		pageSize = int(req.GetPageSize())
	}
	lastIndex := min(offset+pageSize, len(filteredAssets)) - 1

	nextPageToken := ""
	if lastIndex < len(filteredAssets)-1 {
		nextPageToken = idutils.IDFromProtoUnchecked(filteredAssets[lastIndex+1].GetMetadata().GetIdVersion().GetId())
	}

	return &daspb.ListDataAssetsResponse{
		DataAssets:    filteredAssets[offset : lastIndex+1],
		NextPageToken: nextPageToken,
	}, nil
}

func (s *Service) ListDataAssetMetadata(ctx context.Context, req *daspb.ListDataAssetMetadataRequest) (*daspb.ListDataAssetMetadataResponse, error) {
	listRequest := &daspb.ListDataAssetsRequest{
		StrictFilter: req.GetStrictFilter(),
		PageSize:     req.GetPageSize(),
		PageToken:    req.GetPageToken(),
	}
	listResponse, err := s.ListDataAssets(ctx, listRequest)
	if err != nil {
		return nil, err
	}

	metadata := make([]*mpb.Metadata, len(listResponse.GetDataAssets()))
	for i, da := range listResponse.GetDataAssets() {
		metadata[i] = da.GetMetadata()
	}
	return &daspb.ListDataAssetMetadataResponse{
		Metadata:      metadata,
		NextPageToken: listResponse.GetNextPageToken(),
	}, nil
}

func (s *Service) GetDataAsset(ctx context.Context, req *daspb.GetDataAssetRequest) (*dapb.DataAsset, error) {
	id := idutils.IDFromProtoUnchecked(req.GetId())
	da, ok := s.dataAssets[id]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "data asset not found: %q", id)
	}
	return da, nil
}

func (s *Service) StreamReferencedData(req *daspb.StreamReferencedDataRequest, stream dasgrpcpb.DataAssets_StreamReferencedDataServer) error {
	return status.Errorf(codes.Unimplemented, "not implemented")
}

// Fake provides a fake DataAssets service.
type Fake struct {
	Client  dasgrpcpb.DataAssetsClient
	Service *Service
	Server  *grpc.Server
}

type startServerOpts struct {
	dataAssets []*dapb.DataAsset
}

// StartServerOpt is a functional option for StartServer.
type StartServerOpt func(*startServerOpts)

// WithDataAssets returns a StartServerOpt that sets the data assets.
func WithDataAssets(dataAssets []*dapb.DataAsset) StartServerOpt {
	return func(opts *startServerOpts) {
		opts.dataAssets = dataAssets
	}
}

// StartServer creates a gRPC server with the DataAssets service registered.
func StartServer(ctx context.Context, t *testing.T, options ...StartServerOpt) *Fake {
	t.Helper()

	opts := startServerOpts{}
	for _, opt := range options {
		opt(&opts)
	}

	dataAssetsMap := make(map[string]*dapb.DataAsset)
	for _, da := range opts.dataAssets {
		id := idutils.IDFromProtoUnchecked(da.GetMetadata().GetIdVersion().GetId())
		if _, ok := dataAssetsMap[id]; ok {
			t.Fatalf("Duplicate data asset ID: %v", id)
		}
		dataAssetsMap[id] = da
	}

	server := grpc.NewServer()
	service := &Service{
		dataAssets: dataAssetsMap,
	}
	dasgrpcpb.RegisterDataAssetsServer(server, service)

	srvAddr := grpctest.StartServerT(t, server)
	conn, err := grpc.NewClient(srvAddr, grpc.WithTransportCredentials(local.NewCredentials()))
	if err != nil {
		t.Fatalf("failed to dial test server: %v", err)
	}
	t.Cleanup(func() { conn.Close() })

	return &Fake{
		Client:  dasgrpcpb.NewDataAssetsClient(conn),
		Service: service,
		Server:  server,
	}
}
