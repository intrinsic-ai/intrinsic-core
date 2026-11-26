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

// outfeed_server.go is a proxy for connecting a world object to the outfeed
// service running in simulation. It is a no-op during real-world execution.
package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"time"

	"intrinsic/kubernetes/intrinsic"
	simoutfeedserver "intrinsic/simulation/resources/simoutfeedserver"
	"intrinsic/util/proto/protoio"

	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/backoff"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/reflection"

	sstategrpcpb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	rcpb "intrinsic/resources/proto/runtime_context_go_proto"
	simoutfeedgrpcpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	simoutfeedpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	outfeedgrpcpb "intrinsic/simulation/resources/outfeed_server_go_proto"
)

var (
	flagPort              = flag.Int("port", 8080, "")
	flagRuntimeConfigFile = flag.String("runtime_config", "/etc/intrinsic/runtime_config.pb", "The path to the resource instance proto file")
	flagSimRetryInterval  = flag.Duration("retry_interval", 3*time.Second, "The maximum duration we will wait before trying to reconnect to the simulator when dialing gRPC")
	flagSimRetryDuration  = flag.Duration("retry_duration", 2*time.Minute, "The maximum duration we will spend retrying simulation RPCs")
)

type noopSimOutfeedClient struct{}

func (*noopSimOutfeedClient) ProcessOutfeed(context.Context, *simoutfeedpb.ProcessOutfeedRequest, ...grpc.CallOption) (*simoutfeedpb.ProcessOutfeedResponse, error) {
	// do nothing
	return &simoutfeedpb.ProcessOutfeedResponse{}, nil
}

func main() {
	intrinsic.Init()

	rc := &rcpb.RuntimeContext{}
	if err := protoio.ReadBinaryProto(*flagRuntimeConfigFile, rc); err != nil {
		log.Exitf("Failed to read runtime context from %q: %v", *flagRuntimeConfigFile, err)
	}

	var simClient simoutfeedgrpcpb.OutfeedServiceClient
	simClient = &noopSimOutfeedClient{}
	if rc.GetLevel() == rcpb.RuntimeContext_PHYSICS_SIM {
		simAddr := rc.GetSimulationServerAddress()
		simAddr += ":12396"

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
			simClient = simoutfeedgrpcpb.NewOutfeedServiceClient(conn)
		} else {
			log.Exitf("grpc.Dial(%q) failed: %v", simAddr, err)
		}
	}

	ss, err := simoutfeedserver.Create(simClient, *flagSimRetryInterval, *flagSimRetryDuration)
	if err != nil {
		log.Exitf("Failed to create simulation outfeed: %v", err)
	}

	address := fmt.Sprintf(":%d", rc.GetPort())
	lis, err := net.Listen("tcp", address)
	if err != nil {
		log.Exitf("Server failed to listen at %q: %v", address, err)
	}
	log.Infof("Server is now listening at %q", address)

	s := grpc.NewServer()
	outfeedgrpcpb.RegisterOutfeedServer(s, ss)
	sstategrpcpb.RegisterServiceStateServer(s, ss)
	reflection.Register(s)
	s.Serve(lis)
}
