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

// Package identity provides helpers to work with user identities inside the Intrinsic stack.
package identity

import (
	"context"
	"errors"
	"fmt"
	"net/http"
	"regexp"
	"slices"
	"strings"

	"intrinsic/frontend/go/origin"
	"intrinsic/kubernetes/acl/clientcontext"
	"intrinsic/kubernetes/acl/cookies"
	"intrinsic/kubernetes/acl/jwt"
	"intrinsic/kubernetes/acl/org"
	"intrinsic/stats/go/telemetry"

	log "github.com/golang/glog"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/metadata"
)

var (
	// ErrUnauthenticated indicates that the request was not authenticated.
	ErrUnauthenticated = clientcontext.ErrUnauthenticated
	// ErrMissingOrgID indicates that the there was no org-id found.
	ErrMissingOrgID = clientcontext.ErrMissingOrgID
	// ErrMissingProject indicates that there was no project-id found.
	ErrMissingProject = errors.New("no project-id found")
	// ErrInvalidRequest indicates that the request is invalid.
	ErrInvalidRequest = clientcontext.ErrInvalidRequest
)

// The following vars are implementation details and should not be used by a consumer of this lib.
var (
	// errEmailInvalid indicates that the email address is invalid.
	errEmailInvalid = errors.New("email address is invalid")
	// errJWTNotVerified indicates that the JWT verification failed.
	errJWTNotVerified = errors.New("the jwt was not able to be verified")

	// errNoJWT indicates that there was no JWT found.
	errNoJWT = errors.New("no jwt found")
	// errJWTUnmarshal indicates that the JWT could not be unmarshalled.
	errJWTUnmarshal = errors.New("the jwt could not be unmarshalled")
	// errCookiesParse indicates that the cookie string could not be parsed.
	errCookiesParse = errors.New("failed to parse the provivided cookies")
	// errNoIntrinsicCookie indicates that the request had no user auth cookie in the expected locations.
	errNoIntrinsicCookie = errors.New("request has no auth-proxy or onprem-token or portal-token cookie (try refreshing your browser)")
	// errNoMetadata indicates that the request had no metadata.
	errNoMetadata = errors.New("no metadata found")
	// errNoOrgIDCookie indicates that the request had no org-id cookie.
	errNoOrgIDCookie = errors.New("org-id cookie missing")
	// errOrdIDEmpty indicates that the org-id cookie was empty.
	errOrgIDEmpty = errors.New("org-id cookie is empty")
	// errMetadataKeyConflict indicates that multiple possible values were found in context metadata for a single key.
	errMetadataKeyConflict = errors.New("multiple possible values found in context metadata for a single key")
	// errOrgInMetadata indicates that the org-id was found as metadata key, it should be a cookie instead.
	errOrgInMetadata = errors.New("org-id found in metadata keys, use org-id cookie instead")
	// errNoProjectInMetadata indicates that there was no project-id found in metadata.
	errNoProjectInMetadata = errors.New("project-id metadata key missing")
	// errNoProjectInHeader indicates that there was no project-id found in headers.
	errNoProjectInHeader = errors.New("x-intrinsic-auth-project header missing")

	emailRegex     = regexp.MustCompile(`(^(?P<prefix>[^@]+)@(?P<domain>.+)$)`)
	obfuscateRegex = regexp.MustCompile(`(^(.).*(.)$)`)
)

// ErrGRPC converts errors from the identity package to the corresponding gRPC error.
func ErrGRPC(err error) error {
	return clientcontext.ErrGRPC(err)
}

