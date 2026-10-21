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

// Package listutils provides utility functions for listing assets from a catalog.
package listutils

import (
	"context"
	"fmt"

	"google.golang.org/grpc"

	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

type assetLister interface {
	ListAssets(ctx context.Context, req *acpb.ListAssetsRequest, options ...grpc.CallOption) (*acpb.ListAssetsResponse, error)
}

// ListAllAssets lists all assets from a catalog that match the specified filter.
func ListAllAssets(ctx context.Context, client assetLister, pageSize int64, view viewpb.AssetViewType, filter *acpb.ListAssetsRequest_AssetFilter) ([]*acpb.Asset, error) {
	nextPageToken := ""
	var assets []*acpb.Asset
	for {
		resp, err := client.ListAssets(ctx, &acpb.ListAssetsRequest{
			View:         view,
			PageToken:    nextPageToken,
			PageSize:     pageSize,
			StrictFilter: filter,
		})
		if err != nil {
			return nil, fmt.Errorf("could not list assets: %w", err)
		}
		assets = append(assets, resp.GetAssets()...)
		nextPageToken = resp.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}
	return assets, nil
}
