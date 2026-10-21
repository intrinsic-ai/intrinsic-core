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

// Package serialization provides centralized message format detection and gRPC-gateway marshaling configuration.
package serialization

import (
	"mime"
	"net/http"
	"strings"

	"intrinsic/httpjson/any"

	"github.com/grpc-ecosystem/grpc-gateway/v2/runtime"
	"google.golang.org/protobuf/encoding/protojson"
)

// Format defines the serialization encoding
type Format int

const (
	// FormatJSON represents JSON encoding.
	FormatJSON Format = iota
	// FormatProto represents binary protocol buffer encoding.
	FormatProto
)

// RequestFormat inspects the HTTP Content-Type header to determine the request body encoding.
func RequestFormat(r *http.Request) Format {
	format, _ := matchSupportedMIME(r.Header.Values("Content-Type")...)
	return format
}

// ResponseFormat inspects the HTTP Accept header to determine the desired response body encoding.
// If Accept is empty or contains only unsupported types/wildcards (*/*), it falls back to the request Content-Type.
func ResponseFormat(r *http.Request) Format {
	if format, ok := matchSupportedMIME(r.Header.Values("Accept")...); ok {
		return format
	}
	return RequestFormat(r)
}

type mimeSpec struct {
	mimeType string
	format   Format
}

// supportedMIMEs maps MIME types to serialization Formats.
// Binary proto first because it's probably more efficient.
var supportedMIMEs = []mimeSpec{
	// Binary Protobuf variants
	{"application/protobuf", FormatProto},
	{"application/x-protobuf", FormatProto},
	{"application/octet-stream", FormatProto},

	// JSON variants
	{"application/json", FormatJSON},
	{"application/protobuf+json", FormatJSON},
	{"application/x-protobuf+json", FormatJSON},
}

// matchSupportedMIME parses comma-separated media types from one or more header strings,
// matching them against supportedMIMEs in server preference order.
// This implements proactive content negotiation per RFC 9110 Section 12.5.1.
func matchSupportedMIME(headerVals ...string) (Format, bool) {
	for _, spec := range supportedMIMEs {
		for _, val := range headerVals {
			for _, part := range strings.Split(val, ",") {
				trimmed := strings.TrimSpace(part)
				mediaType, _, err := mime.ParseMediaType(trimmed)
				if err != nil {
					mediaType = strings.ToLower(trimmed)
				}
				if mediaType == spec.mimeType {
					return spec.format, true
				}
			}
		}
	}
	return FormatJSON, false
}

// protoMarshalerWithContentType wraps runtime.ProtoMarshaller to return the exact MIME type in the response
// Content-Type header instead of generic application/octet-stream.
type protoMarshalerWithContentType struct {
	runtime.ProtoMarshaller
	contentType string
}

// ContentType overrides runtime.ProtoMarshaller's default application/octet-stream return value.
func (p *protoMarshalerWithContentType) ContentType(_ interface{}) string {
	return p.contentType
}

// NewGatewayMarshalerOptions builds the array of runtime.ServeMuxOption needed to configure gRPC-gateway
// with support for both JSON and binary Protocol Buffer serialization based on supportedMIMEs.
func NewGatewayMarshalerOptions(resolver any.Resolver) []runtime.ServeMuxOption {
	jsonMarshaler := &runtime.JSONPb{
		MarshalOptions: protojson.MarshalOptions{
			Resolver: resolver,
		},
		UnmarshalOptions: protojson.UnmarshalOptions{
			Resolver: resolver,
		},
	}

	opts := make([]runtime.ServeMuxOption, 0, len(supportedMIMEs)+1)
	for _, spec := range supportedMIMEs {
		var m runtime.Marshaler
		if spec.format == FormatProto {
			m = &protoMarshalerWithContentType{contentType: spec.mimeType}
		} else {
			m = jsonMarshaler
		}
		opts = append(opts, runtime.WithMarshalerOption(spec.mimeType, m))
	}
	opts = append(opts, runtime.WithMarshalerOption(runtime.MIMEWildcard, jsonMarshaler))
	return opts
}
