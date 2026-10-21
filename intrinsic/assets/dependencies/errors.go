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

// Package errors provides structured validation errors specific to Asset dependencies.
package errors

import (
	"fmt"

	"intrinsic/util/status/extstatus"

	espb "intrinsic/util/status/extended_status_go_proto"
)

// componentName is the name that will be used as the component name for ExtendedStatus
// errors unless overridden. This is arbitrarily chosen, and deviates from the official
// guidelines that specifies providing a component name following an ID format
// (e.g. <package>.<name>), since the component is not specific to any Asset.
const componentName = "assets_dependencies"

// Code represents a unique identifier for a validation error.
type Code uint32

// Error codes defined here can also be used as status codes for ExtendedStatus errors.
//
// A starting error code of 2001 is arbitrarily chosen here. Codes greater than 10000
// are reserved for errors not originating as part of the system (or platform).
const (
	// CodeErrUnknownDependency is used when a node has a dependency on a non-existent node.
	CodeErrUnknownDependency Code = iota + 2001

	// CodeErrInterfaceMismatch is used when a node has a dependency on an Asset that does not satisfy the dependency.
	CodeErrInterfaceMismatch

	// CodeErrMissingReferencedAsset is used when an Asset has a reference to an Asset that is not available in the Solution.
	CodeErrMissingReferencedAsset

	// CodeErrReferencedAssetVersionMismatch is used when an Asset references an Asset with an explicit version, but a different version is available in the Solution.
	CodeErrReferencedAssetVersionMismatch

	// CodeErrUnusedReferencedAsset is used when an Asset explicitly references an Asset that is not used.
	CodeErrUnusedReferencedAsset

	// CodeErrMissingUsedAsset is used when an Asset uses an Asset that is not available in the Solution.
	CodeErrMissingUsedAsset

	// CodeErrMissingDependencyTarget is used when a required dependency is unset.
	CodeErrMissingDependencyTarget
)

func errorTitleFromCode(code Code) string {
	switch code {
	case CodeErrUnknownDependency:
		return "Unknown dependency"
	case CodeErrInterfaceMismatch:
		return "Dependency interface mismatch"
	case CodeErrMissingReferencedAsset:
		return "Missing referenced Asset"
	case CodeErrReferencedAssetVersionMismatch:
		return "Referenced Asset version mismatch"
	case CodeErrUnusedReferencedAsset:
		return "Unused referenced Asset"
	case CodeErrMissingUsedAsset:
		return "Missing used Asset"
	case CodeErrMissingDependencyTarget:
		return "Missing required dependency"
	default:
		return "Dependency validation error"
	}
}

func errorSeverityFromCode(code Code) espb.ExtendedStatus_Severity {
	switch code {
	case CodeErrReferencedAssetVersionMismatch, CodeErrUnusedReferencedAsset:
		return espb.ExtendedStatus_WARNING
	default:
		return espb.ExtendedStatus_ERROR
	}
}

// Error wraps an error message with an associated validation Code.
type Error struct {
	msg  string
	code Code
}

// New creates a new dependency validation error with the given code and message.
func New(code Code, msg string) *Error {
	return &Error{code: code, msg: msg}
}

// Newf creates a new dependency validation error with the given code and formatted message.
func Newf(code Code, format string, args ...any) *Error {
	return &Error{code: code, msg: fmt.Sprintf(format, args...)}
}

func (e *Error) Error() string {
	if e == nil {
		return ""
	}
	return e.msg
}

// Code returns the error code.
func (e *Error) Code() Code {
	return e.code
}

func (e *Error) Is(target error) bool {
	if e == nil {
		return false
	}
	t, ok := target.(*Error)
	if !ok || t == nil {
		return false
	}
	return e.code == t.code
}

func (e *Error) ToExtendedStatus() *extstatus.ExtendedStatus {
	return extstatus.New(componentName, uint32(e.code),
		extstatus.WithSeverity(errorSeverityFromCode(e.code)),
		extstatus.WithTitle(errorTitleFromCode(e.code)),
		extstatus.WithUserMessage(e.Error()),
	)
}
