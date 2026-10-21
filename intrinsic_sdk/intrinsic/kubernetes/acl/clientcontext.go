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

// Package clientcontext provides helpers to propagate auth-related gRPC metadata.
package clientcontext

import (
	"context"
	"errors"
	"fmt"
	"slices"

	"intrinsic/kubernetes/acl/cookies"
	"intrinsic/stats/go/telemetry"

	log "github.com/golang/glog"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
)

var (
	// ErrUnauthenticated indicates that the request was not authenticated.
	ErrUnauthenticated = errors.New("unauthenticated")
	// ErrMissingOrgID indicates that the there was no org-id found.
	ErrMissingOrgID = errors.New("no org-id found")
	// ErrInvalidRequest indicates that the request is invalid.
	ErrInvalidRequest = errors.New("invalid request")
	// ErrMetadataKeyConflict indicates that multiple possible values were found in context metadata for a single key.
	ErrMetadataKeyConflict = errors.New("multiple possible values found in context metadata for a single key")
)

const (
	AuthHeaderName        = "authorization"
	ApikeyTokenHeaderName = "apikey-token"
	OrgIDHeader           = "x-intrinsic-org"
)

// ErrGRPC converts errors from the clientcontext package to the corresponding gRPC error.
func ErrGRPC(err error) error {
	if err == nil {
		return nil
	}
	switch {
	case errors.Is(err, ErrMissingOrgID), errors.Is(err, ErrInvalidRequest):
		return status.Error(codes.InvalidArgument, err.Error())
	case errors.Is(err, ErrUnauthenticated):
		return status.Error(codes.Unauthenticated, err.Error())
	default:
		return status.Error(codes.Unknown, err.Error())
	}
}

// ToContextFromIncoming copies auth-related incoming GRPC metadata to outgoing
// metadata. The method does not error if auth-related information is not
// present. Use [ToContextFromIncomingChecked] to check if the context contains
// incoming authentication info.
func ToContextFromIncoming(ctx context.Context) (context.Context, error) {
	_, span := trace.StartSpan(ctx, "clientcontext.ToContextFromIncoming")
	defer span.End()

	ctx, _, err := ToContextFromIncomingChecked(ctx)
	return ctx, err
}

