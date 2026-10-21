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

// Package utils contains utils for working with Data assets.
package utils

import (
	"bytes"
	"crypto/sha512"
	"encoding/hex" 
	"fmt"
	"io"
	"strings"

	"intrinsic/util/proto/registryutil"

	"github.com/minio/highwayhash" 
	"google.golang.org/protobuf/proto"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
)

var highwayHashKey = bytes.Repeat([]byte{0}, 32)

// ExtractPayload extracts a Data asset payload to a dynamicpb analog of the target payload type.
func ExtractPayload(data *dapb.DataAsset) (proto.Message, error) {
	payloadAny := data.GetData()

	types, err := registryutil.NewTypesFromFileDescriptorSet(data.GetFileDescriptorSet())
	if err != nil {
		return nil, fmt.Errorf("cannot populate registry: %w", err)
	}

	msgType, err := types.FindMessageByName(payloadAny.MessageName())
	if err != nil {
		return nil, fmt.Errorf("cannot find message for Data payload: %w", err)
	}

	payload := msgType.New().Interface()
	if err := payloadAny.UnmarshalTo(payload); err != nil {
		return nil, fmt.Errorf("cannot unmarshal data payload: %w", err)
	}

	return payload, nil
}

// HashAlgorithm is an enum for the supported digest hash algorithms.
type HashAlgorithm string

const (
	// HighwayHash128 is the HighwayHash-128 algorithm.
	HighwayHash128 HashAlgorithm = "highwayhash128"
	// Sha512 is the SHA-512 algorithm.
	Sha512 HashAlgorithm = "sha512"
)

// DigestOptions contains options for a call to Digest.
type DigestOptions struct {
	// Algorithm is the hashing algorithm to use.
	Algorithm HashAlgorithm
}

// DigestOption is a functional option for DigestFrom*.
type DigestOption func(*DigestOptions)

// WithAlgorithm sets the Algorithm option.
func WithAlgorithm(algorithm HashAlgorithm) DigestOption {
	return func(opts *DigestOptions) {
		opts.Algorithm = algorithm
	}
}

// Digest creates a digest for the specified data.
func Digest(reader io.Reader, options ...DigestOption) (string, error) {
	opts := DigestOptions{
		Algorithm: HighwayHash128,
	}
	for _, opt := range options {
		opt(&opts)
	}

	switch opts.Algorithm {
	case Sha512:
		h := sha512.New()
		if _, err := io.Copy(h, reader); err != nil {
			return "", err
		}
		digest := fmt.Sprintf("%s:%x", opts.Algorithm, h.Sum(nil))
		return digest, nil

	case HighwayHash128:
		h, err := highwayhash.New128(highwayHashKey)
		if err != nil {
			return "", err
		}
		if _, err := io.Copy(h, reader); err != nil {
			return "", err
		}
		digest := fmt.Sprintf("%s:%s", opts.Algorithm, hex.EncodeToString(h.Sum(nil)))
		return digest, nil

	default:
		return "", fmt.Errorf("unknown hash algorithm: %v", opts.Algorithm)
	}
}

// ParsedDigest is a parsed digest.
type ParsedDigest struct {
	Algorithm HashAlgorithm
	Digest    string
	Hash      string
}

// ParseDigest parses a digest.
func ParseDigest(digest string) (*ParsedDigest, error) {
	parts := strings.Split(digest, ":")
	if len(parts) != 2 {
		return nil, fmt.Errorf("invalid digest: %q", digest)
	}
	return &ParsedDigest{
		Algorithm: HashAlgorithm(parts[0]),
		Digest:    digest,
		Hash:      parts[1],
	}, nil
}
