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

// Package cookies provides shared utility functions to deal with context and header embedded information.
package cookies

import (
	"context"
	"fmt"
	"maps"
	"net/http"
	"slices"
	"strings"

	log "github.com/golang/glog"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/metadata"
)

const (
	// CookieHeaderName is the name of the header / metadata field used for cookies
	CookieHeaderName = "Cookie"

	// grpcCookieHeaderName is the lower-cased version of the CookieHeaderName.
	// In gRPC metadata, all keys are normalized to lower-case.
	grpcCookieHeaderName = "cookie"
)

// parseCookies parses cookies from a string into a list of http.Cookie objects.
// Follows closely the semantics for HTTP requests.
func parseCookies(cookies string) ([]*http.Cookie, error) {
	if cookies == "" {
		return nil, nil
	}
	r, err := http.NewRequest("GET", "", nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create http request: %v", err)
	}
	r.Header.Set(CookieHeaderName, cookies)
	return r.Cookies(), nil
}

// FromRequestNamed returns the named cookies from a request.
// Returns only one cookie per name, ignores names that are not found.
func FromRequestNamed(r *http.Request, names []string) []*http.Cookie {
	_, span := trace.StartSpan(r.Context(), "cookies.FromRequestNamed")
	defer span.End()

	var cs []*http.Cookie
	for _, name := range names {
		if cookie, err := r.Cookie(name); err == nil {
			cs = append(cs, cookie)
		}
	}
	return cs
}

// AddToRequest adds cookies to the request and deduplicates already existing
// cookie key value pairs.
//
// It will overwrite existing cookies inside the request if they have the same
// name.
func AddToRequest(r *http.Request, newCs ...*http.Cookie) {
	_, span := trace.StartSpan(r.Context(), "cookies.AddToRequest")
	defer span.End()

	if r == nil {
		return
	}
	cookieMap := make(map[string]string)
	for _, c := range r.Cookies() {
		cookieMap[c.Name] = c.Value
	}
	for _, c := range newCs {
		cookieMap[c.Name] = c.Value
	}
	if r.Header == nil {
		r.Header = make(http.Header)
	}
	r.Header.Del(CookieHeaderName)
	// add cookies in a deterministic order
	for _, k := range slices.Sorted(maps.Keys(cookieMap)) {
		r.AddCookie(&http.Cookie{Name: k, Value: cookieMap[k]})
	}
}

// AddToContext adds cookies to the outgoing context, merging with existing
// cookies.
//
// Cookies with the same name will be overwritten.
func AddToContext(ctx context.Context, newCs ...*http.Cookie) (context.Context, error) {
	md, _ := metadata.FromOutgoingContext(ctx)
	md, err := AddToMD(md, newCs...)
	if err != nil {
		return ctx, err
	}
	return metadata.NewOutgoingContext(ctx, md), nil
}

// AddToIncomingContext adds cookies to the incoming context, merging with
// existing cookies.
//
// Cookies with the same name will be overwritten.
func AddToIncomingContext(ctx context.Context, newCs ...*http.Cookie) (context.Context, error) {
	md, _ := metadata.FromIncomingContext(ctx)
	md, err := AddToMD(md, newCs...)
	if err != nil {
		return ctx, err
	}
	return metadata.NewIncomingContext(ctx, md), nil
}

// AddToMD adds cookies to the gRPC metadata map, merging with existing cookies.
//
// Cookies with the same name will be overwritten.
func AddToMD(md metadata.MD, newCs ...*http.Cookie) (metadata.MD, error) {
	mergedList, err := mergeAndSortCookies(md.Get(CookieHeaderName), newCs)
	if err != nil {
		return nil, err
	}

	if len(mergedList) == 0 {
		return md, nil
	}

	res := ToMDString(mergedList...)
	if md == nil {
		md = metadata.MD{}
	}
	md.Set(res[0], res[1])
	return md, nil
}

// AddToMetadataMap adds cookies to a flat metadata map, merging with existing
// cookies.
//
// Cookies with the same name will be overwritten.
func AddToMetadataMap(m map[string]string, newCs ...*http.Cookie) (map[string]string, error) {
	if len(newCs) == 0 {
		return m, nil
	}

	if m == nil {
		m = make(map[string]string)
	}

	// Read existing cookies (handling potential casing variants)
	existingHeader := m[grpcCookieHeaderName]
	if existingHeader == "" {
		existingHeader = m[CookieHeaderName]
	}

	var existing []string
	if existingHeader != "" {
		existing = []string{existingHeader}
	}

	mergedList, err := mergeAndSortCookies(existing, newCs)
	if err != nil {
		return m, err
	}

	if len(mergedList) > 0 {
		res := ToMDString(mergedList...)
		delete(m, CookieHeaderName) // strip any uppercase keys
		m[grpcCookieHeaderName] = res[1]
	}
	return m, nil
}

