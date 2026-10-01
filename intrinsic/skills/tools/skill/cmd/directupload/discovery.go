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

// Package directupload implements direct upload transferer for inctl tools
// and discovery mechanism for finding the best possible upload path to the target
package directupload

import (
	"context"

	"intrinsic/storage/artifacts/client/client"

	"google.golang.org/grpc"

	articatgrpcpb "intrinsic/storage/artifacts/proto/v1/articat_go_proto"
	artifactgrpcpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

// TargetDiscovery interface represent an implementation of the discovery
// mechanism to find the most efficient path between client and target workcell
// to maximize speed of the upload.
type TargetDiscovery interface {
	// GetClient gets ArtifactServiceApiClient with the best possible path
	GetClient(ctx context.Context) (artifactgrpcpb.ArtifactServiceApiClient, error)
}

// NewFromConnection creates new TargetDiscovery implementation using provided
// grpc.ClientConnInterface to create a client.
func NewFromConnection(conn grpc.ClientConnInterface) TargetDiscovery {
	return &staticConnection{conn: conn}
}

type staticConnection struct {
	conn grpc.ClientConnInterface
}

func (s *staticConnection) GetClient(_ context.Context) (artifactgrpcpb.ArtifactServiceApiClient, error) {
	return artifactgrpcpb.NewArtifactServiceApiClient(s.conn), nil
}

// NewCatalogTarget creates new TargetDiscovery implementation which connects
// to Artifact Service for Catalogs in Cloud. ArtiCat uses different service
// name then regular Artifacts service in order to properly reach cloud based
// service.
func NewCatalogTarget(conn grpc.ClientConnInterface) TargetDiscovery {
	return &catalogConnection{conn: conn}
}

type catalogConnection struct {
	conn grpc.ClientConnInterface
}

func (c *catalogConnection) GetClient(_ context.Context) (artifactgrpcpb.ArtifactServiceApiClient, error) {
	serviceName := articatgrpcpb.ArtifactCatalogService_ServiceDesc.ServiceName
	return artifactgrpcpb.NewArtifactServiceApiClient(client.NewRenamedConnection(c.conn, serviceName)), nil
}
