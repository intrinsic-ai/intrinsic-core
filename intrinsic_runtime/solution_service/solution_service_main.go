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

// The solution_service_main command starts a processes service.
package main

import (
	"context"
	"flag"
	"fmt"
	"net"

	"intrinsic_runtime/solution_service/solutionservice"

	"intrinsic/kubernetes/intrinsic"
	"intrinsic/stats/go/telemetry"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/reflection"

	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	grpcpb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	sigrpcpb "intrinsic/kubernetes/workcell_spec/proto/solution_internal_go_proto"
	loggergrpcpb "intrinsic/logging/proto/logger_service_go_proto"
	msgrpcpb "intrinsic/modified_solution/proto/v1/modified_solution_service_go_proto"
	mspb "intrinsic/modified_solution/proto/v1/modified_solution_service_go_proto"
	srgrpcpb "intrinsic/skills/proto/skill_registry_go_proto"
	hssagrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
)

var (
	port            = flag.Int("port", 0, "Port that the modified solution service listens on.")
	hssAddress      = flag.String("hss_address", "", "Address to connect to for the hot-shared-state service.")
	transferAddress = flag.String("transfer_service_address", "", "Address to connect to for the transfer service.")
	skillregAddress = flag.String("skill_registry_address", "", "Address to connect to for the skill registry service.")
	iaAddress       = flag.String("installed_assets_service_address", "", "Address to connect to for the installed assets service.")
	msAddress       = flag.String("modified_solution_service_address", "", "Address to connect to for the modified solution service.")
	loggerAddress   = flag.String("logger_address", "", "Address to connect to for the data logger service.")

	// Tracing flags.
	traceProbability  = flag.Float64("trace_probability", 0.0, "The fraction of requests to upload to Stackdriver Trace.")
	prometheusPort    = flag.Int64("opencensus_metrics_port", 9101, "Which port to serve the prometheus endpoint on.")
	opencensusTracing = flag.Bool("opencensus_tracing", false, "Whether to send traces to opencensus.")
)

func validateFlags() error {
	if *port == 0 {
		return errors.New("--port must be specified")
	}
	if *hssAddress == "" {
		return errors.New("--hss_address must be specified")
	}
	if *transferAddress == "" {
		return errors.New("--transfer_service_address must be specified")
	}
	if *skillregAddress == "" {
		return errors.New("--skill_registry_address must be specified")
	}
	if *iaAddress == "" {
		return errors.New("--installed_assets_service_address must be specified")
	}
	if *msAddress == "" {
		return errors.New("--modified_solution_service_address must be specified")
	}
	return nil
}

type modifiedSolutionSaver struct {
	msC msgrpcpb.ModifiedSolutionServiceClient
}

func (s *modifiedSolutionSaver) Save(ctx context.Context) error {
	_, err := s.msC.SaveModifiedSolution(ctx, &mspb.SaveModifiedSolutionRequest{})
	return err
}

func createOptions() (*solutionservice.Options, error) {
	baseOpts := []grpc.DialOption{
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(&ocgrpc.ClientHandler{}),
	}
	largeRecvOpts := append(baseOpts,
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(1024*1024*1024)), // 1 GiB
	)
	// Using largeRecvOpts since the cluster may contain large behavior trees.
	hssConn, err := grpc.NewClient(*hssAddress, largeRecvOpts...)
	if err != nil {
		return nil, errors.Wrap(err, "failed to connect to hot-shared-state service")
	}

	transferConn, err := grpc.NewClient(*transferAddress, baseOpts...)
	if err != nil {
		return nil, errors.Wrap(err, "failed to connect to transfer service")
	}

	skillregConn, err := grpc.NewClient(*skillregAddress, baseOpts...)
	if err != nil {
		return nil, errors.Wrap(err, "failed to connect to skill registry service")
	}

	iaConn, err := grpc.NewClient(*iaAddress, baseOpts...)
	if err != nil {
		return nil, errors.Wrap(err, "failed to connect to installed assets service")
	}

	msConn, err := grpc.NewClient(*msAddress, baseOpts...)
	if err != nil {
		return nil, errors.Wrap(err, "failed to connect to modified solution service")
	}
	// TODO(b/522759407): Enforce that `logger_address` is set once the feature
	// option is removed and make client creation unconditional.
	var loggerClient loggergrpcpb.DataLoggerClient
	if *loggerAddress != "" {
		// Using largeRecvOpts as a log item responses can be quite large if they
		// contain big behavior trees.
		loggerConn, err := grpc.NewClient(*loggerAddress, largeRecvOpts...)
		if err != nil {
			return nil, errors.Wrap(err, "failed to connect to data logger service")
		}
		loggerClient = loggergrpcpb.NewDataLoggerClient(loggerConn)
	}

	saver := &modifiedSolutionSaver{
		msC: msgrpcpb.NewModifiedSolutionServiceClient(msConn),
	}

	return &solutionservice.Options{
		HSS:             hssagrpcpb.NewHotSharedStateApplicationServiceClient(hssConn),
		Internal:        sigrpcpb.NewSolutionInternalClient(transferConn),
		SkillRegistry:   srgrpcpb.NewSkillRegistryClient(skillregConn),
		InstalledAssets: iagrpcpb.NewInstalledAssetsReaderClient(iaConn),
		Logger:          loggerClient,
		Saver:           saver,
	}, nil
}

func startServer(ctx context.Context) error {
	opts, err := createOptions()
	if err != nil {
		return errors.Wrap(err, "failed to create options for solution service")
	}

	svc := solutionservice.New(opts)

	lis, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", *port))
	if err != nil {
		return errors.Wrapf(err, "failed to listen on %d", *port)
	}
	srvOpts := []grpc.ServerOption{
		grpc.StatsHandler(&ocgrpc.ServerHandler{}),
		// Allow receiving large behavior trees for creation or updates.
		grpc.MaxRecvMsgSize(1024 * 1024 * 1024), // 1 GiB
	}
	srv := grpc.NewServer(srvOpts...)
	grpcpb.RegisterSolutionServiceServer(srv, svc)
	reflection.Register(srv)

	log.InfoContextf(ctx, "Starting solution service at %q", lis.Addr())
	return srv.Serve(lis)
}

func main() {
	intrinsic.Init()

	ctx := context.Background()

	if err := validateFlags(); err != nil {
		log.ExitContextf(ctx, "Invalid flags: %v", err)
	}

	// Set up and serve telemetry.
	// The returned object could be used to implement a graceful shutdown.
	telemetry.Initialize(
		telemetry.WithTracing(*opencensusTracing), // on-prem!
		telemetry.WithProbability(*traceProbability),
		telemetry.EnableMetrics(*prometheusPort),
		telemetry.WithViews(append(ocgrpc.DefaultClientViews, ocgrpc.DefaultServerViews...)),
	)

	if err := startServer(ctx); err != nil {
		log.ExitContextf(ctx, "Exiting: %v", err)
	}
}
