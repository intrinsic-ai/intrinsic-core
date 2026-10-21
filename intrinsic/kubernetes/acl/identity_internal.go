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

package identity

import (
	"errors"

	"github.com/google/go-containerregistry/pkg/authn"
)

// RegistryAuthenticator returns a go-containerregistry authenticator that
// authenticates registry requests (e.g. to GAR Proxy) as the given user.
//
// The authenticator sends the user's JWT as a Bearer credential, so it must
// only be used with registries that are trusted to receive that token.
// Returns an error wrapping [ErrUnauthenticated] if u is nil or has no JWT.
func RegistryAuthenticator(u *User) (authn.Authenticator, error) {
	if u == nil || u.jwt == "" {
		return nil, errors.Join(ErrUnauthenticated, errNoJWT)
	}
	return &authn.Bearer{Token: u.jwt}, nil
}
