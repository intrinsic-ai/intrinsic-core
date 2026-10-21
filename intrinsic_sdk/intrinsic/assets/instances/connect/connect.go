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

// Package connect provides utility functions for connecting to gRPC services defined by GrpcConnection messages.
package connect

import (
	"context"
	"errors"
	"fmt"
	"strings"

	log "github.com/golang/glog"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"

	gcpb "intrinsic/assets/proto/v1/grpc_connection_go_proto"
)

// errNilConnection is returned when the provided GrpcConnection is nil.
var errNilConnection = errors.New("connection is nil")

// Connect creates a gRPC client connection for the specified GrpcConnection and
// returns a new context with attached metadata.
func Connect(ctx context.Context, connection *gcpb.GrpcConnection) (*grpc.ClientConn, context.Context, error) {
	if connection == nil {
		return nil, nil, errNilConnection
	}

	var metadataStrs []string
	for _, m := range connection.GetMetadata() {
		metadataStrs = append(metadataStrs, fmt.Sprintf("%s=%s", m.GetKey(), m.GetValue()))
	}
	log.Infof("Connecting to address %q with headers added to context: [%s]",
		connection.GetAddress(), strings.Join(metadataStrs, ", "))

	conn, err := grpc.NewClient(connection.GetAddress(), grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create gRPC client for address %q: %w", connection.GetAddress(), err)
	}

	// Add any needed metadata to the context.
	for _, m := range connection.GetMetadata() {
		ctx = metadata.AppendToOutgoingContext(ctx, m.GetKey(), m.GetValue())
	}

	return conn, ctx, nil
}
