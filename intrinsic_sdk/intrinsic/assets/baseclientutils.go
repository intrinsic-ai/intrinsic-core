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

// Package baseclientutils are helper functions for supporting catalog clients
// and authentication, excluding those that need google-internal support.
package baseclientutils

import (
	"crypto/x509"
	"math"
	"net/url"
	"regexp"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
)

const (
	maxMsgSize = math.MaxInt64
	// Policies for retrying failed gRPC requests (as documented in
	// https://pkg.go.dev/google.golang.org/grpc/examples/features/retry):
	//
	// 1. Wildcard (default): Retries on UNAVAILABLE, RESOURCE_EXHAUSTED, UNIMPLEMENTED.
	//    Note that the Ingress will return UNIMPLEMENTED if the server it wants to forward to is
	//    unavailable, so we also check for UNIMPLEMENTED.
	//
	// 2. AssetArtifacts: Custom policy for heavy geometry/artifact uploads.
	//    Uses a patient retry window (6 attempts, max 10s backoff, total ~25s) to survive server
	//    restarts (which take 10-15s) and Nginx rate-limiting blocks under parallel release load.
	//
	//    We EXCLUDE 'UNIMPLEMENTED' from the retry list here to ensure the client-side
	//    'probeAssetArtifacts' call fails fast, rather than hanging for 25s.
	//
	//    We INCLUDE 'UNKNOWN' to retry on transient HTTP 500 errors from the api-relay/auth-proxy
	//    (which the gRPC client translates to UNKNOWN). This is safe because the AssetArtifacts
	//    APIs are idempotent.
	//
	//    Note on Session vs RPC retries: This gRPC retry policy handles transport-level transience on
	//    individual RPCs. If a server pod restarts mid-upload, its in-memory upload session map is
	//    wiped, causing subsequent in-session calls to return NOT_FOUND. That session loss cannot be
	//    resolved by retrying the chunk RPC and is instead recovered by retrying sessions in
	//    referenceddata.go.
	//
	// 3. AssetCatalog / AssetCatalogInternal: Custom policy for catalog interaction.
	//    Retries on database transaction conflicts ('ABORTED') which are transient, but keeps the
	//    retry window short (max 2s backoff) to fail-fast on permanent errors.
	//
	// 4. ContentAddressableStorageService: Copies default but adds retries on UNKNOWN; see b/292473318.
	//
	// 5. ArtifactServiceApi: Custom policy; see TODO(b/512180682).
	retryPolicy = `{
		"methodConfig": [{
				"name": [{}],
				"waitForReady": true,
				"retryPolicy": {
						"MaxAttempts": 4,
						"InitialBackoff": ".5s",
						"MaxBackoff": ".5s",
						"BackoffMultiplier": 1.5,
						"RetryableStatusCodes": [ "UNAVAILABLE", "RESOURCE_EXHAUSTED", "UNIMPLEMENTED"]
				}
		}, {
				"name": [{"service": "intrinsic_proto.assets.v1.AssetArtifacts"}],
				"waitForReady": true,
				"retryPolicy": {
						"MaxAttempts": 6,
						"InitialBackoff": "1s",
						"MaxBackoff": "10s",
						"BackoffMultiplier": 2.0,
						"RetryableStatusCodes": [ "UNAVAILABLE", "RESOURCE_EXHAUSTED", "UNKNOWN" ]
				}
		}, {
				"name": [
					{"service": "intrinsic_proto.catalog.v1.AssetCatalog"},
					{"service": "intrinsic_proto.catalog.v1.AssetCatalogInternal"}
				],
				"waitForReady": true,
				"retryPolicy": {
						"MaxAttempts": 5,
						"InitialBackoff": ".5s",
						"MaxBackoff": "2s",
						"BackoffMultiplier": 1.5,
						"RetryableStatusCodes": [ "UNAVAILABLE", "RESOURCE_EXHAUSTED", "ABORTED"]
				}
		}, {
				"name": [{"service": "intrinsic_proto.content_addressable_storage.v1.ContentAddressableStorageService"}],
				"waitForReady": true,
				"retryPolicy": {
						"MaxAttempts": 4,
						"InitialBackoff": ".5s",
						"MaxBackoff": ".5s",
						"BackoffMultiplier": 1.5,
						"RetryableStatusCodes": [ "UNAVAILABLE", "RESOURCE_EXHAUSTED", "UNIMPLEMENTED", "UNKNOWN"]
				}
		}, {
				"name": [{"service": "intrinsic_proto.storage.artifacts.v1.ArtifactServiceApi"}],
				"waitForReady": true,
				"retryPolicy": {
						"MaxAttempts": 4,
						"InitialBackoff": ".5s",
						"MaxBackoff": ".5s",
						"BackoffMultiplier": 1.5,
						"RetryableStatusCodes": [ "UNAVAILABLE", "RESOURCE_EXHAUSTED", "UNIMPLEMENTED", "ABORTED"]
				}
		}]
}`
)

