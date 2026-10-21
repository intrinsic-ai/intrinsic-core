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

// Package statusutil provides helper function for dealing with gRPC statuses
// and errors.
package statusutil

import (
	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// Wrap generates a new gRPC status with the provided context.  The error code
// is unchanged.
func Wrap(err error, format string, a ...any) error {
	if err == nil {
		return nil
	}
	s := status.Convert(err)
	a = append(a, s.Message())
	return status.Errorf(s.Code(), format+": %v", a...)
}

// Obscure will generate a new error with the given format.  The original
// message details will be removed from the message, but will be logged with
// the appropriate context (at the calling location).  A nil error returns a
// nil.
func Obscure(err error, format string, a ...any) error {
	if err == nil {
		return nil
	}
	s := status.Convert(err)
	ns := status.Errorf(s.Code(), format, a...)

	a = append(a, s.Message())
	log.InfoDepthf(1, format+": %v", a...)

	return ns
}

// NewInternalError creates a new status with an internal error code and the
// provided message.
func NewInternalError(format string, a ...any) error {
	return status.Errorf(codes.Internal, format, a...)
}
