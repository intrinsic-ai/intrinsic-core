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

// Package writer container set of writers for artifact service.
// Each writer is responsible for ensuring correct write into target storage.
package writer

import (
	"context"
	"errors"
	"fmt"
	"io"
	"strings"

	"github.com/containerd/containerd"
	"github.com/containerd/containerd/content"
	"github.com/containerd/containerd/images"
	log "github.com/golang/glog"
	"github.com/opencontainers/go-digest"

	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

// MediaTypeApplicationTar indicates media type of tar file
const MediaTypeApplicationTar = "application/x-tar"

var (
	// ErrMaxContentSizeExceeded indicates that maximum allowed size for
	// the blob was exceeded.
	ErrMaxContentSizeExceeded = errors.New("max content size exceeded")
	// ErrAlreadyFinalized indicates that writer was finalized and
	// cannot perform more operations.
	ErrAlreadyFinalized = errors.New("writer already finalized")
	// ErrInvalidReference indicates that the provided reference is invalid or
	// contains path traversal sequences.
	ErrInvalidReference = errors.New("invalid reference: path traversal detected")
)

// UpdateWriter writes blobs into underlying storage. Writer is fully responsible
// for writing data to storage. More complex writers should be composed of
// simpler writers if possible.
type UpdateWriter interface {
	io.WriteCloser
	// Status gets current status of write
	Status() (content.Status, error)
	// Commit commits current object to storage, finalizing writing operation.
	// if size and expected digest are provided, those are checked, prior to
	// commit.
	Commit(size int64, expected digest.Digest) error
	// Abort aborts write operation and makes the best effort to remove any
	// partial writes from storage
	Abort() error
	// Digest returns expected final digest of the blob, if known. This is optional
	// operation.
	Digest() digest.Digest
}

// ArtifactStore is provider for writer dependencies.
type ArtifactStore interface {
	ContainerdClient() *containerd.Client
	EphemeralFileStore() string
}

// NewWriter returns fresh writer for content based on request's media type.
func NewWriter(ctx context.Context, store ArtifactStore, request *artifactpb.UpdateRequest) (UpdateWriter, error) {
	mt := request.MediaType

	log.InfoContextf(ctx, "writer for object %s (%s)", mt, request.Ref)

	if mt == MediaTypeApplicationTar {
		contentMetadata := request.Content
		if contentMetadata == nil {
			return newFileWriter(ctx, store, request)
		}
		contentMT := contentMetadata.MediaType
		if images.IsIndexType(contentMT) || images.IsManifestType(contentMT) {
			return newImageTarWriter(ctx, store, request)
		}
		return newFileWriter(ctx, store, request)
	}
	if images.IsDockerType(mt) || isOciType(mt) {
		if images.IsManifestType(mt) {
			return newImageWriter(ctx, store, request)
		}
		return newOciWriter(ctx, store, request)
	}
	return nil, fmt.Errorf("unsupported media type: '%s'", mt)
}

func isOciType(mt string) bool {
	return strings.HasPrefix(mt, "application/vnd.oci.")
}
