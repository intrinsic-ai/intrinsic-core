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

// Package resourcetyperuntime contains client code that can read/write ResourceTypeRuntime protos
package resourcetyperuntime

import (
	"context"
	"fmt"
	"slices"
	"strings"
	"sync"
	"time"

	"intrinsic/assets/idutils"
	"intrinsic/resources/resourcefix"
	"intrinsic/util/grpc/statusutil"

	"github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	rdbgrpcpb "intrinsic/resources/proto/runtime_db_go_proto"
	rdbpb "intrinsic/resources/proto/runtime_db_go_proto"
)

const (
	bucket = "resources"
	// An arbitrary size to chunk calls to get resource type runtime data.
	// Without chunk sizes, there were some instances such as b/475802627
	// where the gRPC size limits were exceeded.
	rtrChunkSize = 10
	// Maximum number of times GetAll retries when the bucket is concurrently
	// modified between List and BatchGet.
	getAllMaxRetries     = 5
	getAllInitialBackoff = 10 * time.Millisecond
)

// batchGetOptions holds optional parameters for BatchGet.
type batchGetOptions struct {
	revisionToken string
}

// BatchGetOption configures a BatchGet call.
type BatchGetOption func(*batchGetOptions)

// WithRevisionToken specifies a revision token returned by List to assert
// that the bucket has not been modified before BatchGet reads the entries.
func WithRevisionToken(token string) BatchGetOption {
	return func(o *batchGetOptions) {
		o.revisionToken = token
	}
}

// Client contains functions to read/write ResourceTypeRuntime information.
type Client interface {
	Add(ctx context.Context, rt *rtrpb.ResourceTypeRuntime) error
	Get(ctx context.Context, idVersion string) (*rtrpb.ResourceTypeRuntime, error)
	List(ctx context.Context) ([]string, string, error)
	Delete(ctx context.Context, idVersion string) error
	Clear(ctx context.Context) error
	BatchPut(ctx context.Context, rts []*rtrpb.ResourceTypeRuntime) error
	BatchGet(ctx context.Context, idVersions []string, opts ...BatchGetOption) ([]*rtrpb.ResourceTypeRuntime, error)
	BatchDelete(ctx context.Context, idVersions []string) error
}

// Cache contains functions to manage the in-memory cache of ResourceTypeRuntime protos.
type Cache interface {
	Evict(idVersions ...string)
}

// CachedClient contains functions to read/write ResourceTypeRuntime information
// using an in-memory cache.
type CachedClient interface {
	Client
	Cache
}

type runtimeDBBackedClient struct {
	client rdbgrpcpb.RuntimeDbClient
}

func (s *runtimeDBBackedClient) Add(ctx context.Context, rtr *rtrpb.ResourceTypeRuntime) error {
	idVersion, err := idutils.IDVersionFromProto(rtr.GetMetadata().GetIdVersion())
	if err != nil {
		return statusutil.NewInternalError("could not convert ID version proto to ID version: %v", err)
	}
	// We make a copy to avoid mutating the input.
	rtr = proto.Clone(rtr).(*rtrpb.ResourceTypeRuntime)
	if err := resourcefix.ResourceTypeRuntime(rtr, resourcefix.WithPopulateOldFields(true)); err != nil {
		// Ignore errors to prevent a failing asset runtime from bricking the solution.
		// Note that the deploy service directly populates the runtime DB during deployment.
		log.Errorf("Failed to fix asset %q: %v", idVersion, err)
	}
	marshalled, err := proto.Marshal(rtr)
	if err != nil {
		return statusutil.NewInternalError("could not marshal proto value")
	}

	_, err = s.client.Put(ctx,
		&rdbpb.PutRequest{
			Bucket: bucket,
			Key:    idVersion,
			Value:  marshalled,
		})
	if err != nil {
		return statusutil.Wrap(err, "could not set runtime resource type")
	}
	return nil
}

func (s *runtimeDBBackedClient) Get(ctx context.Context, idVersion string) (*rtrpb.ResourceTypeRuntime, error) {
	resp, err := s.client.Get(ctx,
		&rdbpb.GetRequest{
			Bucket: bucket,
			Key:    idVersion,
		})
	if err != nil {
		return nil, statusutil.Wrap(err, "could not retrieve resource runtime info for %q", idVersion)
	}
	unmarshalled := &rtrpb.ResourceTypeRuntime{}
	if err := proto.Unmarshal(resp.GetValue(), unmarshalled); err != nil {
		return nil, statusutil.NewInternalError("retrieved but could not unmarshal value for key %q", idVersion)
	}
	return unmarshalled, nil
}

