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

// Package uploader provides a stateful uploader for Asset-related artifacts.
package uploader

import (
	"context"
	"crypto/sha512"
	"fmt"
	"hash"
	"slices"
	"strings"
	"sync"
	"time"

	"intrinsic/storage/content_addressable_storage/pkg/clienthelpers"

	log "github.com/golang/glog"
	"github.com/pborman/uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

const (
	// casUploadChunkSize is the maximum byte size of each chunk sent on the CAS Create stream.
	//
	// While the AssetArtifacts server configures a large gRPC max receive message size for
	// UploadChunk requests, the backend CAS gRPC server uses the default 4 MiB limit.
	casUploadChunkSize                = clienthelpers.DefaultUploadChunkSize
	defaultIdleTimeout                = 30 * time.Second
	defaultMaxConcurrentActiveUploads = 100
	defaultFinalizedRetentionTimeout  = 5 * time.Minute
)

// UploaderOption is a functional option for configuring an Uploader.
type UploaderOption func(*Uploader)

// WithIdleTimeout sets the inactivity timeout for uploads created by the Uploader.
func WithIdleTimeout(d time.Duration) UploaderOption {
	return func(u *Uploader) {
		u.idleTimeout = d
	}
}

// WithRetentionTimeout sets the retention timeout for finalized uploads in the Uploader.
func WithRetentionTimeout(d time.Duration) UploaderOption {
	return func(u *Uploader) {
		u.retentionTimeout = d
	}
}

// WithMaxConcurrentUploads sets the maximum number of concurrent active uploads.
func WithMaxConcurrentUploads(limit int) UploaderOption {
	return func(u *Uploader) {
		u.maxConcurrentUploads = limit
	}
}

// New creates a new Uploader.
func New(client caspb.ContentAddressableStorageServiceClient, opts ...UploaderOption) *Uploader {
	u := &Uploader{
		casClient:            client,
		idleTimeout:          defaultIdleTimeout,
		maxConcurrentUploads: defaultMaxConcurrentActiveUploads,
		retentionTimeout:     defaultFinalizedRetentionTimeout,
		uploads:              make(map[string]Upload),
	}
	for _, opt := range opts {
		opt(u)
	}

	return u
}

// Uploader manages a bounded collection of stateful uploads with active limits and retention cleanup.
type Uploader struct {
	activeCount          int
	casClient            caspb.ContentAddressableStorageServiceClient
	idleTimeout          time.Duration
	maxConcurrentUploads int
	mu                   sync.Mutex // Protects access to the uploads map and activeCount.
	retentionTimeout     time.Duration
	uploads              map[string]Upload
}

// Add checks limits, starts a new upload, registers it, and returns the new upload's ID and whether
// the artifact already exists in CAS.
//
// If the artifact already exists in CAS, a no-op Upload is returned.
func (u *Uploader) Add(ctx context.Context, digest string) (string, bool, error) {
	u.mu.Lock()
	defer u.mu.Unlock()

	if u.activeCount >= u.maxConcurrentUploads {
		log.WarningContextf(ctx, "Max concurrent active uploads limit (%d) reached", u.maxConcurrentUploads)
		return "", false, status.Error(codes.ResourceExhausted, "too many concurrent active uploads, try again later")
	}

	id := uuid.New()
	upload, artifactExists, err := u.newUpload(ctx, id, digest)
	if err != nil {
		log.ErrorContextf(ctx, "Failed to start new upload in Uploader.Add: %v", err)
		return "", false, err
	}

	u.uploads[id] = upload
	u.activeCount++

	return id, artifactExists, nil
}

// Get retrieves an upload by ID.
func (u *Uploader) Get(id string) (Upload, error) {
	u.mu.Lock()
	defer u.mu.Unlock()

	upload, ok := u.uploads[id]
	if !ok {
		log.Warningf("Upload session %q not found in active uploads map", id)
		return nil, status.Errorf(codes.NotFound, "upload %q not found", id)
	}

	return upload, nil
}

func (u *Uploader) newUpload(ctx context.Context, id, digest string) (Upload, bool, error) {
	if digest != "" {
		algo, hash, err := parseDigest(digest)
		if err != nil {
			return nil, false, err
		}
		// CAS is content-addressed by SHA-512, so non-sha512 digests (e.g., highwayhash128 on raw GZF
		// ReferencedData protos) cannot be looked up in CAS and fall back to a streaming upload.
		if algo == "sha512" {
			// Check whether the associated object exists in CAS.
			casURI := "intcas://" + hash
			_, err := u.casClient.Stat(ctx, &caspb.StatRequest{ObjectId: casURI})
			switch status.Code(err) {
			case codes.OK:
				return u.newNoOpUpload(id, casURI, digest), true, nil
			case codes.NotFound:
				// Artifact does not exist in CAS; proceed with streaming upload.
			default:
				return nil, false, status.Errorf(codes.Internal, "failed to stat CAS object for digest %q: %v", digest, err)
			}
		}
	}

	upload, err := u.newCASUpload(ctx, id)
	if err != nil {
		return nil, false, err
	}

	return upload, false, nil
}

func (u *Uploader) newNoOpUpload(id, casURI, digest string) Upload {
	return &noOpUpload{
		casURI: casURI,
		digest: digest,
		state:  u.newUploadState(id, nil),
	}
}

func (u *Uploader) newCASUpload(ctx context.Context, id string) (Upload, error) {
	// We need to be able to cancel the CAS stream client independently of the parent context
	// cancellation.
	uploadCtx, cancel := context.WithCancel(context.WithoutCancel(ctx))

	casStream, err := u.casClient.Create(uploadCtx)
	if err != nil {
		cancel()
		log.Errorf("Failed to create CAS stream for upload: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to create CAS stream: %v", err)
	}

	return &casUpload{
		casStream:      casStream,
		checksummer:    clienthelpers.NewChecksummer(),
		shaChecksummer: sha512.New(),
		state:          u.newUploadState(id, cancel),
	}, nil
}

func (u *Uploader) newUploadState(id string, cancel context.CancelFunc) *uploadState {
	s := &uploadState{
		cancel:      cancel,
		idleTimeout: u.idleTimeout,
		onFinalized: func() { u.finalizeUpload(id) },
	}
	s.idleTimer = time.AfterFunc(s.idleTimeout, func() {
		log.Warningf("Upload session timed out after %v of inactivity; aborting upload", s.idleTimeout)
		s.abort()
	})
	return s
}

func (u *Uploader) finalizeUpload(id string) {
	u.mu.Lock()
	u.activeCount--
	u.mu.Unlock()

	time.AfterFunc(u.retentionTimeout, func() {
		u.mu.Lock()
		defer u.mu.Unlock()

		log.V(1).Infof("Retention expired; removing finalized upload session %q", id)
		delete(u.uploads, id)
	})
}

// Upload represents an active upload session.
type Upload interface {
	// Abort cancels the upload and cleans up all resources.
	//
	// If the upload has already been finalized, then this is a no-op.
	Abort()
	// Finalize closes the upload, verifies the digest, and returns the ReferencedData.
	//
	// If the upload has already been finalized, it returns the cached result.
	Finalize(ctx context.Context, expectedDigest string) (*rdpb.ReferencedData, error)
	// Send uploads a chunk of data.
	//
	// Enforces sequential offsets.
	Send(ctx context.Context, offset int64, data []byte) error
}

// noOpUpload represents an active upload session for an artifact that already exists in CAS.
type noOpUpload struct {
	casURI string
	digest string
	state  *uploadState
}

// Send is a no-op that refreshes the upload session's inactivity timer.
func (u *noOpUpload) Send(ctx context.Context, offset int64, data []byte) error {
	return u.state.touch()
}

// Finalize verifies the expected digest against the existing digest and returns the ReferencedData.
//
// If the upload has already been finalized, it returns the cached result.
func (u *noOpUpload) Finalize(ctx context.Context, expectedDigest string) (*rdpb.ReferencedData, error) {
	return u.state.finalize(ctx, u.casURI, u.digest, expectedDigest)
}

// Abort cancels the upload and cleans up all resources.
func (u *noOpUpload) Abort() {
	u.state.abort()
}

// casUpload represents an active upload session that streams chunks to CAS.
type casUpload struct {
	state *uploadState

	// Network streaming
	casStream caspb.ContentAddressableStorageService_CreateClient
	sendMu    sync.Mutex // Serializes Send and Finalize calls to CAS stream.

	// Stream position & retry deduplication
	expectedOffset    int64
	lastChunkMetadata *chunkMetadata

	// Checksumming
	checksummer    hash.Hash32
	shaChecksummer hash.Hash
}

// Send uploads a chunk of data.
//
// Enforces sequential offsets.
func (u *casUpload) Send(ctx context.Context, offset int64, data []byte) error {
	u.sendMu.Lock()
	defer u.sendMu.Unlock()

	if err := u.state.touch(); err != nil {
		log.WarningContextf(ctx, "Upload Send rejected: session already finalized/inactive: %v", err)
		return err
	}

	chunkMetadata := u.newChunkMetadata(offset, data)

	if offset != u.expectedOffset {
		if chunkMetadata.Equal(u.lastChunkMetadata) {
			log.V(2).InfoContextf(ctx, "Ignoring duplicate chunk retry at offset %d (size %d bytes)", offset, len(data))
			return nil
		}
		log.WarningContextf(ctx, "Upload chunk offset mismatch: got offset %d, expected %d; aborting session", offset, u.expectedOffset)
		u.Abort()
		return status.Errorf(codes.InvalidArgument, "offset mismatch: got %d, expected %d", offset, u.expectedOffset)
	}

	// Split the incoming chunk into casUploadChunkSize sub-chunks to decouple AssetArtifacts'
	// maximum request size from that of the CAS server. Offset tracking, retry deduplication, and
	// SHA-512 hashing still operate on the full UploadChunk payload.
	for subChunk := range slices.Chunk(data, casUploadChunkSize) {
		if err := u.casStream.Send(&caspb.CreateRequest{
			ChecksummedData: &caspb.ChecksummedData{
				Content: subChunk,
				Crc32C:  proto.Uint32(u.checksum(subChunk)),
			},
		}); err != nil {
			log.ErrorContextf(ctx, "Failed to send chunk (offset %d, size %d) to CAS stream: %v; aborting session", offset, len(data), err)
			u.Abort()
			return status.Errorf(codes.Internal, "failed to send chunk to CAS: %v", err)
		}
	}

	u.shaChecksummer.Write(data)
	u.lastChunkMetadata = chunkMetadata
	u.expectedOffset += int64(len(data))

	return nil
}

// Finalize closes the upload, verifies the digest, and returns the ReferencedData.
//
// If the upload has already been finalized, it returns the cached result.
func (u *casUpload) Finalize(ctx context.Context, expectedDigest string) (*rdpb.ReferencedData, error) {
	u.sendMu.Lock()
	defer u.sendMu.Unlock()

	if finalized, ref, err := u.state.getFinalState(); finalized {
		log.InfoContextf(ctx, "Upload Finalize called on already finalized session; returning cached result (err: %v)", err)
		return ref, err
	}

	casResp, err := u.casStream.CloseAndRecv()
	if err != nil {
		log.ErrorContextf(ctx, "Failed to close CAS stream on Finalize: %v", err)
		return u.state.setFinalState(nil, status.Errorf(codes.Internal, "failed to close CAS stream: %v", err))
	}

	computedDigest := fmt.Sprintf("sha512:%x", u.shaChecksummer.Sum(nil))
	return u.state.finalize(ctx, casResp.GetObjectId(), computedDigest, expectedDigest)
}

// Abort cancels the upload and cleans up all resources.
//
// If the upload has already been finalized, then this is a no-op.
func (u *casUpload) Abort() {
	u.state.abort()
}

func (u *casUpload) checksum(data []byte) uint32 {
	u.checksummer.Reset()
	u.checksummer.Write(data)
	return u.checksummer.Sum32()
}

func (u *casUpload) newChunkMetadata(offset int64, data []byte) *chunkMetadata {
	return &chunkMetadata{
		crc32c: u.checksum(data),
		offset: offset,
		size:   int64(len(data)),
	}
}

// uploadState manages the shared lifecycle, timers, and cached results of an Upload.
type uploadState struct {
	cancel      context.CancelFunc
	err         error
	finalized   bool
	idleTimeout time.Duration
	idleTimer   *time.Timer
	onFinalized func()
	ref         *rdpb.ReferencedData
	stateMu     sync.Mutex // Protects lifecycle state, timers, and cached results.
}

// abort cancels the upload and cleans up all resources.
//
// If the upload has already been finalized, then this is a no-op.
func (s *uploadState) abort() {
	s.setFinalState(nil, status.Error(codes.Aborted, "upload session aborted"))
}

func (s *uploadState) finalize(ctx context.Context, casURI, computedDigest, expectedDigest string) (*rdpb.ReferencedData, error) {
	if finalized, ref, err := s.getFinalState(); finalized {
		log.InfoContextf(ctx, "Upload Finalize called on already finalized session; returning cached result (err: %v)", err)
		return ref, err
	}

	if err := verifyDigest(expectedDigest, computedDigest); err != nil {
		log.WarningContextf(ctx, "Upload finalization failed: %v", err)
		return s.setFinalState(nil, err)
	}

	return s.setFinalState(&rdpb.ReferencedData{
		Data: &rdpb.ReferencedData_Reference{
			Reference: casURI,
		},
		Digest: computedDigest,
	}, nil)
}

func (s *uploadState) getFinalState() (bool, *rdpb.ReferencedData, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	return s.finalized, s.ref, s.err
}

func (s *uploadState) setFinalState(ref *rdpb.ReferencedData, err error) (*rdpb.ReferencedData, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	if s.finalized {
		return s.ref, s.err
	}

	if s.idleTimer != nil {
		s.idleTimer.Stop()
	}

	if s.cancel != nil {
		s.cancel()
		s.cancel = nil
	}

	s.finalized = true
	s.ref = ref
	s.err = err

	if s.onFinalized != nil {
		s.onFinalized()
		s.onFinalized = nil
	}

	return ref, err
}

func (s *uploadState) touch() error {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	if s.finalized {
		if s.err != nil {
			return s.err
		}
		return status.Error(codes.FailedPrecondition, "upload already finalized")
	}

	if s.idleTimer != nil {
		s.idleTimer.Reset(s.idleTimeout)
	}

	return nil
}

type chunkMetadata struct {
	crc32c uint32
	offset int64
	size   int64
}

func (m *chunkMetadata) Equal(other *chunkMetadata) bool {
	if m == nil || other == nil {
		return m == other
	}
	return *m == *other
}

// parseDigest splits a digest of the form "<algo>:<hex>" into its algorithm and hex hash.
func parseDigest(digest string) (string, string, error) {
	algo, hash, ok := strings.Cut(digest, ":")
	if !ok || algo == "" || hash == "" {
		return "", "", status.Errorf(codes.InvalidArgument, "malformed digest %q", digest)
	}
	return algo, hash, nil
}

// verifyDigest verifies that expectedDigest matches computedDigest if expectedDigest is non-empty
// and uses the same algorithm as computedDigest.
func verifyDigest(expectedDigest, computedDigest string) error {
	if expectedDigest == "" {
		return nil
	}
	expectedAlgo, expectedHash, err := parseDigest(expectedDigest)
	if err != nil {
		return err
	}
	computedAlgo, computedHash, err := parseDigest(computedDigest)
	if err != nil {
		return err
	}
	if expectedAlgo == computedAlgo && expectedHash != computedHash {
		return status.Errorf(codes.InvalidArgument, "digest mismatch: expected %s, computed %s", expectedDigest, computedDigest)
	}
	return nil
}
