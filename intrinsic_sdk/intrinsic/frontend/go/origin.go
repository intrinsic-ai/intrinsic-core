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

// Package origin provides utilities to report where requests came from.
// See https://en.wikipedia.org/wiki/List_of_HTTP_header_fields#Common_non-standard_request_fields
// for many of the used headers.
package origin

import (
	"net/http"

	"github.com/golang/glog"
)

func fromHostAndURI(r *http.Request) string {
	var res string
	if h := r.Header.Get("X-Forwarded-Host"); h != "" {
		res = "https://" + h
	}
	if u := r.Header.Get("X-Original-Uri"); u != "" {
		res += u
	}
	if res != "" {
		return res
	}
	return "<unknown>"
}

// Description returns a human readable request origin suitable for logging.
func Description(r *http.Request) string {
	if originURL := r.Header.Get("X-Original-Url"); originURL != "" {
		return "original-url=" + originURL
	}
	if originPath := r.Header.Get("X-Envoy-Original-Path"); originPath != "" {
		return "envoy-original-path=" + originPath
	}
	// TODO(ensonic): figure why we get these inconsistently
	return "forwarded-host/original-uri=" + fromHostAndURI(r)
}

// ExtractOriginalURL returns the original URL or path from proxy headers.
// TODO (b/492165421) X-Original-Url is nginx ingress specific, X-Envoy-Original-Path is used on istio gateway
// BEWARE: Therefore this method results different types of URL/Paths
// SOLUTION: url.Parse(u).Path strips any scheme/host in X-Original-Url, normalizing it to match X-Envoy-Original-Path.
func ExtractOriginalURL(r *http.Request) string {
	u := r.Header.Get("X-Original-Url")
	p := r.Header.Get("X-Envoy-Original-Path")
	if u != "" && p != "" {
		glog.Warningf("Ambiguous request headers: both X-Original-Url (%q) and X-Envoy-Original-Path (%q) are set", u, p)
		return ""
	}
	if u != "" {
		return u
	}
	return p
}

// URL returns the address of the request origin.
func URL(r *http.Request) string {
	if u := ExtractOriginalURL(r); u != "" {
		return u
	}
	// TODO(ensonic): figure why we get these inconsistently
	return fromHostAndURI(r)
}