func (s *runtimeDBBackedClient) List(ctx context.Context) ([]string, string, error) {
	resp, err := s.client.List(ctx, &rdbpb.ListRequest{Bucket: bucket})
	if err != nil {
		return nil, "", statusutil.Wrap(err, "could not retrieve all resource runtime infos: %v")
	}
	return resp.GetKeys(), resp.GetRevisionToken(), nil
}

func (s *runtimeDBBackedClient) Delete(ctx context.Context, idVersion string) error {
	_, err := s.client.Delete(ctx, &rdbpb.DeleteRequest{Bucket: bucket, Key: idVersion})
	if err != nil {
		return statusutil.Wrap(err, "could not delete resource runtime info for %q", idVersion)
	}
	return nil
}

func (s *runtimeDBBackedClient) Clear(ctx context.Context) error {
	_, err := s.client.Clear(ctx, &rdbpb.ClearRequest{Bucket: bucket})
	if err != nil {
		return statusutil.Wrap(err, "could not clear all resource runtime infos")
	}
	return nil
}

func (s *runtimeDBBackedClient) BatchPut(ctx context.Context, rts []*rtrpb.ResourceTypeRuntime) error {
	values := make(map[string][]byte, len(rts))
	for _, rt := range rts {
		idVersion, err := idutils.IDVersionFromProto(rt.GetMetadata().GetIdVersion())
		if err != nil {
			return statusutil.NewInternalError("could not convert ID version proto to ID version: %v", err)
		}
		// We make a copy to avoid mutating the input.
		rt = proto.Clone(rt).(*rtrpb.ResourceTypeRuntime)
		if err := resourcefix.ResourceTypeRuntime(rt, resourcefix.WithPopulateOldFields(true)); err != nil {
			// Ignore errors to prevent a failing asset runtime from bricking the solution.
			// Note that the deploy service directly populates the runtime DB during deployment.
			log.Errorf("Failed to fix asset %q: %v", idVersion, err)
		}
		marshalled, err := proto.Marshal(rt)
		if err != nil {
			return statusutil.NewInternalError("could not marshal proto value for %q", idVersion)
		}
		values[idVersion] = marshalled
	}
	if _, err := s.client.BatchPut(ctx, &rdbpb.BatchPutRequest{
		Bucket: bucket,
		Values: values,
	}); err != nil {
		return statusutil.Wrap(err, "could not set runtime resource type")
	}
	return nil
}

func (s *runtimeDBBackedClient) BatchGet(ctx context.Context, idVersions []string, opts ...BatchGetOption) ([]*rtrpb.ResourceTypeRuntime, error) {
	o := &batchGetOptions{}
	for _, opt := range opts {
		opt(o)
	}
	resp, err := s.client.BatchGet(ctx, &rdbpb.BatchGetRequest{
		Bucket:        bucket,
		Keys:          idVersions,
		RevisionToken: o.revisionToken,
	})
	if err != nil {
		return nil, statusutil.Wrap(err, "could not retrieve resource runtime info")
	}
	var rts []*rtrpb.ResourceTypeRuntime
	if len(idVersions) > 0 {
		rts = make([]*rtrpb.ResourceTypeRuntime, 0, len(idVersions))
	}
	for _, k := range idVersions {
		v, ok := resp.GetValues()[k]
		if !ok {
			return nil, statusutil.NewInternalError("ID version %q not contained in successful response", k)
		}
		unmarshalled := &rtrpb.ResourceTypeRuntime{}
		if err := proto.Unmarshal(v, unmarshalled); err != nil {
			return nil, statusutil.NewInternalError("retrieved but could not unmarshal value for key %q", k)
		}
		rts = append(rts, unmarshalled)
	}
	return rts, nil
}

func (s *runtimeDBBackedClient) BatchDelete(ctx context.Context, idVersions []string) error {
	_, err := s.client.BatchDelete(ctx, &rdbpb.BatchDeleteRequest{
		Bucket: bucket,
		Keys:   idVersions,
	})
	if err != nil {
		return statusutil.Wrap(err, "could not delete resource runtime info")
	}
	return nil
}