// ToContextFromIncomingChecked copies auth-related incoming GRPC metadata to
// outgoing metadata. Returns false (and an unchanged context) if no relevant
// metadata was found. Use this when chaining GRPC requests
// (HTTP/GRPC->GRPC->GRPC).
//
// If any relevant values are already present on the outgoing context metadata,
// the values from the incoming context metadata will be appended to the
// existing values. This may be problematic for headers where web server may
// expect only a single value, such as the "Authorization" header. A warning
// will be logged if certain headers have more than one value after propagating
// incoming metadata.
func ToContextFromIncomingChecked(ctx context.Context) (context.Context, bool, error) {
	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		return ctx, false, nil
	}

	var changed bool

	cookieHeaders := md.Get(cookies.CookieHeaderName)
	if len(cookieHeaders) >= 1 {
		newCtx, csChanged, err := setOutgoingValueCollisionAware(ctx, cookies.CookieHeaderName, cookieHeaders...)
		if err != nil {
			return ctx, false, err
		}
		changed = changed || csChanged
		ctx = newCtx
	}

	authHeaders := md.Get(AuthHeaderName)
	if len(authHeaders) > 1 {
		log.WarningContextf(ctx, "ToContextFromIncomingChecked: Multiple auth headers found in incoming context metadata: %v", authHeaders)
		return ctx, false, fmt.Errorf("%w: %w for %q in incoming context metadata", ErrInvalidRequest, ErrMetadataKeyConflict, AuthHeaderName)
	}
	if len(authHeaders) == 1 {
		newCtx, authChanged, err := setOutgoingValueCollisionAware(ctx, AuthHeaderName, authHeaders...)
		if err != nil {
			return ctx, false, err
		}
		changed = changed || authChanged
		ctx = newCtx
	}

	apikeyHeaders := md.Get(ApikeyTokenHeaderName)
	if len(apikeyHeaders) > 1 {
		log.WarningContextf(ctx, "ToContextFromIncomingChecked: Multiple apikey headers found in incoming context metadata: %v", apikeyHeaders)
		return ctx, false, fmt.Errorf("%w: %w for %q in incoming context metadata", ErrInvalidRequest, ErrMetadataKeyConflict, ApikeyTokenHeaderName)
	}
	if len(apikeyHeaders) == 1 {
		newCtx, apikeyChanged, err := setOutgoingValueCollisionAware(ctx, ApikeyTokenHeaderName, apikeyHeaders...)
		if err != nil {
			return ctx, false, err
		}
		changed = changed || apikeyChanged
		ctx = newCtx
	}

	orgHeaders := md.Get(OrgIDHeader)
	if len(orgHeaders) > 1 {
		orgHeaders = slices.Clone(orgHeaders)
		slices.Sort(orgHeaders)
		orgHeaders = slices.Compact(orgHeaders)
	}
	if len(orgHeaders) > 1 {
		log.WarningContextf(ctx, "ToContextFromIncomingChecked: Multiple org headers found in incoming context metadata: %v", orgHeaders)
		return ctx, false, fmt.Errorf("%w: %w for %q in incoming context metadata", ErrInvalidRequest, ErrMetadataKeyConflict, OrgIDHeader)
	}
	if len(orgHeaders) == 1 {
		newCtx, orgChanged, err := setOutgoingValueCollisionAware(ctx, OrgIDHeader, orgHeaders...)
		if err != nil {
			return ctx, false, err
		}
		changed = changed || orgChanged
		ctx = newCtx
	}

	if changed {
		warnIfMultipleOutgoingValues(ctx, AuthHeaderName, ApikeyTokenHeaderName, OrgIDHeader)
	}

	return ctx, changed, nil
}

func warnIfMultipleOutgoingValues(ctx context.Context, headers ...string) {
	mdOut, ok := metadata.FromOutgoingContext(ctx)
	if !ok {
		return
	}
	for _, h := range headers {
		if vals := mdOut.Get(h); len(vals) > 1 {
			log.WarningContextf(ctx, "Header %q has %d values in outgoing metadata. Multiple values for this header may cause target services to reject requests with somewhat cryptic errors.", h, len(vals))
		}
	}
}

func setOutgoingValueCollisionAware(ctx context.Context, key string, vals ...string) (context.Context, bool, error) {
	lctx, span := trace.StartSpan(ctx, "clientcontext.setOutgoingValueCollisionAware")
	defer span.End()
	span.AddAttributes(trace.StringAttribute("key", key))

	omd, ok := metadata.FromOutgoingContext(ctx)
	if !ok {
		omd = metadata.MD{}
		omd.Set(key, vals...)
		return metadata.NewOutgoingContext(ctx, omd), true, nil
	}

	presentValues := omd.Get(key)
	if len(presentValues) == 0 {
		omd.Set(key, vals...)
		return metadata.NewOutgoingContext(ctx, omd), true, nil
	}

	slices.Sort(presentValues)
	slices.Sort(vals)
	if slices.Equal(presentValues, vals) {
		return ctx, false, nil
	}

	log.WarningContextf(lctx, "Collision detected when setting values on outgoing context metadata for key %q. present outgoing values: %v, values that should get set: %v", key, presentValues, vals)
	telemetry.SetError(span, trace.StatusCodeInvalidArgument, "setOutgoingValueCollisionAware: Collision detected when setting values on outgoing context metadata", ErrMetadataKeyConflict)
	return ctx, false, fmt.Errorf("%w: %w for %q in outgoing context metadata", ErrInvalidRequest, ErrMetadataKeyConflict, key)
}