// schemePattern matches a URL scheme according to https://github.com/grpc/grpc/blob/master/doc/naming.md.
var schemePattern = regexp.MustCompile("^(?:dns|unix|unix-abstract|vsock|ipv4|ipv6):")

// BaseDialOptions are the base dial options for catalog clients.
func BaseDialOptions() []grpc.DialOption {
	return []grpc.DialOption{
		grpc.WithDefaultServiceConfig(retryPolicy),
		grpc.WithDefaultCallOptions(
			grpc.MaxCallRecvMsgSize(maxMsgSize),
			grpc.MaxCallSendMsgSize(maxMsgSize),
		),
	}
}

// GetTransportCredentialsDialOption returns transport credentials from the system certificate pool.
func GetTransportCredentialsDialOption() (grpc.DialOption, error) {
	pool, err := x509.SystemCertPool()
	if err != nil {
		return nil, errors.Wrap(err, "failed to retrieve system cert pool")
	}

	return grpc.WithTransportCredentials(credentials.NewClientTLSFromCert(pool, "")), nil
}

// IsLocalAddress returns true if the address is a local address.
func IsLocalAddress(address string) bool {
	for _, localAddress := range []string{"127.0.0.1", "local", "xfa.lan"} {
		if strings.Contains(address, localAddress) {
			return true
		}
	}
	return false
}

// UseInsecureCredentials determines whether insecure credentials can/should be used for the given
// address.
//
// The dialer uses this internally to decide which credentials to provide.
// If the input address is invalid, we default to not using insecure credentials.
func UseInsecureCredentials(address string) bool {
	// Matching a URL without a scheme is invalid. Default to the dns://. This is the same default
	// Golang uses to dial targets.
	if !schemePattern.MatchString(address) {
		address = "dns://" + address
	}
	port := 443
	if parsed, err := url.Parse(address); err == nil { // if NO error
		if parsedPort, err := strconv.Atoi(parsed.Port()); err == nil { // if NO error
			port = parsedPort
		}
	}
	return port != 443
}

// NewCatalogClient creates a gRPC connection with the proper transport
// credentials to talk to catalogs.
//
// When calling on the connection, user credentials must be supplied separately
// via the context (e.g., by copying the incoming context metadata from the
// source call to the outgoing context metadata for the client call).  This
// does not attempt to load them from the relevant API key.  This is intended
// to be used within services that will rely solely on auth that has been
// propagated from another client, service, or frontend.
func NewCatalogClient(addr string) (*grpc.ClientConn, error) {
	opts := []grpc.DialOption{
		grpc.WithDefaultServiceConfig(`{
			"methodConfig": [{
				"name": [{}],
				"retryPolicy": {
						"MaxAttempts": 4,
						"InitialBackoff": ".5s",
						"MaxBackoff": ".5s",
						"BackoffMultiplier": 1.5,
						"RetryableStatusCodes": ["UNAVAILABLE", "RESOURCE_EXHAUSTED"]
				}
			}]
}`),
		grpc.WithDefaultCallOptions(
			// Remove the recv limit as we trust that our catalogs will send a
			// reasonable amount of information for a given request.
			grpc.MaxCallRecvMsgSize(maxMsgSize),
			// We don't typically need to send large amounts of data to the
			// catalogs from on-prem, so leave that limit at its default.
		),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	}
	if IsLocalAddress(addr) {
		opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials()))
	} else {
		opt, err := GetTransportCredentialsDialOption()
		if err != nil {
			return nil, errors.Wrap(err, "failed to get transport credentials")
		}
		opts = append(opts, opt)
	}

	return grpc.NewClient(addr, opts...)
}
