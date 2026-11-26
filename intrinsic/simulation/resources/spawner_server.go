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

// spawner_server.go is a proxy for connecting a world object to the spawner
// service running in simulation. It is a no-op during real-world execution.
package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"time"

	"intrinsic/kubernetes/intrinsic"
	simspawnerserver "intrinsic/simulation/resources/simspawnerserver"
	"intrinsic/util/proto/protoio"

	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/backoff"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/reflection"
	"google.golang.org/protobuf/encoding/prototext"

	grpcsstatepb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	rcpb "intrinsic/resources/proto/runtime_context_go_proto"
	simspawnergrpcpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	simspawnerpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	spawnerconfigpb "intrinsic/simulation/resources/spawner_config_go_proto"
	spawnergrpcpb "intrinsic/simulation/resources/spawner_server_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

var (
	flagPort              = flag.Int("port", 8080, "")
	flagRuntimeConfigFile = flag.String("runtime_config", "/etc/intrinsic/runtime_config.pb", "The path to the resource instance proto file")
	flagSimRetryInterval  = flag.Duration("retry_interval", 3*time.Second, "The maximum duration we will wait before trying to reconnect to the simulator when dialing gRPC")
	flagSimRetryDuration  = flag.Duration("retry_duration", 2*time.Minute, "The maximum duration we will spend retrying simulation RPCs")
)

type noopSimSpawnerClient struct{}

func (*noopSimSpawnerClient) SpawnObject(context.Context, *simspawnerpb.SpawnObjectRequest, ...grpc.CallOption) (*emptypb.Empty, error) {
	// do nothing
	return &emptypb.Empty{}, nil
}

func main() {
	intrinsic.Init()

	rc := &rcpb.RuntimeContext{}
	if err := protoio.ReadBinaryProto(*flagRuntimeConfigFile, rc); err != nil {
		log.Exitf("Failed to read runtime context from %q: %v", *flagRuntimeConfigFile, err)
	}

	address := fmt.Sprintf(":%d", rc.GetPort())
	lis, err := net.Listen("tcp", address)
	if err != nil {
		log.Exitf("Server failed to listen at %q: %v", address, err)
	}
	log.Infof("Server is now listening at %q", address)

	s := grpc.NewServer()

	simClient := func() simspawnergrpcpb.SpawnerServiceClient {
		if rc.GetLevel() != rcpb.RuntimeContext_PHYSICS_SIM {
			log.Infof("Using noop sim spawner service.")
			return &noopSimSpawnerClient{}
		}

		simAddr := rc.GetSimulationServerAddress()
		simAddr += ":12395"

		log.Infof("Connecting to simulator: %s", simAddr)
		connectParams := backoff.DefaultConfig
		connectParams.MaxDelay = *flagSimRetryInterval

		if conn, err := grpc.Dial(simAddr,
			grpc.WithTransportCredentials(insecure.NewCredentials()),
			grpc.WithDefaultCallOptions(),
			grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
			grpc.WithConnectParams(
				grpc.ConnectParams{
					Backoff:           connectParams,
					MinConnectTimeout: 1 * time.Second,
				},
			),
		); err == nil {
			return simspawnergrpcpb.NewSpawnerServiceClient(conn)
		} else {
			log.Exitf("grpc.Dial(%q) failed: %v", simAddr, err)
		}
		return nil
	}()

	spawnConfig := new(spawnerconfigpb.SpawnerConfiguration)
	rcConfig := rc.GetConfig()
	if rcConfig != nil {
		if err := rcConfig.UnmarshalTo(spawnConfig); err != nil {
			log.Exitf("error unmarshalling spawn resource instance config %s: %v", prototext.Format(rcConfig), err)
		}
	}

	if ss, err := simspawnerserver.Create(simClient, spawnConfig, *flagSimRetryInterval, *flagSimRetryDuration); err == nil {
		spawnergrpcpb.RegisterSpawnerServer(s, ss)
		grpcsstatepb.RegisterServiceStateServer(s, ss)
	} else {
		log.Exitf("Failed to create simulation spawner: %v", err)
	}

	reflection.Register(s)
	s.Serve(lis)
}
