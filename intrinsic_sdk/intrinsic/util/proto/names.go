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

// Package names provides utilities for proto names.
package names

import (
	"errors"
	"fmt"
	"regexp"
	"strings"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

var (
	// See: https://protobuf.com/docs/language-spec.
	nameRegex = regexp.MustCompile(`^([A-Za-z_][A-Za-z0-9_]*\.)+[A-Za-z_][A-Za-z0-9_]*$`)

	// ErrInvalidProtoName is returned when a proto name is invalid.
	ErrInvalidProtoName = errors.New("invalid proto name")
	// ErrInvalidProtoPrefix is returned when a proto prefix is invalid.
	ErrInvalidProtoPrefix = errors.New("invalid proto prefix")
)

// ValidateProtoName validates a proto name.
func ValidateProtoName(protoName string) error {
	if !nameRegex.MatchString(protoName) {
		return fmt.Errorf("%w: expected name formatted as '<package>.<message>', got %q", ErrInvalidProtoName, protoName)
	}
	return nil
}

// ValidateProtoPrefix validates a proto prefix.
func ValidateProtoPrefix(protoPrefix string) error {
	if len(protoPrefix) < 2 || !strings.HasPrefix(protoPrefix, "/") || !strings.HasSuffix(protoPrefix, "/") {
		return fmt.Errorf("%w: expected prefix formatted as '/<package>.<message>/', got %q", ErrInvalidProtoPrefix, protoPrefix)
	}
	protoName := protoPrefix[1 : len(protoPrefix)-1]
	if !nameRegex.MatchString(protoName) {
		return fmt.Errorf("%w: expected prefix formatted as '/<package>.<message>/', got %q", ErrInvalidProtoPrefix, protoPrefix)
	}
	return nil
}

// AnyToProtoName retrieves the proto name from an Any proto message.
func AnyToProtoName(m *anypb.Any) (string, error) {
	typeURLParts := strings.Split(m.GetTypeUrl(), "/")
	if len(typeURLParts) < 1 {
		return "", fmt.Errorf("cannot extract proto name from type URL %q", m.GetTypeUrl())
	}
	return typeURLParts[len(typeURLParts)-1], nil
}
