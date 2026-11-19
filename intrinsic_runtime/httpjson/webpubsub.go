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

package webpubsub

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/http"

	"github.com/grpc-ecosystem/grpc-gateway/v2/runtime"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/types/known/emptypb"

	pb "intrinsic/httpjson/proto/v1alpha/webpubsub_go_proto"

	"intrinsic_runtime/httpjson/webrtc"
)

type idTokenSource interface {
	Token(ctx context.Context) (string, error)
}

var defaultTokenSource = func(ctx context.Context) (idTokenSource, error) {
	return nil, errors.New("IPC identity token source is only supported in enterprise builds")
}

type legacyConfigurationProvider struct{}

func (p legacyConfigurationProvider) FetchTURNAuthCredentials(_ context.Context) (*webrtc.TURNCredentials, error) {
	return &webrtc.TURNCredentials{
		Username: "intrinsic",
		Password: "this-password-is-leaked",
	}, nil
}

func (p legacyConfigurationProvider) PortalDomain() string {
	return "portal.intrinsic.ai"
}

type turnAuthConfigurationProvider struct {
	httpClient     *http.Client
	credentialsURL string
	tokenSource    idTokenSource
	portalDomain   string
}

func (p turnAuthConfigurationProvider) FetchTURNAuthCredentials(ctx context.Context) (*webrtc.TURNCredentials, error) {
	token, err := p.tokenSource.Token(ctx)
	if err != nil {
		return nil, fmt.Errorf("create IPC identity token: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, p.credentialsURL, nil)
	if err != nil {
		return nil, fmt.Errorf("create request: %w", err)
	}
	req.AddCookie(&http.Cookie{Name: "auth-proxy", Value: token})

	credsResp, err := p.httpClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("http request: %w", err)
	}
	defer credsResp.Body.Close()
	if credsResp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("http request failed with status code %d", credsResp.StatusCode)
	}

	var creds webrtc.TURNCredentials
	if err := json.NewDecoder(credsResp.Body).Decode(&creds); err != nil {
		return nil, fmt.Errorf("decode TURN credentials: %w", err)
	}

	if creds.Password == "" {
		return nil, errors.New("TURN credentials are empty")
	}
	return &creds, nil
}

func (p turnAuthConfigurationProvider) PortalDomain() string {
	return p.portalDomain
}

func NewConfigurationProvider(enableTURNAuthServers bool, portalDomain string) (webrtc.ConfigurationProvider, error) {
	if !enableTURNAuthServers {
		return legacyConfigurationProvider{}, nil
	}

	if portalDomain == "" {
		return nil, errors.New("portal domain must be specified when TURN auth servers are enabled")
	}

	tokenSource, err := defaultTokenSource(context.Background())
	if err != nil {
		return nil, fmt.Errorf("create token source: %w", err)
	}
	return turnAuthConfigurationProvider{
		httpClient:     &http.Client{},
		credentialsURL: fmt.Sprintf("https://%s/api/turn-credentials", portalDomain),
		tokenSource:    tokenSource,
		portalDomain:   portalDomain,
	}, nil
}

type WebPubSubServer struct {
	pb.UnimplementedWebPubSubServer
	configProvider webrtc.ConfigurationProvider
}

func NewWebPubSubServer(provider webrtc.ConfigurationProvider) *WebPubSubServer {
	return &WebPubSubServer{configProvider: provider}
}

func (s *WebPubSubServer) GenerateTurnCredentials(ctx context.Context, req *emptypb.Empty) (*pb.GenerateTurnCredentialsResponse, error) {
	creds, err := s.configProvider.FetchTURNAuthCredentials(ctx)
	if err != nil {
		return nil, fmt.Errorf("failed to retrieve TURN credentials: %w", err)
	}

	portalDomain := s.configProvider.PortalDomain()
	uris := []string{
		fmt.Sprintf("turn:turn.global.%s:443?transport=tcp", portalDomain),
		fmt.Sprintf("turn:turn.global.%s:443", portalDomain),
	}

	return &pb.GenerateTurnCredentialsResponse{
		Uris:     uris,
		Username: creds.Username,
		Password: creds.Password,
	}, nil
}

var ActiveClientsProvider func() []*pb.ConnectedClient

func (s *WebPubSubServer) ListConnectedClients(ctx context.Context, req *pb.ListConnectedClientsRequest) (*pb.ListConnectedClientsResponse, error) {
	if ActiveClientsProvider == nil {
		return &pb.ListConnectedClientsResponse{}, nil
	}

	connections := ActiveClientsProvider()

	pageSize := int(req.GetPageSize())
	if pageSize <= 0 {
		pageSize = 100
	}

	startIndex := 0
	if req.GetPageToken() != "" {
		var offset int
		if _, err := fmt.Sscanf(req.GetPageToken(), "%d", &offset); err == nil {
			startIndex = offset
		}
	}

	if startIndex < 0 {
		startIndex = 0
	}
	if startIndex >= len(connections) {
		return &pb.ListConnectedClientsResponse{}, nil
	}

	endIndex := startIndex + pageSize
	if endIndex > len(connections) {
		endIndex = len(connections)
	}

	clients := connections[startIndex:endIndex]

	var nextPageToken string
	if endIndex < len(connections) {
		nextPageToken = fmt.Sprintf("%d", endIndex)
	}

	return &pb.ListConnectedClientsResponse{
		Clients:       clients,
		NextPageToken: nextPageToken,
	}, nil
}

// StartWebPubSubGRPCServer starts a WebPubSub gRPC server in the background and returns the server instance.
func StartWebPubSubGRPCServer(grpcPort int64, configProvider webrtc.ConfigurationProvider) (*grpc.Server, error) {
	server := NewWebPubSubServer(configProvider)

	grpcServer := grpc.NewServer()
	pb.RegisterWebPubSubServer(grpcServer, server)

	lis, err := net.Listen("tcp", fmt.Sprintf(":%d", grpcPort))
	if err != nil {
		return nil, fmt.Errorf("failed to listen on gRPC port %d: %w", grpcPort, err)
	}

	go func() {
		slog.Info("Starting gRPC server", "port", grpcPort)
		if err := grpcServer.Serve(lis); err != nil {
			slog.Error("gRPC server failed", "error", err)
		}
	}()

	return grpcServer, nil
}

// RegisterWebPubSubHTTP registers the WebPubSub REST/gateway proxy to the mux by dialling the gRPC service address.
func RegisterWebPubSubHTTP(ctx context.Context, mux *runtime.ServeMux, serviceAddress string, opts []grpc.DialOption) error {
	return pb.RegisterWebPubSubHandlerFromEndpoint(ctx, mux, serviceAddress, opts)
}
