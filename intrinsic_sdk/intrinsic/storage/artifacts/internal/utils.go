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

// Package utils contains set of simple utilities for work with OCI images.
package utils


import (
	"fmt"
	"reflect"
	"strings"
	"time"

	"github.com/containerd/containerd/content"
	"github.com/containerd/containerd/platforms"
	"github.com/opencontainers/go-digest"
	ocispec "github.com/opencontainers/image-spec/specs-go/v1"
	"github.com/pkg/errors"

	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"

	googlepb "google.golang.org/protobuf/types/known/timestamppb"
)

const (
	// RefMaxRawSHALengthIndex indicates the end index for cutting digest hash
	// into short reference. Assumes open-containers digest hash in string form.
	RefMaxRawSHALengthIndex = 19
	// RefSHAPrefixEndIndex is end index for digest hash prefix, shaXYZ:IDENTIFIER
	RefSHAPrefixEndIndex = len(digest.Canonical) + 1
)



const (
	// AnnotationImageName is annotation id for UpdateRequest.Content.annotations
	// to specify owning image name for blobs.
	AnnotationImageName = "intrinsic.ai/content.image.name"
)


var (
	// ErrMissingArtifactDescriptor indicates that content descriptor is needed but not provided
	ErrMissingArtifactDescriptor = errors.New("missing content in update message")
	// ErrMissingAnnotationImageName indicates that AnnotationImageName is required, but missing.
	ErrMissingAnnotationImageName = fmt.Errorf("missing %q annotation", AnnotationImageName)
)

// ActionStatResponse converts content.Status and request into Response object
// later sent to caller. This is public as some there are some usages outside
// of this package.
func ActionStatResponse(status content.Status, request *artifactpb.UpdateRequest) *artifactpb.UpdateResponse {
	return &artifactpb.UpdateResponse{
		Ref:       status.Ref,
		Action:    &request.Action,
		ChunkId:   request.ChunkId,
		Total:     &status.Offset,
		UpdatedAt: AsTimestamp(status.UpdatedAt),
	}
}

// AsStringDigest converts digest.Digest into a pointer to string representation
// of given digest
func AsStringDigest(dig digest.Digest) *string {
	result := string(dig)
	if result == "" {
		return nil
	}
	return &result
}

// ToPointer converts given value to pointer. If value is zero, as understood
// by Golang, nil is returned. Pointer to the value is returned otherwise.
// This function uses reflection to determine if value is zero.
func ToPointer[T comparable](value T) *T {
	refVal := reflect.ValueOf(value)
	if !refVal.IsValid() || refVal.IsZero() {
		return nil
	}
	return &value
}

// AsTimestamp converts time.Time to google.protobuf.Timestamp msg
func AsTimestamp(ts time.Time) *googlepb.Timestamp {
	return &googlepb.Timestamp{
		Seconds: ts.Unix(),
		Nanos:   int32(ts.Nanosecond()),
	}
}

// ValueOrEmptyStr returns value of string pointer or empty string if pointer
// is nil.
func ValueOrEmptyStr(value *string) string {
	if value == nil {
		return ""
	}
	return *value
}

// AsShortName converts open-container digest in string from to shorter version
// similar one seen in docker cli outputs. If name is not prefixed with "sha"
// or length if name is less than RefMaxRawSHALengthIndex, full name is returned.
func AsShortName(name string) string {
	if strings.HasPrefix(name, "sha") {
		// shaXYZ:IDENTIFIER
		if len(name) > RefMaxRawSHALengthIndex {
			return name[RefSHAPrefixEndIndex:RefMaxRawSHALengthIndex]
		}
	}
	return name
}

// AnOption is a generic option operating on any interface. The caller
// is expected to specialize this AnOption into particular interface type
type AnOption[T any] func(base T) error

// ApplyOptions applies set of options mutating functions (AnOption) to base
// object, mutating it. Object base have to be passed as interface as AnOption
// assumes in-place modifications
func ApplyOptions[T any](base T, opts ...AnOption[T]) error {
	if len(opts) == 0 {
		return nil
	}
	for _, opt := range opts {
		if err := opt(base); err != nil {
			return err
		}
	}
	return nil
}

// AsDescriptor converts our ImageManifest into OCI v1.Descriptor.
func AsDescriptor(origin *artifactpb.ImageManifest) ocispec.Descriptor {
	platform, err := platforms.Parse(ValueOrEmptyStr(origin.Platform))
	if err != nil {
		// we cannot parse platform, we will use default platform for our system
		platform = platforms.DefaultSpec()
	}

	return ocispec.Descriptor{
		MediaType:    origin.MediaType,
		Digest:       digest.Digest(origin.Digest),
		Size:         origin.Size,
		URLs:         origin.Urls,
		Annotations:  origin.Annotations,
		Data:         origin.Data,
		Platform:     &platform,
		ArtifactType: ValueOrEmptyStr(origin.ArtifactType),
	}
}

// AsRefArray converts list of OCI v1.Descriptor objects to list of
// string references of their digests.
func AsRefArray(descriptions ...ocispec.Descriptor) []string {
	result := make([]string, 0, len(descriptions))
	for _, desc := range descriptions {
		result = append(result, desc.Digest.String())
	}
	return result
}

// ImageNameFromDescriptor extracts image name associated with the given ArtifactDescriptor
// annotation in order to find blob origin.
func ImageNameFromDescriptor(ad *artifactpb.ArtifactDescriptor) (string, error) {
	if ad == nil {
		return "", ErrMissingArtifactDescriptor
	}
	name, ok := ad.Annotations[AnnotationImageName]
	if !ok {
		return "", ErrMissingAnnotationImageName
	}
	return name, nil
}