// ToMDString converts a list of http.Cookie objects to a string that can be used as a metadata
// value.
func ToMDString(cs ...*http.Cookie) []string {
	var cookiesKV []string
	for _, c := range cs {
		cookiesKV = append(cookiesKV, (&http.Cookie{Name: c.Name, Value: c.Value}).String())
	}
	return []string{CookieHeaderName, strings.Join(cookiesKV, "; ")}
}

// FromContext extracts the "Cookies" from a GRPC incoming context.
// Cookie here refers to a mapped metadata that mirrors http cookies and is used to unify handling
// of http and GRPC based metadata in our stack.
func FromContext(ctx context.Context) ([]*http.Cookie, error) {
	// no tracing span here to reduce trace bloat (called in many request chains)
	md, ok := metadata.FromIncomingContext(ctx)
	// If there's no context, we have an empty list.
	if !ok {
		return nil, nil
	}

	return FromMD(md)
}

// FromOutgoingContext extracts the "Cookies" from a GRPC outgoing context.
// Cookie here refers to a mapped metadata that mirrors http cookies and is used to unify handling
// of http and GRPC based metadata in our stack.
func FromOutgoingContext(ctx context.Context) ([]*http.Cookie, error) {
	md, ok := metadata.FromOutgoingContext(ctx)
	if !ok {
		return nil, nil
	}
	return FromMD(md)
}

// FromMD extracts the "Cookies" from a GRPC metadata.MD.
// Cookie here refers to a mapped metadata that mirrors http cookies and is used to unify handling
// of http and GRPC based metadata in our stack.
func FromMD(md metadata.MD) ([]*http.Cookie, error) {
	cookies := md.Get(CookieHeaderName)
	// If there's no cookies set, it's an empty list.
	if len(cookies) == 0 {
		return nil, nil
	}

	// If there's more than one cookie header, we attempt to merge them.
	if len(cookies) > 1 {
		log.Warningf("Multiple cookie headers found, attempting to merge them...")
		cs, err := mergeCookies(cookies...)
		if err != nil {
			return nil, fmt.Errorf("failed to merge cookies: %v", err)
		}
		return cs, nil
	}

	p, err := parseCookies(cookies[0])
	if err != nil {
		return nil, fmt.Errorf("failed to parse cookies: %v", err)
	}
	return p, nil
}

func mergeCookies(cs ...string) ([]*http.Cookie, error) {
	mergedCookies := make(map[string]*http.Cookie)

	for _, cval := range cs {
		cookies, err := parseCookies(cval)
		if err != nil {
			return nil, fmt.Errorf("failed to parse cookies: %v", err)
		}

		for _, cookie := range cookies {
			mc, ok := mergedCookies[cookie.Name]
			// If the cookie already exists, check if the value is the same.
			if ok && mc.Value != cookie.Value { // unhappy path
				return nil, fmt.Errorf("conflicting cookie values for key %s", cookie.Name)
			}
			// If the cookie does not exist or the value is the same, just add/overwrite it.
			mergedCookies[cookie.Name] = cookie
		}
	}

	result := make([]*http.Cookie, 0, len(mergedCookies))
	for _, cookie := range mergedCookies {
		result = append(result, cookie)
	}

	return result, nil
}

func mergeAndSortCookies(existing []string, newCs []*http.Cookie) ([]*http.Cookie, error) {
	cs, err := mergeCookies(existing...)
	if err != nil {
		return nil, err
	}

	cookieMap := make(map[string]*http.Cookie)
	for _, c := range cs {
		if c != nil {
			cookieMap[c.Name] = c
		}
	}
	for _, c := range newCs {
		if c != nil {
			cookieMap[c.Name] = c
		}
	}

	var mergedList []*http.Cookie
	for _, c := range cookieMap {
		mergedList = append(mergedList, c)
	}

	slices.SortFunc(mergedList, func(a, b *http.Cookie) int {
		return strings.Compare(a.Name, b.Name)
	})
	return mergedList, nil
}
