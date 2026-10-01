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

// Package httphandlers contains HTTP handlers for the http-gateway service
package httphandlers

import (
	"context"
	"intrinsic_runtime/httpjson/arguments"
	"intrinsic_runtime/httpjson/livelinesshandlers"
	"intrinsic_runtime/httpjson/openapihandler"
	"intrinsic_runtime/httpjson/webpubsub"

	acsggp "intrinsic/assets/proto/v1/asset_configuration_go_proto"
	bbggp "intrinsic/executive/proto/blackboard_service_go_proto"
	esggp "intrinsic/executive/proto/executive_service_go_proto"
	pbggp "intrinsic/executive/proto/proto_builder_go_proto"
	ssggp "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	gsggp "intrinsic/geometry/proto/geometry_service_go_proto"
	invggp "intrinsic/kubernetes/inversion/v1/inversion_go_proto"
	lsggp "intrinsic/logging/proto/logger_service_go_proto"
	rsggp "intrinsic/logging/proto/replay_service_go_proto"
	tsggpp "intrinsic/perception/proto/v1/train_service_go_proto"

	cdsggp "intrinsic/kubernetes/intrinsic_os/connectivity_diagnostics/proto/v1/connectivity_diagnostics_service_go_proto"
	csggp "intrinsic/kubernetes/intrinsic_os/protos/v1/cluster_service_go_proto"

	webpubsubpb "intrinsic/httpjson/proto/v1alpha/webpubsub_go_proto"

	"github.com/intrinsic-ai/insrc/intrinsic_runtime/httpjson/webrtc"

	"log/slog"
	"math"

	prggp "intrinsic/proto_tools/proto/proto_registry_go_proto"
	soiggp "intrinsic/scene/proto/v1/scene_object_import_go_proto"
	owsggp "intrinsic/world/public/proto/object_world_service_go_proto"

	"github.com/grpc-ecosystem/grpc-gateway/v2/runtime"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

func RegisterHandlers(
	ctx context.Context,
	mux *runtime.ServeMux,
	cliArgs *arguments.CLIArguments,
	pubSubConnManager *webrtc.ConnectionManager,
) error {
	slog.Info("Registering HTTP handlers")

	// Register handlers that work even when no other gRPC services are ready
	livelinesshandlers.RegisterLivelinessHandlers(mux)
	openapihandler.RegisterOpenAPIHandler(mux)
	if cliArgs.EnableHttpGatewayPubsub {
		webpubsub.ActiveClientsProvider = func() []*webpubsubpb.ConnectedClient {
			return pubSubConnManager.ListClientStats()
		}
	}

	var err error
	insecureOpts := []grpc.DialOption{
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(math.MaxInt)),
	}

	// Register handlers for the solution service
	err = ssggp.RegisterSolutionServiceHandlerFromEndpoint(ctx, mux, cliArgs.SolutionServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Executive service
	err = esggp.RegisterExecutiveServiceHandlerFromEndpoint(ctx, mux, cliArgs.ExecutiveServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Proto Builder
	err = pbggp.RegisterProtoBuilderHandlerFromEndpoint(ctx, mux, cliArgs.ProtoBuilderAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Proto Registry
	err = prggp.RegisterProtoRegistryHandlerFromEndpoint(ctx, mux, cliArgs.ProtoRegistryAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Blackboard service
	err = bbggp.RegisterExecutiveBlackboardHandlerFromEndpoint(ctx, mux, cliArgs.BlackboardServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Asset configuration service
	err = acsggp.RegisterAssetConfigurationServiceHandlerFromEndpoint(ctx, mux, cliArgs.AssetConfigurationServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the Geometry service
	err = gsggp.RegisterGeometryServiceHandlerFromEndpoint(ctx, mux, cliArgs.GeometryServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the SceneObject import service
	err = soiggp.RegisterSceneObjectImportHandlerFromEndpoint(ctx, mux, cliArgs.SceneObjectImportServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	err = owsggp.RegisterObjectWorldServiceHandlerFromEndpoint(ctx, mux, cliArgs.ObjectWorldServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	err = invggp.RegisterIpcUpdaterHandlerFromEndpoint(ctx, mux, cliArgs.IPCUpdaterServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	err = csggp.RegisterClusterServiceHandlerFromEndpoint(ctx, mux, cliArgs.ClusterServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the train service
	err = tsggpp.RegisterTrainServiceHandlerFromEndpoint(ctx, mux, cliArgs.TrainServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the logger service
	err = lsggp.RegisterDataLoggerHandlerFromEndpoint(ctx, mux, cliArgs.LoggerServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the replay service
	err = rsggp.RegisterReplayHandlerFromEndpoint(ctx, mux, cliArgs.ReplayServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	// Register handlers for the connectivity diagnostics service
	err = cdsggp.RegisterIpcConnectivityDiagnosticsServiceHandlerFromEndpoint(ctx, mux, cliArgs.ConnectivityDiagnosticsServiceAddress, insecureOpts)
	if err != nil {
		return err
	}

	if cliArgs.EnableHttpGatewayPubsub {
		if err := webpubsub.RegisterWebPubSubHTTP(ctx, mux, cliArgs.WebPubSubServiceAddress, insecureOpts); err != nil {
			return err
		}
		mux.HandlePath("GET", "/webpubsub/v1alpha/control-connection", pubSubConnManager.HandleControlConnection)
	}

	return nil
}