type cachedRuntimeDBBackedClient struct {
	backing *runtimeDBBackedClient
	mu      sync.RWMutex
	cache   map[string]*rtrpb.ResourceTypeRuntime
}

func (s *cachedRuntimeDBBackedClient) Add(ctx context.Context, rtr *rtrpb.ResourceTypeRuntime) error {
	if err := s.backing.Add(ctx, rtr); err != nil {
		return err
	}
	rtrCopy := proto.Clone(rtr).(*rtrpb.ResourceTypeRuntime)
	_ = resourcefix.ResourceTypeRuntime(rtrCopy, resourcefix.WithPopulateOldFields(true))
	s.mu.Lock()
	s.cache[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtrCopy
	s.mu.Unlock()
	return nil
}

func (s *cachedRuntimeDBBackedClient) Get(ctx context.Context, idVersion string) (*rtrpb.ResourceTypeRuntime, error) {
	s.mu.RLock()
	if rt, ok := s.cache[idVersion]; ok {
		s.mu.RUnlock()
		return proto.Clone(rt).(*rtrpb.ResourceTypeRuntime), nil
	}
	s.mu.RUnlock()

	rt, err := s.backing.Get(ctx, idVersion)
	if err != nil {
		return nil, err
	}
	s.mu.Lock()
	s.cache[idVersion] = proto.Clone(rt).(*rtrpb.ResourceTypeRuntime)
	s.mu.Unlock()
	return rt, nil
}

func (s *cachedRuntimeDBBackedClient) List(ctx context.Context) ([]string, string, error) {
	return s.backing.List(ctx)
}

func (s *cachedRuntimeDBBackedClient) Delete(ctx context.Context, idVersion string) error {
	if err := s.backing.Delete(ctx, idVersion); err != nil {
		return err
	}
	s.mu.Lock()
	delete(s.cache, idVersion)
	s.mu.Unlock()
	return nil
}

func (s *cachedRuntimeDBBackedClient) Clear(ctx context.Context) error {
	if err := s.backing.Clear(ctx); err != nil {
		return err
	}
	s.mu.Lock()
	s.cache = make(map[string]*rtrpb.ResourceTypeRuntime)
	s.mu.Unlock()
	return nil
}

func (s *cachedRuntimeDBBackedClient) BatchPut(ctx context.Context, rts []*rtrpb.ResourceTypeRuntime) error {
	if err := s.backing.BatchPut(ctx, rts); err != nil {
		return err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, rt := range rts {
		rtCopy := proto.Clone(rt).(*rtrpb.ResourceTypeRuntime)
		_ = resourcefix.ResourceTypeRuntime(rtCopy, resourcefix.WithPopulateOldFields(true))
		s.cache[idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion())] = rtCopy
	}
	return nil
}

func (s *cachedRuntimeDBBackedClient) BatchGet(ctx context.Context, idVersions []string, opts ...BatchGetOption) ([]*rtrpb.ResourceTypeRuntime, error) {
	if len(idVersions) == 0 {
		return nil, nil
	}

	o := &batchGetOptions{}
	for _, opt := range opts {
		opt(o)
	}

	resultsMap := make(map[string]*rtrpb.ResourceTypeRuntime, len(idVersions))
	var missingKeys []string
	seenMissing := make(map[string]struct{})
	s.mu.RLock()
	for _, idv := range idVersions {
		if rt, ok := s.cache[idv]; ok {
			resultsMap[idv] = rt
		} else if _, seen := seenMissing[idv]; !seen {
			seenMissing[idv] = struct{}{}
			missingKeys = append(missingKeys, idv)
		}
	}
	s.mu.RUnlock()

	if len(missingKeys) > 0 {
		fetched, err := s.backing.BatchGet(ctx, missingKeys, opts...)
		if err != nil {
			return nil, err
		}
		s.mu.Lock()
		for i, k := range missingKeys {
			s.cache[k] = proto.Clone(fetched[i]).(*rtrpb.ResourceTypeRuntime)
		}
		s.mu.Unlock()
		for i, k := range missingKeys {
			resultsMap[k] = fetched[i]
		}
	} else if o.revisionToken != "" {
		// Validate the revision token against the backing store even when all keys are cached.
		if _, err := s.backing.BatchGet(ctx, nil, opts...); err != nil {
			return nil, err
		}
	}

	res := make([]*rtrpb.ResourceTypeRuntime, len(idVersions))
	for i, idv := range idVersions {
		res[i] = proto.Clone(resultsMap[idv]).(*rtrpb.ResourceTypeRuntime)
	}
	return res, nil
}

// Evict removes the specified resource type runtime ID-versions from the in-memory cache.
func (s *cachedRuntimeDBBackedClient) Evict(idVersions ...string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, idv := range idVersions {
		delete(s.cache, idv)
	}
}

func (s *cachedRuntimeDBBackedClient) BatchDelete(ctx context.Context, idVersions []string) error {
	if err := s.backing.BatchDelete(ctx, idVersions); err != nil {
		return err
	}
	s.mu.Lock()
	for _, idv := range idVersions {
		delete(s.cache, idv)
	}
	s.mu.Unlock()
	return nil
}

// CreateClient creates a new runtime-db-backed client that can read/write
// ResourceTypeRuntime protos. This contains a subset of the full ResourceType
// data that is available in the resource catalog. The reason we don't just use
// the resource catalog itself is because we need to support non-catalog-backed
// resources as well.
func CreateClient(client rdbgrpcpb.RuntimeDbClient) Client {
	return &runtimeDBBackedClient{
		client: client,
	}
}

// CreateCachedClient creates a new cached runtime-db-backed client that can read/write
// ResourceTypeRuntime protos.
func CreateCachedClient(client rdbgrpcpb.RuntimeDbClient) CachedClient {
	return &cachedRuntimeDBBackedClient{
		backing: &runtimeDBBackedClient{client: client},
		cache:   make(map[string]*rtrpb.ResourceTypeRuntime),
	}
}

// GetFromID returns a ResourceTypeRuntime proto for a given Asset ID.
//
// This function errors if multiple versions of the same Asset are
// installed in the solution.
func GetFromID(ctx context.Context, client Client, id string) (*rtrpb.ResourceTypeRuntime, error) {
	idvs, _, err := client.List(ctx)
	if err != nil {
		return nil, statusutil.Wrap(err, "could not list runtime info for assets")
	}
	var matchingIDVs []string
	for _, idv := range idvs {
		strippedIDV, err := idutils.RemoveVersionFrom(idv)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "invalid asset in database: %q", idv)
		}
		if strippedIDV == id {
			matchingIDVs = append(matchingIDVs, idv)
		}
	}
	if len(matchingIDVs) == 0 {
		return nil, status.Errorf(codes.NotFound, "requested id %q not found", id)
	}
	if len(matchingIDVs) != 1 {
		return nil, status.Errorf(codes.Internal, "found more than one asset that could match %q: %v", id, strings.Join(matchingIDVs, ","))
	}

	rtr, err := client.Get(ctx, matchingIDVs[0])
	if err != nil {
		return nil, statusutil.Wrap(err, "could not find runtime info for asset %q", matchingIDVs[0])
	}
	return rtr, nil
}