// ErrHTTP converts errors from the identity package to the corresponding HTTP status code and writes it to the response writer.
func ErrHTTP(w http.ResponseWriter, err error) {
	if err == nil {
		return
	}
	switch {
	case errors.Is(err, ErrMissingOrgID), errors.Is(err, ErrInvalidRequest):
		http.Error(w, err.Error(), http.StatusBadRequest)
	case errors.Is(err, ErrUnauthenticated):
		http.Error(w, err.Error(), http.StatusUnauthorized)
	default:
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

// User represents a user inside the Intrinsic stack.
type User struct {
	jwt string
	// Unmarshalled jwt cache.
	data *jwt.Data
	// Org is the organization of the user, populated dynamically when extracted from context/request.
	org *org.Organization
	// Project is the compute project of the user, populated dynamically when extracted from context/request.
	project string
}

// Org returns the organization of the user.
func (i *User) Org() *org.Organization {
	if i == nil {
		return nil
	}
	return i.org
}

// Project returns the compute project of the user.
func (i *User) Project() string {
	if i == nil {
		return ""
	}
	return i.project
}

const (
	// authProxyCookieName is the name of the cookie for storing the auth proxy token
	authProxyCookieName   = "auth-proxy"
	onpremTokenCookieName = "onprem-token"
	portalCookieName      = "portal-token"
	authHeaderName        = "authorization"
	// apikeyTokenHeaderName is the metadata key for api key-based authorization
	apikeyTokenHeaderName = "apikey-token"
	authProjectHeaderName = "x-intrinsic-auth-project"

	// Real user accounts have @google.com email addresses
	googleEmailSuffix = "@google.com"
	// IntrinsicServiceAccountEmailSuffix is the email domain for service accounts (such as automated processes)
	IntrinsicServiceAccountEmailSuffix = "@serviceaccount.intrinsic.ai"
	// IntrinsicTestAccountEmailSuffix is the email domain for owned test accounts (OTA, see go/rhea)
	IntrinsicTestAccountEmailSuffix = "@gmail.com"

	// IntrinsicIPCEmailSuffix is the email domain for IPC accounts.
	IntrinsicIPCEmailSuffix = "@ipc.intrinsic.ai"
)

var cookieHeaders = []string{authProxyCookieName, onpremTokenCookieName, portalCookieName}

// GetJWTFromRequest returns the JWT from a request.
func GetJWTFromRequest(r *http.Request) (string, error) {
	_, span := trace.StartSpan(r.Context(), "identity.GetJWTFromRequest")
	defer span.End()

	for _, cn := range cookieHeaders {
		jwt, err := r.Cookie(cn)
		if err == nil && jwt.Value != "" {
			log.V(2).Infof("Using jwt from cookie %q", cn)
			return jwt.Value, nil
		}
	}
	if token := r.Header.Get(apikeyTokenHeaderName); token != "" {
		log.V(2).Infof("Using jwt from header %q", apikeyTokenHeaderName)
		return token, nil
	}
	// Retrieving the JWT from the authorization header.
	// The authorization header is set for service-to-service communication.
	if token := trimBearer(r.Header.Get(authHeaderName)); token != "" {
		log.V(2).Infof("Using jwt from header %q", authHeaderName)
		return token, nil
	}
	telemetry.SetError(span, trace.StatusCodeUnauthenticated, "GetJWTFromRequest", errNoJWT)
	return "", errors.Join(ErrUnauthenticated, errNoJWT)
}

// GetJWTFromContext returns the JWT from a context.
func GetJWTFromContext(ctx context.Context) (string, error) {
	_, span := trace.StartSpan(ctx, "identity.GetJWTFromContext")
	defer span.End()

	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		telemetry.SetError(span, trace.StatusCodeUnauthenticated, "GetJWTFromContext", errNoMetadata)
		return "", errors.Join(ErrUnauthenticated, errNoMetadata)
	}

	contextCookies, err := cookies.FromContext(ctx)
	if err != nil {
		telemetry.SetError(span, trace.StatusCodeInvalidArgument, "GetJWTFromContext: Failed to parse cookies", errCookiesParse)
		return "", errors.Join(ErrUnauthenticated, errCookiesParse, err)
	}
	for _, c := range contextCookies {
		for _, cn := range cookieHeaders {
			if c.Name == cn && c.Value != "" {
				return c.Value, nil
			}
		}
	}
	// Retrieving the JWT from the apikey-token header.
	if jwtMD, ok := md[apikeyTokenHeaderName]; ok && len(jwtMD) > 0 && jwtMD[0] != "" {
		return jwtMD[0], nil
	}
	// Retrieving the JWT from the authorization header.
	// The authorization header is set for service-to-service communication.
	if tks, ok := md[authHeaderName]; ok && len(tks) > 0 && trimBearer(tks[0]) != "" {
		return trimBearer(tks[0]), nil
	}
	telemetry.SetError(span, trace.StatusCodeUnauthenticated, "GetJWTFromContext", errNoJWT)
	return "", errors.Join(ErrUnauthenticated, errNoJWT)
}

// trimBearer strips the "Bearer " or "bearer " prefix from the authorization header.
// Returns empty string if no prefix is there.
func trimBearer(authHeader string) string {
	if strings.HasPrefix(authHeader, "Bearer ") {
		return strings.TrimPrefix(authHeader, "Bearer ")
	}
	if strings.HasPrefix(authHeader, "bearer ") {
		return strings.TrimPrefix(authHeader, "bearer ")
	}
	return ""
}

// TokenVerifier verifies the given JWT.
// Required for From{Request,Context}Verified.
type TokenVerifier interface {
	VerifyIDToken(ctx context.Context, token string) error
}

func extractUserOptionsFromContext(ctx context.Context) ([]UserOption, error) {
	results, err := extractFromContext(ctx, userExtractionSpecs...)
	if err != nil {
		return nil, err
	}

	var opts []UserOption
	if orgID := results["Org"]; orgID != "" {
		opts = append(opts, WithUserOrg(orgID))
	}
	if projectID := results["Project"]; projectID != "" {
		opts = append(opts, WithUserProject(projectID))
	}
	return opts, nil
}

func extractUserOptionsFromRequest(r *http.Request) ([]UserOption, error) {
	results, err := extractFromRequest(r, userExtractionSpecs...)
	if err != nil {
		return nil, err
	}

	var opts []UserOption
	if orgID := results["Org"]; orgID != "" {
		opts = append(opts, WithUserOrg(orgID))
	}
	if projectID := results["Project"]; projectID != "" {
		opts = append(opts, WithUserProject(projectID))
	}
	return opts, nil
}

// UserFromContextVerified returns the verified user's identity from an incoming context.
func UserFromContextVerified(ctx context.Context, tv TokenVerifier) (*User, error) {
	ctx, span := trace.StartSpan(ctx, "identity.UserFromContextVerified")
	defer span.End()

	t, err := GetJWTFromContext(ctx)
	if err != nil {
		return nil, err
	}
	if err := tv.VerifyIDToken(ctx, t); err != nil {
		telemetry.SetError(span, trace.StatusCodeUnauthenticated, "UserFromContextVerified: Failed to verify JWT", err)
		return nil, errors.Join(ErrUnauthenticated, errJWTNotVerified, err)
	}
	opts, err := extractUserOptionsFromContext(ctx)
	if err != nil {
		return nil, err
	}
	opts = append(opts, WithJWT(t))
	return NewUser(opts...)
}

// UserFromRequestVerified returns the verified user's identity from an incoming HTTP request.
func UserFromRequestVerified(r *http.Request, tv TokenVerifier) (*User, error) {
	_, span := trace.StartSpan(r.Context(), "identity.UserFromRequestVerified")
	defer span.End()

	jwt, err := GetJWTFromRequest(r)
	if err != nil {
		return nil, err
	}
	if err := tv.VerifyIDToken(r.Context(), jwt); err != nil {
		telemetry.SetError(span, trace.StatusCodeUnauthenticated, "UserFromRequestVerified: Failed to verify JWT", err)
		return nil, errors.Join(ErrUnauthenticated, errJWTNotVerified, err)
	}
	opts, err := extractUserOptionsFromRequest(r)
	if err != nil {
		return nil, err
	}
	opts = append(opts, WithJWT(jwt))
	return NewUser(opts...)
}

// UserFromRequest returns the user's identity from an HTTP request.
// No verification of the given JWT is performed.
func UserFromRequest(r *http.Request) (*User, error) {
	_, span := trace.StartSpan(r.Context(), "identity.UserFromRequest")
	defer span.End()

	jwt, err := GetJWTFromRequest(r)
	if err != nil {
		return nil, err
	}
	opts, err := extractUserOptionsFromRequest(r)
	if err != nil {
		return nil, err
	}
	opts = append(opts, WithJWT(jwt))
	return NewUser(opts...)
}

// UserFromContext returns the user's identity from a gRPC context.
// No verification of the given JWT is performed.
func UserFromContext(ctx context.Context) (*User, error) {
	ctx, span := trace.StartSpan(ctx, "identity.UserFromContext")
	defer span.End()

	t, err := GetJWTFromContext(ctx)
	if err != nil {
		return nil, err
	}
	opts, err := extractUserOptionsFromContext(ctx)
	if err != nil {
		return nil, err
	}
	opts = append(opts, WithJWT(t))
	return NewUser(opts...)
}

// Email retrieves the canonalized user or service email of an identity.
// Alias for EmailCanonicalized.
// Deprecated: use EmailCanonicalized instead or better EmailRaw for non-ACL use cases.
func (i *User) Email() string {
	return i.EmailCanonicalized()
}

// EmailRaw retrieves the user's email as it is stored in the JWT (uncanonicalized).
// The JWT stores the primary email address of the user as defined by the login provider.
func (i *User) EmailRaw() string {
	m, _ := jwt.Email(i.jwt) // verified when created
	return m
}

// EmailCanonicalized retrieves the canonicalized user's email.
// Only use for ACL lookups.
// We are removing canonalization and ACLs are the only use case left for it.
func (i *User) EmailCanonicalized() string {
	m, _ := jwt.Email(i.jwt)    // verified when created
	m, _ = CanonicalizeEmail(m) // only error would be if m is not an email, can't happen here
	return m
}

func (i *User) lazyUnmarshalAndReturnData() (*jwt.Data, error) {
	if i.data != nil {
		return i.data, nil
	}
	d, err := jwt.UnmarshalUnsafe(i.jwt)
	if err != nil {
		return nil, errors.Join(ErrUnauthenticated, errJWTUnmarshal, err)
	}
	i.data = d // last write wins for parallel access
	return i.data, nil
}

// LoggableID returns an obfuscated name by which to identify a user in a log message.
func (i *User) LoggableID() string {
	submatches := emailRegex.FindStringSubmatch(i.EmailCanonicalized())
	var prefix, domain string
	if submatches == nil {
		prefix = ""
		domain = ""
	} else {
		groups := emailRegex.SubexpNames()
		prefix = submatches[slices.Index(groups, "prefix")]
		domain = submatches[slices.Index(groups, "domain")]
	}

	return fmt.Sprintf("%s@%s", obfuscateString(prefix), obfuscateString(domain))
}

// FirebaseUserID retrieves the user's user_id as defined by Firebase Auth.
// This is not named UserID or UID because we may want to introduce a different ID in the future
// which will be stored as claims in the JWT.
func (i *User) FirebaseUserID() string {
	dt, err := i.lazyUnmarshalAndReturnData()
	if err != nil {
		return ""
	}
	return dt.UserID
}

// JWTData retrieves the user's JWT data as defined by the JWT spec.
func (i *User) JWTData() (*jwt.Data, error) {
	return i.lazyUnmarshalAndReturnData()
}

// IsIntrinsicUser tests whether a user belongs to the Intrinsic organization.
// This is a temporary function for cases where we have no ACL checks yet.
func (i *User) IsIntrinsicUser() bool {
	return isIntrinsicUser(strings.ToLower(i.EmailRaw()))
}

func isIntrinsicUser(email string) bool {
	return strings.HasSuffix(email, googleEmailSuffix) ||
		strings.HasSuffix(email, IntrinsicServiceAccountEmailSuffix)
}

// RequestToContext returns a new context that has the user's identity, audience, and org-id stored in its
// metadata. The signature of the JWT is not verified.
// Use [RequestToContext] when chaining HTTP -> GRPC.
// Prefer [RequestToIncomingContext] over [RequestToContext] to avoid oversharing data with downstream services.
// See [ToContextFromIncoming] when chaining GRPC -> GRPC.
// Set the ctx parameter to r.Context() if there is no other more suitable context (e.g. a trace
// span derived from r.Context()).
// We require the user JWT to be on metadata["Cookie"]["auth-proxy"] because that is where the
// auth proxy service reads it from.
func RequestToContext(ctx context.Context, r *http.Request) (context.Context, error) {
	lctx, span := trace.StartSpan(ctx, "identity.RequestToContext")
	defer span.End()

	md, err := requestToMetadata(lctx, r)
	if err != nil {
		telemetry.SetError(span, trace.StatusCodeInvalidArgument, "RequestToContext: Failed to transform http requests identity to metadata", err)
		return ctx, err
	}
	return metadata.AppendToOutgoingContext(ctx, md...), nil
}

// RequestToIncomingContext returns a new context that has the user's identity, audience, and org-id stored in its
// metadata. The signature of the JWT is not verified.
func RequestToIncomingContext(ctx context.Context, r *http.Request) (context.Context, error) {
	lctx, span := trace.StartSpan(ctx, "identity.RequestToIncomingContext")
	defer span.End()

	md, err := requestToMetadata(lctx, r)
	if err != nil {
		return ctx, err
	}
	return metadata.NewIncomingContext(ctx, metadata.Pairs(md...)), nil
}

func requestToMetadata(ctx context.Context, r *http.Request) ([]string, error) {
	ctx, span := trace.StartSpan(ctx, "identity.requestToMetadata")
	defer span.End()

	userJWT, err := GetJWTFromRequest(r)
	if err != nil {
		return nil, err
	}
	// Ensure that authProxyCookieName is set to GetJWTFromRequest if it is not set yet.
	// This way downstream services do not have to check for the onpremTokenCookieName.
	if _, err := r.Cookie(authProxyCookieName); err != nil { // if not set yet, add it
		cookies.AddToRequest(r, &http.Cookie{Name: authProxyCookieName, Value: userJWT})
	}

	// Copy the relevant cookies to the context metadata
	cs := cookies.FromRequestNamed(r, []string{authProxyCookieName, org.OrgIDCookie})
	// backfill org-id cookie if it is not set, but passed as a header
	if _, err := r.Cookie(org.OrgIDCookie); err != nil {
		orgID := r.Header.Get(org.OrgIDCookie)
		if orgID != "" {
			log.WarningContextf(ctx, "Legacy org-id detected, request=%v, %s", r.URL, origin.Description(r))
			cs = append(cs, &http.Cookie{Name: org.OrgIDCookie, Value: orgID})
		} else {
			log.WarningContextf(ctx, "No org-id in request, request=%v, %s", r.URL, origin.Description(r))
		}
	}
	md := cookies.ToMDString(cs...)

	// Copy organization header to the context metadata
	oID, err := extractRequestHeader(r, org.OrgIDHeader)
	if err != nil {
		return nil, err
	}
	if oID != "" {
		md = append(md, org.OrgIDHeader, oID)
	}

	// Copy the audience to the context metadata.
	if aud, err := jwt.Aud(userJWT); err == nil {
		md = append(md, authProjectHeaderName, aud)
	} else {
		log.WarningContext(ctx, "no aud info in request")
	}
	return md, err
}

// ContextToRequest adds the auth related metadata from a GRPC context to an http request.
// Use ToRequest when chaining GRPC -> HTTP.
func ContextToRequest(ctx context.Context, r *http.Request) error {
	ctx, span := trace.StartSpan(ctx, "identity.ContextToRequest")
	defer span.End()

	cookiesToCopy := []string{authProxyCookieName, portalCookieName, org.OrgIDCookie}
	// Copy the relevant cookies to the context request
	possibleCookies, err := cookies.FromContext(ctx)
	if err != nil {
		telemetry.SetError(span, trace.StatusCodeInvalidArgument, "ContextToRequest: Failed to get cookies from context", err)
		return errors.Join(ErrUnauthenticated, errCookiesParse, err)
	}
	var filteredCookies []*http.Cookie
	for _, c := range possibleCookies {
		if slices.Contains(cookiesToCopy, c.Name) {
			filteredCookies = append(filteredCookies, c)
		}
	}
	cookies.AddToRequest(r, filteredCookies...)

	metaToCopy := []string{apikeyTokenHeaderName, org.OrgIDHeader}
	md, _ := metadata.FromIncomingContext(ctx)
	for _, m := range metaToCopy {
		if val := md.Get(m); len(val) == 1 {
			r.Header.Set(m, val[0])
		}
	}

	return nil
}

// EnsureAuthProxyCookie ensures that the input request has an auth-proxy token cookie, copying it
// from the onprem-token cookie if needed.
func EnsureAuthProxyCookie(r *http.Request) error {
	_, span := trace.StartSpan(r.Context(), "identity.EnsureAuthProxyCookie")
	defer span.End()

	// Return early if the auth-proxy cookie already exists.
	if _, err := r.Cookie(authProxyCookieName); err == nil {
		return nil
	}

	// Try to copy the onprem-token or portal-token cookie to the auth-proxy cookie.
	for _, cn := range []string{onpremTokenCookieName, portalCookieName} {
		c, err := r.Cookie(cn)
		if err != nil {
			continue
		}
		cookies.AddToRequest(r, &http.Cookie{Name: authProxyCookieName, Value: c.Value})
		break
	}
	if _, err := r.Cookie(authProxyCookieName); err != nil {
		telemetry.SetError(span, trace.StatusCodeUnauthenticated, "EnsureAuthProxyCookie: No auth-proxy cookie found in request", err)
		return errors.Join(ErrUnauthenticated, errNoIntrinsicCookie, err)
	}

	return nil
}

var (
	// TODO(ensonic): should we add '.' to both and only drop duplicate, leading and trailing dots?
	// see gaia/data/email_util.cc;l=221
	stripNonEmailChars = regexp.MustCompile(`[^a-zA-Z0-9!#$%&'*+\-/=?^_{|}~` + "`" + `]`)
	// see gaia/data/email_util.cc;l=227
	stripNonGmailChars = regexp.MustCompile(`[^a-zA-Z0-9]`)
)

// CanonicalizeEmail ensures that different valid forms of emails map to the same user account.
func CanonicalizeEmail(email string) (string, error) {
	// convert everything to lowercase (RFC 5321)
	parts := strings.Split(strings.ToLower(email), "@")
	if len(parts) != 2 {
		return "", fmt.Errorf("%w: email address has an incorrect number of '@' character: %q", errEmailInvalid, email)
	}
	user, provider := parts[0], parts[1]
	if user == "" {
		return "", fmt.Errorf("%w: email address is missing the user part: %q", errEmailInvalid, email)
	}
	if provider == "" {
		return "", fmt.Errorf("%w: email address is missing the provider part: %q", errEmailInvalid, email)
	}

	// canonicalize provider
	if provider == "googlemail.com" {
		provider = "gmail.com"
	}

	// canonicalize user
	// cut everything starting with '+' on the part before the @ (including the '+') (RFC 5233)
	user = strings.SplitN(user, "+", 2)[0]

	// canonicalize user based on provider
	if provider == "gmail.com" {
		// replace all non gmail-supported characters before the @ sign
		user = stripNonGmailChars.ReplaceAllString(user, "")
	} else {
		// replace all non email-supported characters before the @ sign
		user = stripNonEmailChars.ReplaceAllString(user, "")
	}

	return user + "@" + provider, nil
}

// Note: We are removing this as there should not be a need for clients using SDK
// to use any kind of metadata conversion methods.

// ToContextFromIncoming copies auth-related incoming GRPC metadata to outgoing
// metadata. The method does not error if auth-related information is not
// present. Use [ToContextFromIncomingChecked] to check if the context contains
// incoming authentication info. See [ToContextFromIncomingChecked] for more
// details.
func ToContextFromIncoming(ctx context.Context) (context.Context, error) {
	return clientcontext.ToContextFromIncoming(ctx)
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
	return clientcontext.ToContextFromIncomingChecked(ctx)
}

func obfuscateString(s string) string {
	if len(s) < 5 {
		return "***"
	}
	return obfuscateRegex.ReplaceAllString(s, `$2***$3`)
}

// ClearRequest removes the user and org metadata from the HTTP request.
func ClearRequest(r *http.Request) {
	_, span := trace.StartSpan(r.Context(), "identity.ClearRequest")
	defer span.End()

	ToRequest(r, WithClearUser())
}

func filterCookies(cookies []*http.Cookie, filter []string) []*http.Cookie {
	var validCookies []*http.Cookie
	for _, c := range cookies {
		if !slices.Contains(filter, c.Name) {
			validCookies = append(validCookies, c)
		}
	}
	return validCookies
}

// ClearContext removes the user and org metadata from the outgoing context.
func ClearContext(ctx context.Context) (context.Context, error) {
	_, span := trace.StartSpan(ctx, "identity.ClearContext")
	defer span.End()

	return AppendToOutgoingContext(ctx, WithClearUser())
}

// IPCEmail returns the IPC email based on its identifier.
// Example: "my-robot" -> "my-robot@ipc.intrinsic.ai"
func IPCEmail(name string) string {
	return name + IntrinsicIPCEmailSuffix
}

// UserOption configures a User struct during construction.
type UserOption func(*User)

// WithJWT sets the user's raw JWT.
func WithJWT(token string) UserOption {
	return func(u *User) {
		u.jwt = token
	}
}

// WithUserOrg sets the organization of the user.
func WithUserOrg(orgID string) UserOption {
	return func(u *User) {
		if orgID != "" {
			u.org = &org.Organization{ID: orgID}
		}
	}
}

// WithUserProject sets the compute project of the user.
func WithUserProject(projectID string) UserOption {
	return func(u *User) {
		u.project = projectID
	}
}

// NewUser constructs a User with the provided options.
// A JWT is required to construct a valid User.
func NewUser(opts ...UserOption) (*User, error) {
	u := &User{}
	for _, opt := range opts {
		opt(u)
	}
	if u.jwt == "" {
		return nil, fmt.Errorf("%w: JWT is required to construct a User", ErrInvalidRequest)
	}
	_, err := jwt.Email(u.jwt)
	if err != nil {
		return nil, fmt.Errorf("%w: failed to get email from JWT: %v", ErrUnauthenticated, err)
	}
	return u, nil
}

// UserFromJWT retrieves an Identity from a given JWT.
// Deprecated: use NewUser with options.
func UserFromJWT(t string) (*User, error) {
	return NewUser(WithJWT(t))
}

// Option configures the context/request propagation.
type Option func(*update) error

type update struct {
	headers      map[string]string
	cookies      []*http.Cookie
	clearHeaders []string
	clearCookies []string
}

func (u *update) addHeader(k, v string) {
	if u.headers == nil {
		u.headers = make(map[string]string)
	}
	u.headers[k] = v
}

func (u *update) addCookie(c *http.Cookie) {
	if c != nil {
		u.cookies = append(u.cookies, c)
	}
}

func (u *update) clearHeader(k string) {
	u.clearHeaders = append(u.clearHeaders, k)
}

func (u *update) clearCookie(k string) {
	u.clearCookies = append(u.clearCookies, k)
}

func (u *update) applyToMD(md metadata.MD) (metadata.MD, error) {
	newMD := md.Copy()
	if newMD == nil {
		newMD = metadata.MD{}
	}

	// 1. Apply all header changes first
	for _, kh := range u.clearHeaders {
		newMD.Delete(kh)
	}
	for k, v := range u.headers {
		newMD.Set(k, v)
	}

	// 2. Guard: If no cookie changes are needed, return early
	if len(u.clearCookies) == 0 && len(u.cookies) == 0 {
		return newMD, nil
	}

	// 3. Guard: If we only have additions (no clearing), handle and return early
	if len(u.clearCookies) == 0 {
		return cookies.AddToMD(newMD, u.cookies...)
	}

	// 4. Handle clearing (and potential merging with additions)
	existing, err := cookies.FromMD(newMD)
	if err != nil {
		return nil, err
	}

	finalCookies := filterCookies(existing, u.clearCookies)
	finalCookies = append(finalCookies, u.cookies...)
	newMD.Delete(cookies.CookieHeaderName)
	return cookies.AddToMD(newMD, finalCookies...)
}

func withCookie(c *http.Cookie) Option {
	return func(u *update) error {
		u.addCookie(c)
		return nil
	}
}

func withHeader(k, v string) Option {
	return func(u *update) error {
		u.addHeader(k, v)
		return nil
	}
}

// WithOrg scopes the organization identifier.
func WithOrg(orgID string) Option {
	return func(u *update) error {
		if orgID == "" {
			return ErrMissingOrgID
		}
		if err := withCookie(org.IDCookie(orgID))(u); err != nil {
			return err
		}
		return withHeader(org.OrgIDHeader, orgID)(u)
	}
}

// WithUserJWT scopes the user's JWT token.
func WithUserJWT(jwt string) Option {
	return func(u *update) error {
		if jwt == "" {
			return ErrUnauthenticated
		}
		return withCookie(&http.Cookie{Name: authProxyCookieName, Value: jwt})(u)
	}
}

// WithComputeProject scopes the compute project identifier.
func WithComputeProject(projectID string) Option {
	return func(u *update) error {
		if projectID == "" {
			return ErrMissingProject
		}
		return withHeader(authProjectHeaderName, projectID)(u)
	}
}

// WithUser scopes the user's identity, organization, and compute project.
func WithUser(u *User) Option {
	return func(up *update) error {
		if u == nil {
			return fmt.Errorf("%w: user cannot be nil", ErrInvalidRequest)
		}
		if err := WithUserJWT(u.jwt)(up); err != nil {
			return err
		}
		if u.org != nil {
			if err := WithOrg(u.org.ID)(up); err != nil {
				return err
			}
		}
		if u.project != "" {
			if err := WithComputeProject(u.project)(up); err != nil {
				return err
			}
		}
		return nil
	}
}

// WithClearUser removes all user identity credentials, API keys, and
// authentication cookies from the context or request.
//
// Note: if options to add identity information are also present, this will
// clear identity data first, then add the new data. This allows clients to
// clear -> add in a single call, instead of two separate calls.
func WithClearUser() Option {
	return func(u *update) error {
		if err := withClearUserOrg()(u); err != nil {
			return err
		}
		return withClearUserAuth()(u)
	}
}

func withClearUserOrg() Option {
	return func(u *update) error {
		u.clearHeader(org.OrgIDHeader)
		u.clearHeader(org.OrgIDCookie) // clears legacy org-id header
		u.clearCookie(org.OrgIDCookie) // clears org-id cookie
		return nil
	}
}

func withClearUserAuth() Option {
	return func(u *update) error {
		u.clearHeader(authHeaderName)
		u.clearHeader(apikeyTokenHeaderName)
		u.clearHeader(authProjectHeaderName)
		for _, ch := range cookieHeaders {
			u.clearCookie(ch)
		}
		return nil
	}
}

// NewOutgoingContext creates a new outgoing context with the provided options.
func NewOutgoingContext(ctx context.Context, opts ...Option) (context.Context, error) {
	if len(opts) == 0 {
		return ctx, nil
	}

	_, span := trace.StartSpan(ctx, "identity.NewOutgoingContext")
	defer span.End()

	u := &update{}
	for _, opt := range opts {
		if err := opt(u); err != nil {
			return ctx, err
		}
	}

	newMD, err := u.applyToMD(metadata.MD{})
	if err != nil {
		return ctx, err
	}

	return metadata.NewOutgoingContext(ctx, newMD), nil
}

// AppendToOutgoingContext appends/merges the provided options into the outgoing context metadata.
func AppendToOutgoingContext(ctx context.Context, opts ...Option) (context.Context, error) {
	if len(opts) == 0 {
		return ctx, nil
	}

	_, span := trace.StartSpan(ctx, "identity.AppendToOutgoingContext")
	defer span.End()

	md, _ := metadata.FromOutgoingContext(ctx)
	newMD, err := AppendToMetadata(md, opts...)
	if err != nil {
		return ctx, err
	}

	return metadata.NewOutgoingContext(ctx, newMD), nil
}

// ToRequest modifies the HTTP request headers and cookies based on options.
func ToRequest(r *http.Request, opts ...Option) error {
	if r == nil {
		return fmt.Errorf("%w: request cannot be nil", ErrInvalidRequest)
	}
	if len(opts) == 0 {
		return nil
	}

	_, span := trace.StartSpan(r.Context(), "identity.ToRequest")
	defer span.End()

	u := &update{}
	for _, opt := range opts {
		if err := opt(u); err != nil {
			return err
		}
	}

	for _, kh := range u.clearHeaders {
		r.Header.Del(kh)
	}

	if len(u.clearCookies) > 0 {
		cs := filterCookies(r.Cookies(), u.clearCookies)
		r.Header.Del(cookies.CookieHeaderName)
		cookies.AddToRequest(r, cs...)
	}

	for k, v := range u.headers {
		r.Header.Set(k, v)
	}

	if len(u.cookies) > 0 {
		cookies.AddToRequest(r, u.cookies...)
	}
	return nil
}

// AppendToIncomingContext appends/merges the provided options to the incoming gRPC context.
func AppendToIncomingContext(ctx context.Context, opts ...Option) (context.Context, error) {
	if len(opts) == 0 {
		return ctx, nil
	}

	_, span := trace.StartSpan(ctx, "identity.AppendToIncomingContext")
	defer span.End()

	md, _ := metadata.FromIncomingContext(ctx)
	newMD, err := AppendToMetadata(md, opts...)
	if err != nil {
		return ctx, err
	}

	return metadata.NewIncomingContext(ctx, newMD), nil
}

// AppendToMetadata merges the provided identity options into an existing gRPC metadata map.
func AppendToMetadata(md metadata.MD, opts ...Option) (metadata.MD, error) {
	if len(opts) == 0 {
		return md, nil
	}

	u := &update{}
	for _, opt := range opts {
		if err := opt(u); err != nil {
			return md, err
		}
	}

	return u.applyToMD(md)
}

// ToMetadata serializes the provided identity options directly into a gRPC metadata map.
func ToMetadata(opts ...Option) (metadata.MD, error) {
	return AppendToMetadata(nil, opts...)
}

// AppendToMetadataMap merges identity options into an existing flat metadata map.
func AppendToMetadataMap(m map[string]string, opts ...Option) (map[string]string, error) {
	if len(opts) == 0 {
		return m, nil
	}

	md := make(metadata.MD, len(m))
	for k, v := range m {
		md.Set(k, v)
	}

	newMD, err := AppendToMetadata(md, opts...)
	if err != nil {
		return m, err
	}

	if m == nil {
		m = make(map[string]string, len(newMD))
	} else {
		for k := range m {
			delete(m, k)
		}
	}
	for k, vs := range newMD {
		m[k] = strings.Join(vs, ", ")
	}
	return m, nil
}

// ToMetadataMap flattens identity options into a metadata key-value map from scratch.
func ToMetadataMap(opts ...Option) (map[string]string, error) {
	return AppendToMetadataMap(nil, opts...)
}

func deduplicate(s []string) []string {
	if len(s) <= 1 {
		return s
	}
	seen := make(map[string]struct{})
	result := make([]string, 0, len(s))
	for _, val := range s {
		if _, ok := seen[val]; !ok {
			seen[val] = struct{}{}
			result = append(result, val)
		}
	}
	return result
}

// OrgFromContext extracts the organization identifier from the gRPC context.
// If a user identity is present, prefer to use UserFromContext and User.Org().
func OrgFromContext(ctx context.Context) (*org.Organization, error) {
	results, err := extractFromContext(ctx, orgExtractionSpec)
	if err != nil {
		return nil, err
	}
	orgID := results["Org"]
	if orgID == "" {
		return nil, ErrMissingOrgID
	}
	return &org.Organization{ID: orgID}, nil
}

// OrgFromRequest extracts the organization identifier from the HTTP request.
// If a user identity is present, prefer to use UserFromRequest and User.Org().
func OrgFromRequest(r *http.Request) (*org.Organization, error) {
	results, err := extractFromRequest(r, orgExtractionSpec)
	if err != nil {
		return nil, err
	}
	orgID := results["Org"]
	if orgID == "" {
		return nil, ErrMissingOrgID
	}
	return &org.Organization{ID: orgID}, nil
}

type keyType int

const (
	headerKey keyType = iota
	cookieKey
)

type keySpec struct {
	Type keyType
	Name string
}

type extractionSpec struct {
	FieldName string
	Keys      []keySpec
}

var (
	orgExtractionSpec = extractionSpec{
		FieldName: "Org",
		Keys: []keySpec{
			// TODO(b/325040410) Propagate org-id only as cookie, not header
			{headerKey, org.OrgIDHeader},
			// TODO(b/346714117): Remove explicit metadata entry.
			{cookieKey, org.OrgIDCookie},
		},
	}
	projectExtractionSpec = extractionSpec{
		FieldName: "Project",
		Keys: []keySpec{
			{headerKey, authProjectHeaderName},
		},
	}
	userExtractionSpecs = []extractionSpec{
		orgExtractionSpec,
		projectExtractionSpec,
	}
)

func extractMetadataHeader(md metadata.MD, key string) (string, error) {
	vals := md.Get(key)
	vals = deduplicate(vals)
	if len(vals) > 1 {
		return "", fmt.Errorf("multiple values specified in %q header", key)
	}
	if len(vals) == 1 && vals[0] != "" {
		return vals[0], nil
	}
	return "", nil
}

func extractFromContext(ctx context.Context, specs ...extractionSpec) (map[string]string, error) {
	results := make(map[string]string)

	var md metadata.MD
	var hasMetadata bool
	var contextCookies []*http.Cookie
	var cookiesErr error

	for _, spec := range specs {
		var foundVal string
	KeysLoop:
		for _, key := range spec.Keys {
			switch key.Type {
			case headerKey:
				if md == nil {
					md, hasMetadata = metadata.FromIncomingContext(ctx)
				}
				if hasMetadata {
					val, err := extractMetadataHeader(md, key.Name)
					if err != nil {
						return nil, fmt.Errorf("field %s: %w", spec.FieldName, err)
					}
					if val != "" {
						foundVal = val
						break KeysLoop
					}
				}
			case cookieKey:
				if contextCookies == nil && cookiesErr == nil {
					contextCookies, cookiesErr = cookies.FromContext(ctx)
				}
				if cookiesErr == nil {
					for _, cookie := range contextCookies {
						if cookie.Name == key.Name && cookie.Value != "" {
							foundVal = cookie.Value
							break KeysLoop
						}
					}
				}
			}
		}
		results[spec.FieldName] = foundVal
	}
	return results, nil
}

func extractRequestHeader(r *http.Request, key string) (string, error) {
	vals := r.Header.Values(key)
	vals = deduplicate(vals)
	if len(vals) > 1 {
		return "", fmt.Errorf("multiple values specified in %q header", key)
	}
	if len(vals) == 1 && vals[0] != "" {
		return vals[0], nil
	}
	return "", nil
}

func extractFromRequest(r *http.Request, specs ...extractionSpec) (map[string]string, error) {
	if r == nil {
		return nil, nil
	}
	results := make(map[string]string)
	for _, spec := range specs {
		var foundVal string
	KeysLoop:
		for _, key := range spec.Keys {
			switch key.Type {
			case headerKey:
				val, err := extractRequestHeader(r, key.Name)
				if err != nil {
					return nil, fmt.Errorf("field %s: %w", spec.FieldName, err)
				}
				if val != "" {
					foundVal = val
					break KeysLoop
				}
			case cookieKey:
				cookie, err := r.Cookie(key.Name)
				if err == nil && cookie.Value != "" {
					foundVal = cookie.Value
					break KeysLoop
				}
				// Legacy fallback specifically for Org ID in requests
				if key.Name == org.OrgIDCookie {
					orgName := r.Header.Get(org.OrgIDCookie)
					if orgName != "" {
						foundVal = orgName
						break KeysLoop
					}
				}
			}
		}
		results[spec.FieldName] = foundVal
	}
	return results, nil
}
