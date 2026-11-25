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

// The code_execution_service serves the CodeExecutionService and
// CodeExecutionInfoService, which handle code execution tasks for the executive
// and provide information related to code execution for user interfaces such
// as the frontend.
package main

import (
	"context"
	"flag"
	"fmt"
	"math"
	"net"
	"os/signal"
	"syscall"

	"intrinsic/executive/code/codeexecutionservice"
	"intrinsic/kubernetes/intrinsic"

	log "github.com/golang/glog"
	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

	codeexecutioninfoservicegrpcpb "intrinsic/executive/proto/code_execution_info_service_go_proto"
	codeexecutionservicegrpcpb "intrinsic/executive/proto/code_execution_service_go_proto"
)

const (
	maxMsgSize = math.MaxInt64
)

var (
	port              = flag.Int("port", 12345, "Port to serve gRPC on.")
	jupyterServerHost = flag.String(
		"jupyter_server_host",
		"",
		"Hostname and port of the Jupyter server.",
	)
	worldServiceAddress    = flag.String("world_service_address", "", "Address of the world service.")
	geometryServiceAddress = flag.String(
		"geometry_service_address",
		"",
		"Address of the geometry service.",
	)
	numJupyterSessions = flag.Int("num_jupyter_sessions", 1, "Number of parallel Jupyter sessions to maintain.")
)

func main() {
	intrinsic.Init()

	if *numJupyterSessions <= 0 {
		log.Exitf("num_code_execution_sessions must be at least 1, got: 0")
	}

	grpcListener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", *port))
	if err != nil {
		log.Exitf("gRPC failed to listen: %v", err)
	}
	grpcServer := grpc.NewServer(
		grpc.MaxRecvMsgSize(maxMsgSize),
		grpc.MaxSendMsgSize(maxMsgSize),
	)
	reflection.Register(grpcServer)

	serviceContext, shutdownService := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer shutdownService()

	go func() {
		select {
		case <-serviceContext.Done():
			log.InfoContextf(serviceContext, "Received interrupt signal, shutting down gRPC server")
			grpcServer.GracefulStop()
		}
	}()

	codeExecutionService := codeexecutionservice.New(
		serviceContext,
		*jupyterServerHost,
		*worldServiceAddress,
		*geometryServiceAddress,
		*numJupyterSessions,
		nil,
		nil,
	)

	codeexecutionservicegrpcpb.RegisterCodeExecutionServiceServer(
		grpcServer, codeExecutionService,
	)
	codeexecutioninfoservicegrpcpb.RegisterCodeExecutionInfoServiceServer(
		grpcServer, codeExecutionService,
	)

	log.InfoContextf(serviceContext, "Serving gRPC at %s", grpcListener.Addr())
	if err := grpcServer.Serve(grpcListener); err != nil {
		log.FatalContextf(serviceContext, "gRPC server failed: %v", err)
	}
	codeExecutionService.WaitShutdown()
}
