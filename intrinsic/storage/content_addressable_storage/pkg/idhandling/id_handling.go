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

// Package idhandling contains helper functions for dealing with CAS object IDs and generating hashes.
package idhandling

import (
	"crypto/sha512"
	"errors"
	"fmt"
	"hash"
	"strings"
)

const schema = "intcas://"

// EmptyDigest is the SHA-512 hex digest of an empty byte slice.
const EmptyDigest = "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"

// NewHasher returns a new SHA-512 hasher for CAS objects.
func NewHasher() hash.Hash {
	return sha512.New()
}

// StripSchema removes the schema from the given CAS object ID and validates that
// the remaining digest is a valid 128-character hex string.
//
// Example:
//
//	StripSchema("intcas://1234...") -> "1234...", nil
//	StripSchema("invalid://1234") -> "", error
func StripSchema(s string) (string, error) {
	if s == "" {
		return "", errors.New("empty input")
	}
	if s == schema {
		return "", errors.New("empty identifier")
	}
	if !strings.HasPrefix(s, schema) {
		return "", fmt.Errorf("object ID %q does not have the expected schema", s)
	}
	digest := strings.TrimPrefix(s, schema)
	if err := ValidateDigest(digest); err != nil {
		return "", fmt.Errorf("invalid object ID %q: %w", s, err)
	}
	return digest, nil
}

// AddSchema validates that the given string is a valid 128-character hex digest
// and adds the schema to it.
//
// Example:
//
//	AddSchema("1234...") -> "intcas://1234...", nil
//	AddSchema("http://1234") -> "", error
func AddSchema(s string) (string, error) {
	if err := ValidateDigest(s); err != nil {
		return "", err
	}
	return schema + s, nil
}

// ValidateDigest checks if the given string is a valid CAS digest (128-character hex string).
func ValidateDigest(digest string) error {
	if len(digest) != 128 {
		return fmt.Errorf("invalid digest length: got %d, want 128", len(digest))
	}
	for i := 0; i < len(digest); i++ {
		c := digest[i]
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
			return fmt.Errorf("invalid digest hex: character %q at index %d is not a valid hex digit", c, i)
		}
	}
	return nil
}

// ValidateObjectSize validates that the object size matches the expectations for the digest.
// Specifically, a size of 0 is valid if and only if the digest equals EmptyDigest.
func ValidateObjectSize(digest string, size int64) error {
	if size < 0 {
		return fmt.Errorf("invalid negative object size %d for digest %q", size, digest)
	}
	if size == 0 && digest != EmptyDigest {
		return fmt.Errorf("invalid zero-byte object for non-empty digest %q", digest)
	}
	if digest == EmptyDigest && size != 0 {
		return fmt.Errorf("invalid non-zero size %d for empty digest %q", size, digest)
	}
	return nil
}
