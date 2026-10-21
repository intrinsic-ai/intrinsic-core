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

// Package interfaceutils provides utilities for working with Asset URI interfaces.
package interfaceutils

import (
	"fmt"
	"regexp"

	"github.com/pkg/errors"
)

const (
	// GRPCURIPrefix is the prefix used for gRPC service dependencies.
	GRPCURIPrefix = "grpc://"
	// DataURIPrefix is the prefix used for proto-based data dependencies.

	// LINT.IfChange(data_uri_prefix)

	DataURIPrefix = "data://"

	// LINT.ThenChange(//intrinsic/frontend/onprem/proto_custom_fields/resolved_dependency.ts:data_uri_prefix)

)

var (
	uriRegex = regexp.MustCompile(`^(grpc://|data://)([A-Za-z_][A-Za-z0-9_]*\.)+[A-Za-z_][A-Za-z0-9_]*$`)

	// ErrInvalidInterfaceName is returned when an interface name is invalid.
	ErrInvalidInterfaceName = errors.New("invalid interface name")
)

// ValidateInterfaceName validates an interface name with a protocol prefix.
func ValidateInterfaceName(uri string) error {
	if !uriRegex.MatchString(uri) {
		return fmt.Errorf("%w: expected URI to be formatted as '<protocol>://<package>.<message>', got %q", ErrInvalidInterfaceName, uri)
	}
	return nil
}
