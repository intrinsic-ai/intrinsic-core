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

package auth

import (
	"context"
	"fmt"
	"net/http"

	"intrinsic/kubernetes/acl/cookies"
	"intrinsic/kubernetes/acl/identity"
)

// AddMetadata contains additional metadata to be added to the request.
// Example:
//
//	md := &AddMetadata{
//	  metadata: map[string]string{"custom-header": "something"},
//	  cookies:  map[string]string{"org-id": "intrinsic-dev"},
//	}
//	NewAPIKeyTokenSource("api-key", tp, WithAdditionalMetadata(md))
type AddMetadata struct {
	metadata map[string]string
	cookies  []*http.Cookie
}

type tokenSource interface {
	Token(ctx context.Context) (string, error)
}

// perRPCCreds is a gRPC credentials.PerRPCCredentials that adds the given token source and
// metadata to the request.
type perRPCCreds struct {
	ts            tokenSource
	md            *AddMetadata
	staticOpts    []identity.Option
	allowInsecure bool
}

// GetRequestMetadata returns request metadata that authenticates the request
// using a JWT retrieved using the API key.
func (s *perRPCCreds) GetRequestMetadata(ctx context.Context, _ ...string) (map[string]string, error) {
	t, err := s.ts.Token(ctx)
	if err != nil {
		return nil, fmt.Errorf("could not get account token: %v", err)
	}

	metadata := make(map[string]string)
	for k, v := range s.md.metadata {
		if k != "" && v != "" {
			metadata[k] = v
		}
	}

	opts := append([]identity.Option{identity.WithUserJWT(t)}, s.staticOpts...)
	metadata, err = identity.AppendToMetadataMap(metadata, opts...)
	if err != nil {
		return nil, fmt.Errorf("failed to append request metadata: %w", err)
	}

	// Append additional custom cookies if provided
	if len(s.md.cookies) > 0 {
		metadata, err = cookies.AddToMetadataMap(metadata, s.md.cookies...)
		if err != nil {
			return nil, fmt.Errorf("failed to merge request cookies: %w", err)
		}
	}
	return metadata, nil
}

// RequireTransportSecurity returns the configured level of transport security.
// A token source requires transport security unless it was explicitly
// configured using [WithAllowInsecure].
func (s *perRPCCreds) RequireTransportSecurity() bool {
	return !s.allowInsecure
}
