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

// Package jwt provides helpers for JWT handling.
package jwt

import (
	"encoding/base64"
	"fmt"
	"net/mail"
	"strings"
	"time"

	"github.com/golang-jwt/jwt/v5"
	"github.com/pkg/errors"
)

// Data defines our relevant subset of the oauth standard plus our custom claims.
// Extend with more attributes when needed.
type Data struct {
	RegisteredClaims        // Embedded standard JWT Claims
	UserID           string `json:"user_id"`
	// Aud represents single audience in our claims.
	//
	// DEPRECATED: User Audience field from jwt.RegisteredClaims
	Aud           string `json:"-"`
	Email         string `json:"email"`
	EmailVerified bool   `json:"email_verified"`

	// intrinsic custom claims
	Authorized bool     `json:"authorized"`
	Projects   []string `json:"ps"`
	ClusterID  string   `json:"cluster_id"`
	// Organization indicates the organization an IPC identity
	// is registered to. Only available for IPC user records.
	Organization string `json:"orgid"`
}

// ExpiresAt is helper method to extract expiration information from [Data]
func (d *Data) ExpiresAt() time.Time {
	if d.RegisteredClaims.ExpiresAt != nil {
		return d.RegisteredClaims.ExpiresAt.Time
	}
	return time.Now()
}

// Claim keys and values found in the JWT.
const (
	ClaimAudience      = "aud"
	ClaimEmailVerified = "email_verified"
	ClaimAuthorized    = "authorized"
	ClaimProjects      = "ps"
	ClaimProjectsAll   = "*"
	ClaimOrgID         = "orgid"
	ClaimClusterID     = "cluster_id"
)

var jwtParser = jwt.NewParser(jwt.WithoutClaimsValidation())

// Claims is type alias for [jwt.Claims] to hide implementation details.
type Claims = jwt.Claims

// RegisteredClaims is type alias for [jwt.RegisteredClaims] to hide
// implementation details
type RegisteredClaims = jwt.RegisteredClaims

var _ Claims = &Data{} // forces Data to always implement Claims

// UnmarshalUnsafe unmarshals the JWT payload and returns the parsed data.
//
// Unsafe because the content can not be trusted if you do not also verify
// the signature of the JWT.
func UnmarshalUnsafe(jwtk string) (*Data, error) {
	var claims Data
	err := PayloadClaimsUnsafe(jwtk, &claims)
	if err != nil {
		return nil, fmt.Errorf("cannot parse jwt token: %w", err)
	}

	// for backwards compatibility only
	if len(claims.Audience) > 0 {
		claims.Aud = claims.Audience[0]
	}
	return &claims, nil
}

// PayloadUnsafe returns the unverified payload section of a given JWT.
// Use PayloadUnsafe if you need raw access to the JWT.
//
// Unsafe because the content can not be trusted if you do not also verify
// the signature of the JWT.
func PayloadUnsafe(jwtk string) (map[string]any, error) {
	dat := jwt.MapClaims{}

	if err := PayloadClaimsUnsafe(jwtk, &dat); err != nil {
		return nil, fmt.Errorf("cannot decode payload: %w", err)
	}

	return dat, nil
}

// PayloadClaimsUnsafe parses claims of provided JWT based on claims input.
// It's unsafe as it does not validate JWT signature. Use only when you know
// what are you doing.
func PayloadClaimsUnsafe(jwtk string, claims Claims) error {
	_, _, err := jwtParser.ParseUnverified(jwtk, claims)
	if err != nil {
		return fmt.Errorf("cannot parse jwt token: %w", err)
	}
	return nil
}

// IsVerifiedAndAuthorizedUnsafe checks if the given JWT is verified and authorized.
//
// Unsafe because the content can not be trusted if you do not also verify
// the signature of the JWT.
func IsVerifiedAndAuthorizedUnsafe(tk string) error {
	d, err := UnmarshalUnsafe(tk)
	if err != nil {
		return fmt.Errorf("failed to unmarshal token: %v", err)
	}
	if !d.EmailVerified {
		return fmt.Errorf("email not verified")
	}
	if !d.Authorized {
		return fmt.Errorf("record not authorized")
	}
	return nil
}

func decodePayload(jwtk string) ([]byte, error) {
	parts := strings.Split(jwtk, ".")
	if len(parts) != 3 {
		return nil, errors.New("invalid JWT, token must have 3 parts")
	}
	d, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("failed to decode JWT payload section: %v", err)
	}
	return d, nil
}

// Email extracts the mail address of the user from the given JWT.
//
// Be aware that the signature of the JWT is not verified in this function.
func Email(t string) (string, error) {
	pl, err := PayloadUnsafe(t)
	if err != nil {
		return "", errors.Wrap(err, "decoding payload")
	}
	// We check "email" in firebase id-tokens and "uid" in custom token we create
	// from api-key usage. So far we have not found a way to align the fields.
	// "user_id" is populated by ID tokens generated from the API key auth path.
	// `sub` is used by userless access as standard registered JWT Claim.
	const jwtSubject = "sub"
	for _, k := range []string{"email", "uid", "user_id", jwtSubject} {
		m, ok := pl[k].(string)
		if ok && m != "" {
			// limiting this to subject claim only as identity tests indicate there is
			// expectation that some of those values are NOT valid emails.
			if k == jwtSubject && !isEmailAddress(m) {
				continue
			}
			return m, nil
		}
	}
	return "", fmt.Errorf("failed to extract email from JWT")
}

func isEmailAddress(value string) bool {
	_, err := mail.ParseAddress(value)
	return err == nil
}

// Aud extracts the audience from the given JWT.
//
// For Firebase ID tokens, we return the value of the `aud` field, which specifies the Firebase that
// issued/signed the JWT.
//
// Be aware that the signature of the JWT is not verified in this function.
func Aud(t string) (string, error) {
	pl, err := PayloadUnsafe(t)
	if err != nil {
		return "", errors.Wrap(err, "decoding payload")
	}

	// Look for a proper audience from a Firebase ID token.
	if aud, ok := pl["aud"].(string); ok && aud != "" && !strings.HasPrefix(aud, "https://") {
		return aud, nil
	}
	return "", fmt.Errorf("failed to extract audience from JWT")
}