// GetAll gets `ResourceTypeRuntime` protos of all Assets in the runtime db.
func GetAll(ctx context.Context, client Client) ([]*rtrpb.ResourceTypeRuntime, error) {
	b := backoff.WithContext(
		backoff.WithMaxRetries(
			backoff.NewExponentialBackOff(backoff.WithInitialInterval(getAllInitialBackoff)),
			getAllMaxRetries,
		),
		ctx,
	)
	return backoff.RetryWithData(func() ([]*rtrpb.ResourceTypeRuntime, error) {
		idvs, token, err := client.List(ctx)
		if err != nil {
			return nil, backoff.Permanent(fmt.Errorf("could not list runtime info for assets: %v", err))
		}

		var rtrs []*rtrpb.ResourceTypeRuntime
		if len(idvs) > 0 {
			rtrs = make([]*rtrpb.ResourceTypeRuntime, 0, len(idvs))
		}
		for chunk := range slices.Chunk(idvs, rtrChunkSize) {
			chunkRtrs, err := client.BatchGet(ctx, chunk, WithRevisionToken(token))
			if status.Code(err) == codes.Aborted {
				return nil, fmt.Errorf("could not get runtime info for assets: %v", err)
			}
			if err != nil {
				return nil, backoff.Permanent(fmt.Errorf("could not get runtime info for assets: %v", err))
			}
			rtrs = append(rtrs, chunkRtrs...)
		}
		return rtrs, nil
	}, b)
}
