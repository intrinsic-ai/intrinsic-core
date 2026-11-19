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

// Package arguments handles CLI arguments to the HTTP-gateway.
package arguments

import (
	"flag"
	"sync"
)

type CLIArguments struct {
	HttpPort                              int64
	TraceProbability                      float64
	PrometheusPort                        int64
	OpencensusTracing                     bool
	AssetConfigurationServiceAddress      string
	BlackboardServiceAddress              string
	ClusterServiceAddress                 string
	ExecutiveServiceAddress               string
	GeometryServiceAddress                string
	IPCUpdaterServiceAddress              string
	InstalledAssetsAddress                string
	ObjectWorldServiceAddress             string
	ProtoBuilderAddress                   string
	ProtoRegistryAddress                  string
	SceneObjectImportServiceAddress       string
	SolutionServiceAddress                string
	TrainServiceAddress                   string
	LoggerServiceAddress                  string
	ReplayServiceAddress                  string
	WebPubSubServiceAddress               string
	ConnectivityDiagnosticsServiceAddress string
	EnableHttpGatewayPubsub               bool
	EnableTURNAuthServers                 bool
	PortalDomain                          string
	GRPCPort                              int64
}

var (
	instance *CLIArguments
	once     sync.Once
)

const (
	ingressAddress = "istio-ingressgateway.app-ingress.svc.cluster.local:80"
)

// Get registers the flags on the default command line set (flag.CommandLine)
// on the first call and returns the singleton instance of CLIArguments.
// Subsequent calls return the existing instance without re-registering flags.
func Get() *CLIArguments {
	once.Do(func() {
		instance = &CLIArguments{}

		// Register flags globally because that's what intrinsic.Init() uses
		flag.Int64Var(&instance.HttpPort, "port", 8080, "Which port to handle HTTP/JSON requests on.")
		flag.Int64Var(&instance.GRPCPort, "grpc_port", 8082, "Which port to handle gRPC requests on.")
		flag.Float64Var(&instance.TraceProbability, "trace_probability", 0.0, "The fraction of requests to upload to Stackdriver Trace.")
		flag.Int64Var(&instance.PrometheusPort, "opencensus_metrics_port", 9101, "Which port to serve the prometheus endpoint on.")
		flag.BoolVar(&instance.OpencensusTracing, "opencensus_tracing", false, "Whether to send traces to opencensus.")
		flag.StringVar(&instance.AssetConfigurationServiceAddress, "asset-configuration-service-endpoint", ingressAddress, "Asset configuration service endpoint")
		flag.StringVar(&instance.BlackboardServiceAddress, "blackboard-service-endpoint", ingressAddress, "Blackboard service endpoint")
		flag.StringVar(&instance.ClusterServiceAddress, "cluster-service-endpoint", ingressAddress, "Cluster service endpoint")
		flag.StringVar(&instance.ExecutiveServiceAddress, "executive-service-endpoint", ingressAddress, "Executive service endpoint")
		flag.StringVar(&instance.GeometryServiceAddress, "geometry-service-endpoint", ingressAddress, "Geometry service endpoint")
		flag.StringVar(&instance.IPCUpdaterServiceAddress, "ipc-updater-service-endpoint", ingressAddress, "IPC updater service endpoint")
		flag.StringVar(&instance.InstalledAssetsAddress, "installed-assets-endpoint", ingressAddress, "Installed Assets service endpoint")
		flag.StringVar(&instance.ObjectWorldServiceAddress, "object-world-service-endpoint", ingressAddress, "Object world service endpoint")
		flag.StringVar(&instance.ProtoBuilderAddress, "proto-builder-endpoint", ingressAddress, "Proto Builder endpoint")
		flag.StringVar(&instance.ProtoRegistryAddress, "proto-registry-endpoint", ingressAddress, "Proto Registry service endpoint")
		flag.StringVar(&instance.SceneObjectImportServiceAddress, "scene-object-import-service-endpoint", ingressAddress, "Scene object import service endpoint")
		flag.StringVar(&instance.SolutionServiceAddress, "solution-service-endpoint", ingressAddress, "Solution service endpoint")
		flag.StringVar(&instance.TrainServiceAddress, "train-service-endpoint", ingressAddress, "Train service endpoint")
		flag.StringVar(&instance.ReplayServiceAddress, "replay-service-endpoint", ingressAddress, "Replay service endpoint")
		flag.StringVar(&instance.LoggerServiceAddress, "logger-service-endpoint", ingressAddress, "Logger service endpoint")
		flag.StringVar(&instance.WebPubSubServiceAddress, "webpubsub-service-endpoint", "", "WebPubSub service endpoint")
		flag.StringVar(&instance.ConnectivityDiagnosticsServiceAddress, "connectivity-diagnostics-service-endpoint", ingressAddress, "Connectivity diagnostics service endpoint")
		flag.BoolVar(&instance.EnableHttpGatewayPubsub, "enable_http_gateway_pubsub", false, "Whether to enable pubsub support on the HTTP gateway.")
		flag.BoolVar(&instance.EnableTURNAuthServers, "enable_webrtc_turn_auth_servers", false, "Whether to use token-based TURN server authentication.")
		flag.StringVar(&instance.PortalDomain, "portal_domain", "flowstate.intrinsic.ai", "Portal domain used for TURN credential exchange and ICE servers.")
	})
	return instance
}
