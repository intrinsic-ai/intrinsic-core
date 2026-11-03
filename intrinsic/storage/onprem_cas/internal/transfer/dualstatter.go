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

package transfer

import (
	"context"
	"fmt"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"

	"log/slog"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// StatResult represents the detailed result of a stat operation.
type StatResult struct {
	Locally  bool
	Upstream bool
	Size     uint64
	// Err is set if the async operation failed.
	Err error
}

// dualStatter manages querying the status (existence and size) of objects
// in local and upstream CAS services, using an LRU cache for upstream status.
type dualStatter struct {
	upstreamClient caspb.ContentAddressableStorageServiceClient
	localClient    caspb.ContentAddressableStorageServiceClient
	upstreamCache  *upstreamCache
}

// newDualStatter creates a new dualStatter with the given clients and cache size.
func newDualStatter(
	upstreamClient caspb.ContentAddressableStorageServiceClient,
	localClient caspb.ContentAddressableStorageServiceClient,
	cacheSize int,
	cacheDir string,
) (*dualStatter, error) {
	cache, err := newUpstreamCache(cacheSize, cacheDir)
	if err != nil {
		return nil, fmt.Errorf("failed to initialize upstream LRU cache: %w", err)
	}
	return &dualStatter{
		upstreamClient: upstreamClient,
		localClient:    localClient,
		upstreamCache:  cache,
	}, nil
}

// stat queries the local and upstream CAS services for the object.
func (s *dualStatter) stat(ctx context.Context, digest string) (*StatResult, error) {
	res, err := s.statImpl(ctx, digest)
	if err != nil {
		return nil, err
	}
	slog.Debug("DualStatter: Stat results", slog.String("digest", digest),
		slog.Bool("locally", res.Locally), slog.Bool("upstream", res.Upstream),
		slog.Uint64("size", res.Size))
	return res, nil
}

func (s *dualStatter) statImpl(ctx context.Context, digest string) (*StatResult, error) {
	objectID, err := idhandling.AddSchema(digest)
	if err != nil {
		return nil, fmt.Errorf("invalid digest: %w", err)
	}

	// verify local existence
	locally := false
	var size uint64
	statRes, err := s.localClient.Stat(ctx, &caspb.StatRequest{ObjectId: objectID})
	if err != nil && status.Code(err) != codes.NotFound { // unknown local error, abort
		return nil, fmt.Errorf("local stat error: %w", err)
	}
	if err == nil { // NO error (object exists locally)
		if valErr := idhandling.ValidateObjectSize(digest, statRes.Size); valErr != nil {
			slog.Warn("DualStatter: local object has invalid size, treating as missing and removing",
				slog.String("digest", digest),
				slog.Int64("size", statRes.Size),
				slog.Any("error", valErr))
			if _, delErr := s.localClient.Delete(ctx, &caspb.DeleteRequest{ObjectId: objectID}); delErr != nil && status.Code(delErr) != codes.NotFound {
				slog.Warn("DualStatter: failed to delete corrupt local object", slog.String("digest", digest), slog.Any("error", delErr))
			}
		} else {
			locally = true
			size = uint64(max(0, statRes.Size))
		}
	}

	// verify upstream existence (first in LRU cache)
	upstream := false
	if cached, ok := s.upstreamCache.Get(digest); ok { // FOUND in upstream cache
		upstream = true
		if locally && size != cached.Size {
			slog.Warn("DualStatter: local object size does not match upstream cached size, removing local corrupt object",
				slog.String("digest", digest),
				slog.Uint64("local_size", size),
				slog.Uint64("upstream_size", cached.Size))
			if _, delErr := s.localClient.Delete(ctx, &caspb.DeleteRequest{ObjectId: objectID}); delErr != nil && status.Code(delErr) != codes.NotFound {
				slog.Warn("DualStatter: failed to delete corrupt local object", slog.String("digest", digest), slog.Any("error", delErr))
			}
			locally = false
		}
		if !locally {
			size = cached.Size
		}
		return &StatResult{Locally: locally, Upstream: upstream, Size: size}, nil
	}

	// not cache hit, make upstream Stat request
	recordAttempt(ctx, OpStat)
	trackActiveOperations(ctx, OpStat, 1)
	defer trackActiveOperations(ctx, OpStat, -1)

	req := &caspb.StatRequest{ObjectId: objectID}
	statRes, statErr := s.upstreamClient.Stat(ctx, req)
	if statErr != nil && status.Code(statErr) != codes.NotFound { // unknown upstream error, abort
		recordFailed(ctx, OpStat)
		return nil, fmt.Errorf("upstream stat error: %w", statErr)
	}
	recordCompleted(ctx, OpStat)

	// NO error, object was found upstream
	if statErr == nil {
		upstream = true
		upstreamSize := uint64(max(0, statRes.Size))
		if locally && size != upstreamSize {
			slog.Warn("DualStatter: local object size does not match upstream size, removing local corrupt object",
				slog.String("digest", digest),
				slog.Uint64("local_size", size),
				slog.Uint64("upstream_size", upstreamSize))
			if _, delErr := s.localClient.Delete(ctx, &caspb.DeleteRequest{ObjectId: objectID}); delErr != nil && status.Code(delErr) != codes.NotFound {
				slog.Warn("DualStatter: failed to delete corrupt local object", slog.String("digest", digest), slog.Any("error", delErr))
			}
			locally = false
		}
		if !locally { // use size for upstream if not found locally before
			size = upstreamSize
		}
		s.upstreamCache.Add(digest, &upstreamStat{Size: upstreamSize})
		// We only cache positive responses. We do not cache negative responses (NotFound)
		// based on the assumption that upstream objects never disappear, and if an object
		// is missing right now, it might be added later.
	}
	return &StatResult{Locally: locally, Upstream: upstream, Size: size}, nil
}

// RecordUpload adds the object to the upstream cache, marking it as existing upstream.
// This should be called when an object is successfully uploaded.
func (s *dualStatter) RecordUpload(digest string, size uint64) {
	s.upstreamCache.Add(digest, &upstreamStat{Size: size})
}
