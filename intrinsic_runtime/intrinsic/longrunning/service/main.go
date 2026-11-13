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

package main

import (
	"context"
	"flag"
	"fmt"
	"net"

	"intrinsic/kubernetes/intrinsic"
	"intrinsic/stats/go/telemetry"
	intrinsicflag "intrinsic/util/flag"
	"intrinsic/util/go/shutdown"

	"intrinsic/longrunning/go/proxy"

	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/reflection"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

const (
	// Max of 150Mb sent or received.
	grpcMaxSendMessageSize    = 150 * 1024 * 1024
	grpcMaxReceiveMessageSize = 150 * 1024 * 1024
)

var (
	port                 = flag.Int("port", 0, "port to listen on (must be specified)")
	grpcProxies          = intrinsicflag.MultiString("grpc_proxy", nil, "Proxy address. Can be specified multiple times.")
	transientGrpcProxies = intrinsicflag.MultiString("transient_grpc_proxy", nil, "Proxy address. Can be specified multiple times.")
	traceProbability     = flag.Float64("trace_probability", 1e-4, "The fraction of requests to upload to Stackdriver Trace.")
	prometheusPort       = flag.Int64("opencensus_metrics_port", 9101, "Which port to serve the prometheus endpoint on.")
	opencensusTracing    = flag.Bool("opencensus_tracing", false, "Whether to send traces to opencensus.")
)

func main() {
	intrinsic.Init()
	ctx, cancel := shutdown.RegisterContext(context.Background())
	defer cancel()

	if *port == 0 {
		log.ExitContextf(ctx, "--port must be specified")
	}

	telem := telemetry.Initialize(
		telemetry.WithTracing(*opencensusTracing), // on-prem!
		telemetry.WithProbability(*traceProbability),
		telemetry.EnableMetrics(*prometheusPort),
		telemetry.WithViews(append(ocgrpc.DefaultClientViews, ocgrpc.DefaultServerViews...)),
	)
	defer telem.Shutdown(ctx)

	var proxies []lrogrpcpb.OperationsClient
	for _, addr := range *grpcProxies {
		if addr == "" {
			log.ExitContextf(ctx, "Found an empty proxy address in grpc_proxy")
		}
		conn, err := grpc.NewClient(addr,
			grpc.WithTransportCredentials(insecure.NewCredentials()),
		)
		if err != nil {
			log.ExitContextf(ctx, "Unable to connect to proxy %q: %v", addr, err)
		}
		proxies = append(proxies, lrogrpcpb.NewOperationsClient(conn))
	}
	for _, addr := range *transientGrpcProxies {
		if addr == "" {
			log.ExitContextf(ctx, "Found an empty proxy address in transient_grpc_proxy")
		}
		conn, err := grpc.NewClient(addr,
			grpc.WithTransportCredentials(insecure.NewCredentials()),
		)
		if err != nil {
			log.ExitContextf(ctx, "Unable to connect to transient proxy %q: %v", addr, err)
		}
		proxies = append(proxies, proxy.NewTransientProxy(lrogrpcpb.NewOperationsClient(conn)))
	}

	service := proxy.NewProxySet(proxies)

	serverAddress := fmt.Sprintf(":%d", *port)
	lis, err := net.Listen("tcp", serverAddress)
	if err != nil {
		log.ExitContextf(ctx, "Failed to listen on %s: %v", serverAddress, err)
	}

	server := grpc.NewServer(
		grpc.MaxRecvMsgSize(grpcMaxReceiveMessageSize),
		grpc.MaxSendMsgSize(grpcMaxSendMessageSize),
	)

	lrogrpcpb.RegisterOperationsServer(server, service)
	reflection.Register(server)

	log.Infof("Starting Operations server %s", serverAddress)

	go func() {
		<-ctx.Done()
		log.Infof("Shutting down operations server")
		server.GracefulStop()
	}()

	if err := server.Serve(lis); err != nil {
		log.ExitContextf(ctx, "Server stopped with error: %v", err)
	}
}
