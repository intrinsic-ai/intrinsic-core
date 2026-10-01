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

package any

import (
	"context"
	"errors"
	"fmt"
	"math"
	"strings"
	"sync"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"

	"intrinsic/assets/proto/id_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
	"intrinsic/util/proto/registryutil"
)

// cacheEntry holds the resolved types and the version string for an asset.
type cacheEntry struct {
	version string
	types   *protoregistry.Types
}

// InstalledAssetsResolver implements protoregistry.MessageTypeResolver, using descriptors fetched from the InstalledAssets service.
type InstalledAssetsResolver struct {
	client iagrpcpb.InstalledAssetsClient
	// Key: id of installed asset, value: cacheEntry struct.
	typeCache map[string]cacheEntry
	mu        sync.RWMutex
}

// NewInstalledAssetsResolver returns a new InstalledAssetsResolver instance.
func NewInstalledAssetsResolver(installedAssetsAddress string) (*InstalledAssetsResolver, error) {
	if installedAssetsAddress == "" {
		return nil, errors.New("installedAssetsAddress must not be empty")
	}
	options := []grpc.DialOption{
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(math.MaxInt32)),
	}
	grpcClient, err := grpc.NewClient(installedAssetsAddress, options...)
	if err != nil {
		return nil, fmt.Errorf("grpc.NewClient(%q) failed: %w", installedAssetsAddress, err)
	}

	return &InstalledAssetsResolver{
		client:    iagrpcpb.NewInstalledAssetsClient(grpcClient),
		typeCache: make(map[string]cacheEntry),
	}, nil
}

// FindMessageByName looks up a message by its full name.
// E.g., "google.protobuf.Any"
//
// This returns (nil, NotFound) if not found.
func (r *InstalledAssetsResolver) FindMessageByName(message protoreflect.FullName) (protoreflect.MessageType, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	for _, entry := range r.typeCache {
		mt, err := entry.types.FindMessageByName(message)
		if err == nil {
			return mt, nil
		}
	}
	return nil, protoregistry.NotFound
}

// FindMessageByURL looks up a message by a URL identifier.
// See documentation on google.protobuf.Any.type_url for the URL format.
//
// This returns (nil, NotFound) if not found.
func (r *InstalledAssetsResolver) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	// Strip everything up to the last `/`, becaues no official protobuf library uses it.
	// https://github.com/protocolbuffers/protobuf/blob/c6f77c18ed34647b5358d9522e5854637df7bea5/src/google/protobuf/any.proto#L150-L153
	name := url
	if i := strings.LastIndex(url, "/"); i >= 0 {
		name = url[i+1:]
	}

	return r.FindMessageByName(protoreflect.FullName(name))
}

// FindExtensionByName looks up an extension field by the field's full name.
func (r *InstalledAssetsResolver) FindExtensionByName(field protoreflect.FullName) (protoreflect.ExtensionType, error) {
	return nil, fmt.Errorf("InstalledAssetsResolver.FindExtensionByName(%s) not supported: %w", field, protoregistry.NotFound)
}

// FindExtensionByNumber looks up an extension field by the field number.
func (r *InstalledAssetsResolver) FindExtensionByNumber(message protoreflect.FullName, field protoreflect.FieldNumber) (protoreflect.ExtensionType, error) {
	return nil, fmt.Errorf("InstalledAssetsResolver.FindExtensionByNumber(%s, %d) not supported: %w", message, field, protoregistry.NotFound)
}

func (r *InstalledAssetsResolver) RefreshInstalledAssets() error {
	idsToFetch := []*id_go_proto.Id{}
	ctx := context.Background()

	// List all installed assets to see if there are any new ones
	var allListedAssets []*iagrpcpb.InstalledAsset
	nextPageToken := ""
	for {
		req := &iagrpcpb.ListInstalledAssetsRequest{
			View:      viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC,
			PageToken: nextPageToken,
		}
		result, err := r.client.ListInstalledAssets(ctx, req)
		if err != nil {
			return fmt.Errorf("failed to list installed assets: %w", err)
		}
		allListedAssets = append(allListedAssets, result.GetInstalledAssets()...)
		nextPageToken = result.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}

	r.mu.RLock()
	for _, asset := range allListedAssets {
		id := asset.GetMetadata().GetIdVersion().GetId()
		idString := id.GetPackage() + "." + id.GetName()
		newVersionStr := asset.GetMetadata().GetIdVersion().GetVersion()

		// if the id is not in r.typeCache, then add it to idsToFetch
		existingEntry, exists := r.typeCache[idString]
		if !exists {
			idsToFetch = append(idsToFetch, id)
			continue
		}

		// Fetch this asset if the version is different.
		if existingEntry.version != newVersionStr {
			idsToFetch = append(idsToFetch, id)
		}
	}
	r.mu.RUnlock()

	if len(idsToFetch) == 0 {
		return nil
	}

	// Get all metadata for new assets and cache it
	batchReq := &iagrpcpb.BatchGetInstalledAssetsRequest{
		Ids:  idsToFetch,
		View: viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL_METADATA,
	}

	batchResp, err := r.client.BatchGetInstalledAssets(ctx, batchReq)
	if err != nil {
		return fmt.Errorf("failed to batch get installed assets: %w", err)
	}

	r.mu.Lock()
	defer r.mu.Unlock()
	for _, asset := range batchResp.GetInstalledAssets() {
		id := asset.GetMetadata().GetIdVersion().GetId()
		idString := id.GetPackage() + "." + id.GetName()
		version := asset.GetMetadata().GetIdVersion().GetVersion()
		fds := asset.GetMetadata().GetFileDescriptorSet()

		if fds == nil {
			continue
		}

		_, err = r.setCachedAssetDescriptorLocked(idString, version, fds)
		if err != nil {
			return fmt.Errorf("failed to set cached descriptor for asset %s: %w", id, err)
		}
	}

	return nil
}

func (r *InstalledAssetsResolver) setCachedAssetDescriptorLocked(id string, version string, fileDescriptorSet *descriptorpb.FileDescriptorSet) (*protoregistry.Types, error) {
	files := new(protoregistry.Files)
	types := new(protoregistry.Types)

	for _, fdProto := range fileDescriptorSet.GetFile() {
		// NewFile checks dependencies. We pass 'files' as the resolver so it can
		// resolve dependencies that were processed in previous iterations of this loop.
		file, err := protodesc.NewFile(fdProto, files)
		if err != nil {
			return nil, fmt.Errorf("failed to create file descriptor: %w", err)
		}

		err = files.RegisterFile(file)
		if err != nil {
			return nil, fmt.Errorf("failed to register file: %w", err)
		}
	}

	if err := registryutil.PopulateTypesFromFiles(types, files); err != nil {
		return nil, fmt.Errorf("failed to populate types: %w", err)
	}

	// Insert into cache
	r.typeCache[id] = cacheEntry{
		version: version,
		types:   types,
	}

	return types, nil
}
