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

// Package main contains the http-gateway entrypoint
package main

import (
	"context"
	"fmt"
	"intrinsic_runtime/httpjson/arguments"
	"intrinsic_runtime/httpjson/httphandlers"
	"intrinsic_runtime/httpjson/webpubsub"
	"log/slog"
	"net/http"
	"os"

	"intrinsic/httpjson/any"
	"intrinsic/httpjson/cachecontrol"
	"intrinsic/httpjson/serialization"
	"intrinsic/kubernetes/intrinsic"
	"intrinsic/platform/pubsub/golang/pubsub"
	"intrinsic/stats/go/telemetry"

	"github.com/intrinsic-ai/insrc/intrinsic_runtime/httpjson/webrtc"

	"github.com/grpc-ecosystem/grpc-gateway/v2/runtime"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/protobuf/encoding/protojson"
)

func run(ctx context.Context, cliArgs *arguments.CLIArguments) error {
	slog.Info("Launching http-gateway listening to HTTP requests", "port", cliArgs.HttpPort)

	// Enable gRPC-gateway to resolve Any messages
	anyRes, err := any.NewAnyResolver(cliArgs.ProtoRegistryAddress, cliArgs.InstalledAssetsAddress)
	if err != nil {
		return err
	}
	defer anyRes.Close()

	var resolver any.Resolver = anyRes

	if cliArgs.EnableHttpGatewayPubsub {
		fdsPaths := []string{
			"intrinsic-core/intrinsic/tools/introspect/gzmsgs_descriptor_set_transitive_set_sci.proto.bin",
			"intrinsic-core/intrinsic/tools/introspect/intrinsic_descriptor_set_transitive_set_sci.proto.bin",
			"intrinsic-core/intrinsic/tools/introspect/ros_descriptor_set_transitive_set_sci.proto.bin",
		}
		fdsRes, err := any.NewFileDescriptorSetResolver(fdsPaths)
		if err != nil {
			return fmt.Errorf("failed to create FileDescriptorSetResolver: %w", err)
		}
		resolver = any.NewGreedyResolver([]any.Resolver{
			fdsRes,
			anyRes,
		})
	}

	jsonProtoAnyMarshaller := runtime.JSONPb{
		MarshalOptions: protojson.MarshalOptions{
			Resolver: resolver,
		},
		UnmarshalOptions: protojson.UnmarshalOptions{
			Resolver: resolver,
		},
	}

	mux := runtime.NewServeMux(serialization.NewGatewayMarshalerOptions(resolver)...)

	var pubSubConnManager *webrtc.ConnectionManager
	if cliArgs.EnableHttpGatewayPubsub {
		configProvider, err := webpubsub.NewConfigurationProvider(cliArgs.EnableTURNAuthServers, cliArgs.CloudProjectID)
		if err != nil {
			return err
		}

		if cliArgs.WebPubSubServiceAddress == "" {
			grpcServer, err := webpubsub.StartWebPubSubGRPCServer(cliArgs.GRPCPort, configProvider)
			if err != nil {
				return err
			}
			defer grpcServer.GracefulStop()
			cliArgs.WebPubSubServiceAddress = fmt.Sprintf("localhost:%d", cliArgs.GRPCPort)
		}

		pubSub, err := pubsub.NewPubSub()
		if err != nil {
			return fmt.Errorf("failed to initialize Intrinsic PubSub: %w", err)
		}
		defer pubSub.Close()

		pubSubConnManager, err = webrtc.NewConnectionManager(ctx, configProvider, 32123, pubSub, &jsonProtoAnyMarshaller)
		if err != nil {
			return fmt.Errorf("failed to create ConnectionManager: %w", err)
		}
		defer func() {
			if err := pubSubConnManager.Close(); err != nil {
				slog.ErrorContext(ctx, "Failed to close ConnectionManager", "error", err)
			}
		}()
	}

	err = httphandlers.RegisterHandlers(ctx, mux, cliArgs, pubSubConnManager)
	if err != nil {
		return err
	}

	return http.ListenAndServe(
		fmt.Sprintf(":%d", cliArgs.HttpPort),
		// Block cross-origin requests.
		// CrossOriginProtection allows all "safe" methods like GET, HEAD, and OPTIONS
		// So reviews of APIs added to the http-gateway must make sure these methods don't change state
		cachecontrol.NoCacheHeaders(http.NewCrossOriginProtection().Handler(mux)),
	)
}

func main() {
	cliArgs := arguments.Get()
	intrinsic.Init()

	// Set up and serve telemetry.
	telemetry.Initialize(
		telemetry.WithTracing(cliArgs.OpencensusTracing), // on-prem!
		telemetry.WithProbability(cliArgs.TraceProbability),
		telemetry.EnableMetrics(cliArgs.PrometheusPort),
		telemetry.WithViews(append(ocgrpc.DefaultClientViews, ocgrpc.DefaultServerViews...)),
	)

	ctx := context.Background()
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	if err := run(ctx, cliArgs); err != nil {
		slog.ErrorContext(ctx, "Exiting: %v", err)
		os.Exit(1)
	}
}
