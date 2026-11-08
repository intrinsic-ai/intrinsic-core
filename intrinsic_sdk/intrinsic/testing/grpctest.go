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

// Package grpctest provides a test server for unit tests that use gRPC.
package grpctest

import (
	"fmt"
	"net"
	"testing"

	"github.com/bazelbuild/remote-apis-sdks/go/pkg/portpicker"
)

// Server is an interface used by StartServer to allow for grpcprod or grpc
// servers to be used.
type Server interface {
	Serve(lis net.Listener) error
	Stop()
}

// StartServerT instantiates a gRPC server suitable for unit tests, and returns
// the server address the client can use to connect. The server will automatically
// be shut down at the end of the test.
//
// Example:
//
//	srv, err := grpcprod.NewServer(nil)
//	if err != nil {
//	     t.Fatalf("Error creating test server: %v", err)
//	}
//	s := &myServer{}
//	pb.RegisterMyServer(srv.GRPC, s)
//	addr := grpctest.StartServerT(t, srv)
//
// Prefer this over StartServer where possible.
func StartServerT(tb testing.TB, server Server) string {
	tb.Helper()
	port, err := portpicker.PickUnusedPort()
	if err != nil {
		tb.Fatalf("Picking unused port: %v", err)
	}
	// Best effort to reuse the port.
	tb.Cleanup(func() { portpicker.RecycleUnusedPort(port) })

	addr := fmt.Sprintf(":%d", port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		tb.Fatalf("Creating TCP listener: %v", err)
	}
	// server.Stop() should close this, but do it anyway to avoid resource leak.
	tb.Cleanup(func() { lis.Close() })

	go server.Serve(lis)
	tb.Cleanup(server.Stop)
	// Listen on INADDR_ANY, but return localhost address to make
	// sure both IPv4 and IPv6 listeners are possible.
	// Using ip6-localhost breaks IPv4 tests.
	return fmt.Sprintf("localhost:%d", port)
}
