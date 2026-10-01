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

// The workcell_cluster_service provides multiple gRPC services, e.g. Transfer, that clients can
// use to configure a cluster (see go/intrinsic-configfs-design) and interact with applications
// (see go/intrinsic-workcell-ppr).
package main

import (
	"context"
	"crypto/tls"
	"encoding/base64"
	"flag"
	"fmt"
	"net"
	"os"
	"time"

	"intrinsic/assets/artifacts/assetartifactsservice"
	"intrinsic/assets/artifacts/processor"
	"intrinsic/assets/artifacts/uploader"
	"intrinsic/assets/baseclientutils"

	"intrinsic/assets/configuration/assetconfigurationservice"
	"intrinsic/assets/data/dataassetsservice"
	"intrinsic/assets/deploy/assetdeploymentservice"
	"intrinsic/assets/deploy/privileges"
	"intrinsic/assets/deploy/render"
	"intrinsic/assets/install/installedassetsservice"
	"intrinsic/assets/instances/assetinstancesservice"
	"intrinsic/assets/services/systemservicestate"
	"intrinsic/kubernetes/intrinsic"
	"intrinsic/kubernetes/workcell_spec/chartassignment"
	"intrinsic/kubernetes/workcell_spec/deployservice"
	"intrinsic/kubernetes/workcell_spec/transfersvc"
	"intrinsic/longrunning/go/operations"
	"intrinsic/resources/service/resourcereader"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/resources/service/resourceworld"
	"intrinsic/skills/internal/skillruntime"
	"intrinsic/stats/go/telemetry"
	casobservability "intrinsic/storage/content_addressable_storage/pkg/observability"
	"intrinsic/util/go/shutdown"

	log "github.com/golang/glog"
	apps "github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/googlecloudrobotics/core/src/go/pkg/client/versioned"
	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc"
	"go.opencensus.io/trace"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/reflection"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/informers"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	dasgrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	adsgrpcpb "intrinsic/assets/proto/asset_deployment_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"
	acgrpcpb "intrinsic/assets/proto/v1/asset_configuration_go_proto"
	assetinstancesgrpcpb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	solutiondeploymentgrpcpb "intrinsic/assets/proto/v1/solution_deployment_go_proto"
	systemservicestategrpcpb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
	conductorgrpcpb "intrinsic/conductor/proto/conductor_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	idbcsgrpcpb "intrinsic/kubernetes/data_store/proto/cluster_service_go_proto"
	deploygrpcpb "intrinsic/kubernetes/workcell_spec/proto/deploy_go_proto"
	sigrpcpb "intrinsic/kubernetes/workcell_spec/proto/solution_internal_go_proto"
	transferpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	loggerservicepb "intrinsic/logging/proto/logger_service_go_proto"
	resourcecataloginternalgrpcpb "intrinsic/resources/catalog/proto/resource_catalog_internal_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	rdbgrpcpb "intrinsic/resources/proto/runtime_db_go_proto"
	simgrpcpb "intrinsic/simulation/service/proto/first_party/simulation_service_go_proto"
	svsgrpcpb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
	appservicepb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	clusterservicepb "intrinsic/storage/hot_shared_state/proto/v1/cluster_service_go_proto"
	rsgrpcpb "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service_go_proto"
	owsgrpcpb "intrinsic/world/public/proto/object_world_service_go_proto"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

const (
	numBytesInGiB       = 1024 * 1024 * 1024
	operationsQueueSize = 50
	// operationsWorkers is 1 so requests for installed assets are handled
	// sequentially, and always operate on the current state of the cluster
	// left by the previous request.
	operationsWorkers  = 1
	artifactsQueueSize = 100
	artifactsWorkers   = 10
)

var (
	gcpProject                         = flag.String("gcp_project", "giza-workcells", "The GCP project to install released workcellspecs from.")
	transferServicePort                = flag.Int("transfer_service_port", 0, "The transfer server port")
	adsPort                            = flag.Int("asset_deployment_service_port", 0, "The port on which the asset deployment service is exposed.")
	solutionDeploymentPort             = flag.Int("solution_deployment_service_port", 0, "The port on which the solution deployment service is exposed.")
	assetArtifactsPort                 = flag.Int("asset_artifacts_service_port", 0, "The port on which the AssetArtifacts service is exposed.")
	assetInstancesPort                 = flag.Int("asset_instances_service_port", 0, "The port on which the asset instances service is exposed.")
	hssServiceAddress                  = flag.String("hss_service_address", "", "The hot shared	state service address")
	casProxyAddress                    = flag.String("cas_proxy_address", "", "The CAS onprem proxy address")
	runtimeDbServiceAddress            = flag.String("runtime_db_service_address", "", "The runtime DB service address")
	resourceCatalogAddress             = flag.String("resource_catalog_address", "", "Address of the ResourceCatalog gRPC service.")
	assetCatalogAddress                = flag.String("asset_catalog_address", "", "Address of the AssetCatalog gRPC service.")
	conductorServiceAddress            = flag.String("conductor_service_address", "", "Address of the ConductorService gRPC service.")
	loggerServiceAddress               = flag.String("data_logger_grpc_service_address", "", "Address of the Data Logger gRPC service.")
	initResourceInstanceDataFilesImage = flag.String("init_resource_instance_data_files_image", "", "Container image reference to be used for initializing resource instances' data files.")
	objectWorldServiceAddress          = flag.String("object_world_service_address", "", "Address of the Object World gRPC service.")
	simServiceAddress                  = flag.String("sim_service_address", "", "The simulation service address")
	cloudIngressAddress                = flag.String("cloud_ingress_address", "", "Cloud ingress address.")
	registry                           = flag.String("registry", "", "The OCI repository that intrinsic-base and intrinsic-app-chart were released to. Default: gcr.io/<gcp_project>")

	// For configuring telemetry.
	traceProbability  = flag.Float64("trace_probability", 1e-4, "The fraction of requests to upload to Stackdriver Trace.")
	prometheusPort    = flag.Int64("opencensus_metrics_port", 9101, "Which port to serve the prometheus endpoint on.")
	opencensusTracing = flag.Bool("opencensus_tracing", false, "Whether to send traces to opencensus.")

	enableCasPeerMetadata       = flag.Bool("enable_cas_peer_metadata", false, "Whether to enable CAS peer metadata.")
	enableSecurityContext       = flag.Bool("enable_security_context", false, "Enable services with non-default security context")
	enableRelaxedHostPathChecks = flag.Bool("enable_relaxed_host_path_checks", false, "Enable services with non-default host path mounts")
	numSkillPods                = flag.Int("num_skill_pods", 0, "The number of pods to distribute skills across. Must be > 0.")
)

var onSolutionUpdateFactory = func(rtrClient resourcetyperuntime.Client) (func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error, func()) {
	return func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error {
		return nil
	}, func() {}
}

var startOptionalClusterMonitoring = func(ctx context.Context, factory informers.SharedInformerFactory, crcClient versioned.Interface) func() {
	return func() {}
}

var getClusterInfo = func(ctx context.Context, crcClient versioned.Interface) transfersvc.ClusterInfo {
	return transfersvc.ClusterInfo{
		Name:                   "local",
		CanDoPhysicalExecution: true,
		CanDoSim:               true,
		HasGpu:                 false,
		GCPProject:             "none",
		Registry:               "gcr.io/local",
	}
}

func makeKubeClients() (versioned.Interface, kubernetes.Interface) {
	cfg, err := rest.InClusterConfig()
	if err != nil {
		log.Fatalf("rest.InClusterConfig() failed: %v", err)
	}
	// Increase maxQPS so ApplyWorkcellSpec is faster to create/update many ChartAssignments. A 200
	// request burst is enough for 100 skills (GET+POST for each update).
	cfg.QPS = float32(50)
	cfg.Burst = 200
	crcClient, err := versioned.NewForConfig(cfg)
	if err != nil {
		log.Fatalf("versioned.NewForConfig() failed: %v", err)
	}
	coreClient, err := kubernetes.NewForConfig(cfg)
	if err != nil {
		log.Fatalf("kubernetes.NewForConfig() failed: %v", err)
	}
	return crcClient, coreClient
}

func makeCloudClients() (idbcsgrpcpb.ClusterServiceClient, svsgrpcpb.SolutionVersionServiceClient, func()) {
	log.Infof("Connecting to the cloud ingress at %q", *cloudIngressAddress)
	conn, err := grpc.NewClient(
		*cloudIngressAddress,
		// Use secure connection since this is going to the compute project
		// endpoint, rather than just within the cluster where we can use
		// insecure.
		grpc.WithTransportCredentials(credentials.NewTLS(new(tls.Config))),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(numBytesInGiB)),
		grpc.WithDefaultCallOptions(grpc.MaxCallSendMsgSize(numBytesInGiB)),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to the cloud ingress at %q: %v", *cloudIngressAddress, err)
	}
	return idbcsgrpcpb.NewClusterServiceClient(conn), svsgrpcpb.NewSolutionVersionServiceClient(conn), func() { conn.Close() }
}

func makeResourceCatalogInternalClient() (resourcecataloginternalgrpcpb.ResourceCatalogInternalClient, func()) {
	log.Infof("connecting to ResourceCatalogInternal service at %s", *resourceCatalogAddress)
	// In production this connects to the compute project endpoint (above), but
	// it is faked out locally within the cluster for testing.  This switches
	// between the two and adds retries and max size functionality to be able
	// to pull down large asset information.
	conn, err := baseclientutils.NewCatalogClient(*resourceCatalogAddress)
	if err != nil {
		log.Exitf("failed to connect to resource catalog internal service: %v", err)
	}
	return resourcecataloginternalgrpcpb.NewResourceCatalogInternalClient(conn), func() { conn.Close() }
}

func makeAssetCatalogInternalClient() (acigrpcpb.AssetCatalogInternalClient, func()) {
	log.Infof("connecting to AssetCatalogInternal service at %s", *assetCatalogAddress)
	// In production this connects to a global asset project, but it is faked
	// out locally within the cluster for testing.  This switches between the
	// two and adds retries and max size functionality to be able to pull down
	// large asset information.
	conn, err := baseclientutils.NewCatalogClient(*assetCatalogAddress)
	if err != nil {
		log.Exitf("failed to connect to asset catalog internal service: %v", err)
	}
	return acigrpcpb.NewAssetCatalogInternalClient(conn), func() { conn.Close() }
}

func makeLoggerClient() (loggerservicepb.DataLoggerClient, func()) {
	log.Infof("Connecting to the data logger at %q", *loggerServiceAddress)
	conn, err := grpc.NewClient(
		*loggerServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		log.Exitf("Failed to connect to the data logger at %q", *loggerServiceAddress)
	}
	return loggerservicepb.NewDataLoggerClient(conn), func() { conn.Close() }
}

func makeObjectWorldClient() (owsgrpcpb.ObjectWorldServiceClient, func()) {
	log.Infof("Connecting to the object world service at %q", *objectWorldServiceAddress)
	conn, err := grpc.NewClient(
		*objectWorldServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to world service %q: %v", *objectWorldServiceAddress, err)
	}
	return owsgrpcpb.NewObjectWorldServiceClient(conn), func() { conn.Close() }
}

func makeCASClient() (caspb.ContentAddressableStorageServiceClient, func()) {
	unary, stream := casobservability.GRPCClientInterceptors("workcell-cluster-service")
	conn, err := grpc.NewClient(
		*casProxyAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithUnaryInterceptor(unary),
		grpc.WithStreamInterceptor(stream),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to the CAS proxy %q: %v", *casProxyAddress, err)
	}
	return caspb.NewContentAddressableStorageServiceClient(conn), func() { conn.Close() }
}

func makeHSSClients() (clusterservicepb.HotSharedStateClusterServiceClient, appservicepb.HotSharedStateApplicationServiceClient, rsgrpcpb.HotSharedStateResourceSetServiceClient, func()) {
	log.Infof("Connecting to the hot shared state service at %q", *hssServiceAddress)
	conn, err := grpc.NewClient(
		*hssServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(numBytesInGiB)),
		grpc.WithDefaultCallOptions(grpc.MaxCallSendMsgSize(numBytesInGiB)),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to the hot shared state service %q: %v", *hssServiceAddress, err)
	}
	return clusterservicepb.NewHotSharedStateClusterServiceClient(conn), appservicepb.NewHotSharedStateApplicationServiceClient(conn), rsgrpcpb.NewHotSharedStateResourceSetServiceClient(conn), func() { conn.Close() }
}

func makeRuntimeDBClient() (rdbgrpcpb.RuntimeDbClient, func()) {
	log.Infof("Connecting to the runtime db service at %q", *runtimeDbServiceAddress)
	conn, err := grpc.NewClient(
		*runtimeDbServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(numBytesInGiB)),
		grpc.WithDefaultCallOptions(grpc.MaxCallSendMsgSize(numBytesInGiB)),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to the runtime db service %q: %v", *runtimeDbServiceAddress, err)
	}
	return rdbgrpcpb.NewRuntimeDbClient(conn), func() { conn.Close() }
}

func makeSimClient() (simgrpcpb.SimulationServiceClient, func()) {
	log.Infof("Connecting to the simulation service at %q", *simServiceAddress)
	conn, err := grpc.NewClient(
		*simServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		log.Exitf("failed to connect to sim service: %v", err.Error())
	}
	return simgrpcpb.NewSimulationServiceClient(conn), func() { conn.Close() }
}

func makeConductorClient() (conductorgrpcpb.ConductorServiceClient, func()) {
	log.Infof("Connecting to ConductorService at %q", *conductorServiceAddress)
	conn, err := grpc.NewClient(
		*conductorServiceAddress,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		log.Exitf("Failed to establish connection to the ConductorService at %q: %v", *conductorServiceAddress, err)
	}
	return conductorgrpcpb.NewConductorServiceClient(conn), func() { conn.Close() }
}

// defaultWorkcellSpec creates a workcell spec from the embedded app chart in
// combination with the values with which intrinsic-base was started.
// TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
func defaultWorkcellSpec(ctx context.Context, crcClient versioned.Interface) (*transferpb.WorkcellSpec, error) {
	appChartEncoded := os.Getenv("INTRINSIC_APP_CHART")
	if appChartEncoded == "" {
		return nil, fmt.Errorf("no default app chart provided via INTRINSIC_APP_CHART environment variable")
	}
	appChart, err := base64.StdEncoding.DecodeString(appChartEncoded)
	if err != nil {
		return nil, errors.Wrapf(err, "decode INTRINSIC_APP_CHART")
	}
	ws := &transferpb.WorkcellSpec{
		Items: []*transferpb.WorkcellSpecItem{
			{
				Item: &transferpb.WorkcellSpecItem_ChartAssignment{
					ChartAssignment: &transferpb.ChartAssignment{
						Yaml: []byte(appChart),
					},
				},
			},
		},
	}

	list, err := crcClient.AppsV1alpha1().ChartAssignments().List(ctx, metav1.ListOptions{LabelSelector: "app=intrinsic-base"})
	if err != nil {
		return nil, fmt.Errorf("unable to fetch chart assignments resources for app=intrinsic-base: %w", err)
	}
	if got := len(list.Items); got != 1 {
		return nil, fmt.Errorf("unexpected number of chart assignments for app=intrinsic-base: %d, want: 1", got)
	}
	values := list.Items[0].Spec.Chart.Values
	// Remove values specific to the chart creation.  We'll use these values to
	// override intrinsic-app-chart, but don't want to touch these.
	for _, k := range []string{
		"app_name",
		"embed_chart",
		"embed_chart_files",
		"image_lists",
		"images",
		"lfs_paths",
	} {
		delete(values, k)
	}

	if err := chartassignment.UpdateWorkcellSpec(func(_ int, ca *apps.ChartAssignment) error {
		chartassignment.SetLabel(ca, "exclusive", true)
		if err := chartassignment.MergeValues(ca, values); err != nil {
			return err
		}
		return nil
	}, ws); err != nil {
		return nil, errors.Wrap(err, "update chart assignment")
	}
	return ws, nil
}

func main() {
	intrinsic.Init()
	ctx, cancel := shutdown.RegisterContext(context.Background())
	defer cancel()

	if *initResourceInstanceDataFilesImage == "" {
		log.ExitContextf(ctx, "--init_resource_instance_data_files_image is required")
	}
	if *numSkillPods <= 0 {
		log.ExitContextf(ctx, "--num_skill_pods is required and must be > 0")
	}

	telem := telemetry.Initialize(
		telemetry.WithTracing(*opencensusTracing), // on-prem!
		telemetry.WithProbability(*traceProbability),
		telemetry.EnableMetrics(*prometheusPort),
		telemetry.WithViews(append(ocgrpc.DefaultClientViews, ocgrpc.DefaultServerViews...)),
	)
	defer telem.Shutdown(ctx)

	crcClient, coreClient := makeKubeClients()
	cloudClusterClient, solutionVersionServiceClient, stop := makeCloudClients()
	defer stop()
	resourceCatalogClient, stop := makeResourceCatalogInternalClient()
	defer stop()
	assetCatalogInternalClient, stop := makeAssetCatalogInternalClient()
	defer stop()
	objectWorldServiceClient, stop := makeObjectWorldClient()
	defer stop()
	casClient, stop := makeCASClient()
	defer stop()
	clusterClient, applicationClient, resourceSetClient, stop := makeHSSClients()
	defer stop()
	runtimeDBClient, stop := makeRuntimeDBClient()
	defer stop()
	simClient, stop := makeSimClient()
	defer stop()
	conductorServiceClient, stop := makeConductorClient()
	defer stop()

	clusterInfo := getClusterInfo(ctx, crcClient)
	transferService := transfersvc.NewServiceWithClients(crcClient, coreClient, clusterInfo)
	resourceTypeRuntimeClient := resourcetyperuntime.CreateCachedClient(runtimeDBClient)
	skillRuntimeClient := skillruntime.CreateClient(runtimeDBClient)

	onSolutionUpdate, stopOnSolutionUpdate := onSolutionUpdateFactory(resourceTypeRuntimeClient)
	defer stopOnSolutionUpdate()

	clusterParams := render.ClusterParams{
		HasGpu:       clusterInfo.HasGpu,
		NumSkillPods: *numSkillPods,
	}

	initDataFiles := render.InitDataFilesParams{
		Image:                 *initResourceInstanceDataFilesImage,
		CASAddress:            *casProxyAddress,
		EnableCASPeerMetadata: *enableCasPeerMetadata,
	}

	validator := privileges.NewValidator(privileges.ValidateOptions{
		EnableSecurityContext:       *enableSecurityContext,
		EnableRelaxedHostPathChecks: *enableRelaxedHostPathChecks,
	})

	queue := operations.NewQueue(operationsQueueSize)
	queue.Start(operationsWorkers)
	// There's a period of time here where the server will stop responding
	// to requests on the operations service, but may still be completing
	// some operations.  At that point the user will no longer be able to
	// check their status or cancel them, but we will at least run them to
	// completion.  We can run the operations services on a separate port if we
	// want to gracefully shutdown the operations service after the queue.
	defer queue.Stop()

	solutionDeploymentOps := operations.NewMap()
	deployService := deployservice.New(deployservice.Options{
		ConductorClient:               conductorServiceClient,
		ClusterClient:                 clusterClient,
		CloudClusterClient:            cloudClusterClient,
		ApplicationClient:             applicationClient,
		AssetCatalogInternalClient:    assetCatalogInternalClient,
		ResourceCatalogInternalClient: resourceCatalogClient,
		ResourceTypeRuntimeClient:     resourceTypeRuntimeClient,
		SkillRuntimeClient:            skillRuntimeClient,
		TransferService:               transferService,
		ClusterInfo:                   clusterInfo,
		LoggerClient:                  nil,
		OnSolutionUpdate:              onSolutionUpdate,
		Validator:                     validator,
		DefaultWorkcellSpec: func(ctx context.Context) (*transferpb.WorkcellSpec, error) {
			return defaultWorkcellSpec(ctx, crcClient)
		},
		SolutionVersionServiceClient: solutionVersionServiceClient,
		ClusterParams:                clusterParams,
		InitDataFilesParams:          initDataFiles,
		Runner:                       operations.NewRunner(solutionDeploymentOps, queue),
	})

	installedAssetsOps := operations.NewMap()
	installedAssets := installedassetsservice.New(installedassetsservice.Options{
		ACIClient:           assetCatalogInternalClient,
		RTRClient:           resourceTypeRuntimeClient,
		AppClient:           applicationClient,
		SVSClient:           solutionVersionServiceClient,
		OWSClient:           objectWorldServiceClient,
		CASClient:           casClient,
		TransferService:     transferService,
		Runner:              operations.NewRunner(installedAssetsOps, queue),
		OnSolutionUpdate:    onSolutionUpdate,
		ClusterParams:       clusterParams,
		InitDataFilesParams: initDataFiles,
		Validator:           validator,
	})

	reader := resourcereader.NewClient(resourcereader.NewClientOpts{
		RSSClient: resourceSetClient,
		RTRClient: resourceTypeRuntimeClient,
	})
	assetDeploymentOps := operations.NewMap()
	ads := assetdeploymentservice.New(assetdeploymentservice.Options{
		ApplicationClient: applicationClient,
		OWSClient:         objectWorldServiceClient,
		ACIClient:         assetCatalogInternalClient,
		World: resourceworld.NewClient(resourceworld.NewClientOpts{
			RR:        reader,
			RSSClient: resourceSetClient,
			OWSClient: objectWorldServiceClient,
		}),
		SimClient:             simClient,
		TransferServiceClient: transferService,
		RTRClient:             resourceTypeRuntimeClient,
		K8sClient:             coreClient,
		Runner:                operations.NewRunner(assetDeploymentOps, queue),
		Validator:             validator,
		ClusterParams:         clusterParams,
		InitDataFilesParams:   initDataFiles,
		OnSolutionUpdate:      onSolutionUpdate,
	})

	assetInstances := assetinstancesservice.New(assetinstancesservice.Options{
		AppClient: applicationClient,
		RTRClient: resourceTypeRuntimeClient,
	})

	log.InfoContextf(ctx, "Starting DataAssets service at %d", *transferServicePort)
	dataAssets := dataassetsservice.New(dataassetsservice.Options{
		RTRClient: resourceTypeRuntimeClient,
	})

	log.InfoContextf(ctx, "Starting SystemServiceState service at %d", *transferServicePort)

	factory := informers.NewSharedInformerFactory(coreClient, 10*time.Minute)

	stopOptionalClusterMonitoring := startOptionalClusterMonitoring(ctx, factory, crcClient)
	defer stopOptionalClusterMonitoring()

	systemServiceState, err := systemservicestate.New(ctx, &systemservicestate.Options{
		K8sClient: coreClient,
		Factory:   factory,
	})
	if err != nil {
		log.ExitContextf(ctx, "Failed to start SystemServiceState service: %v", err)
	}

	factory.Start(ctx.Done())
	factory.WaitForCacheSync(ctx.Done())

	assetConfigService := assetconfigurationservice.New(assetconfigurationservice.Options{
		RRClient:  reader,
		RTRClient: resourceTypeRuntimeClient,
	})

	artifactsQueue := operations.NewQueue(artifactsQueueSize)
	artifactsQueue.Start(artifactsWorkers)
	defer artifactsQueue.Stop()
	artifactsOps := operations.NewMap()
	artifactsRunner := operations.NewRunner(artifactsOps, artifactsQueue)
	ap, err := processor.New(&processor.Config{
		CASClient:        casClient,
		CASClientFactory: processor.DefaultCASClientFactory,
		Runner:           artifactsRunner,
	})
	if err != nil {
		log.ExitContextf(ctx, "failed to create artifact processor: %v", err)
	}
	log.InfoContextf(ctx, "Starting AssetArtifacts service at %d", *assetArtifactsPort)
	artifactsSvc, err := assetartifactsservice.New(ap, uploader.NewUploads(uploader.New(casClient)))
	if err != nil {
		log.ExitContextf(ctx, "failed to create artifacts service: %v", err)
	}

	group, groupCtx := errgroup.WithContext(ctx)
	// Always await completion go routines in group.  We'll start several gRPC
	// servers concurrently, but want to ensure all of them have stopped prior
	// to exiting.
	defer func() {
		log.InfoContext(ctx, "Awaiting shutdown or error")
		if err := group.Wait(); err != nil {
			log.ExitContextf(ctx, "group task failed: %v", err)
		}
		log.InfoContext(ctx, "Group exited successfully.  Stopping.")
	}()
	if lis, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", *transferServicePort)); err != nil {
		log.ExitContextf(ctx, "Failed to listen on transfer service port: %v", err)
	} else {
		server := grpc.NewServer(
			grpc.StatsHandler(&ocgrpc.ServerHandler{}),
			grpc.MaxRecvMsgSize(numBytesInGiB),
		)
		dasgrpcpb.RegisterDataAssetsServer(server, dataAssets)
		deploygrpcpb.RegisterDeployServiceServer(server, deployService)
		iagrpcpb.RegisterInstalledAssetsReaderServer(server, installedAssets)
		iagrpcpb.RegisterInstalledAssetsServer(server, installedAssets)
		lrogrpcpb.RegisterOperationsServer(server, operations.NewServer(installedAssetsOps))
		sigrpcpb.RegisterSolutionInternalServer(server, transferService)
		systemservicestategrpcpb.RegisterSystemServiceStateServer(server, systemServiceState)
		acgrpcpb.RegisterAssetConfigurationServiceServer(server, assetConfigService)

		reflection.Register(server)
		group.Go(func() error {
			if err := server.Serve(lis); err != nil {
				return fmt.Errorf("workcell-cluster-service server stopped with an error: %w", err)
			}
			log.InfoContext(groupCtx, "workcell-cluster-service server stopped successfully")
			return nil
		})
		go func() {
			<-groupCtx.Done()
			log.InfoContextf(groupCtx, "shutting down workcell-cluster-service server")
			server.GracefulStop()
		}()
	}

	if lis, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", *assetArtifactsPort)); err != nil {
		log.ExitContextf(ctx, "Failed to listen on artifacts service port: %v", err)
	} else {
		server := grpc.NewServer(
			grpc.StatsHandler(&ocgrpc.ServerHandler{}),
			grpc.MaxRecvMsgSize(numBytesInGiB),
		)
		assetartifactspb.RegisterAssetArtifactsServer(server, artifactsSvc)
		lrogrpcpb.RegisterOperationsServer(server, operations.NewServer(artifactsOps))

		reflection.Register(server)
		group.Go(func() error {
			if err := server.Serve(lis); err != nil {
				return fmt.Errorf("artifacts server stopped with an error: %w", err)
			}
			log.InfoContext(groupCtx, "artifacts server stopped successfully")
			return nil
		})
		go func() {
			<-groupCtx.Done()
			log.InfoContextf(groupCtx, "shutting down artifacts server")
			server.GracefulStop()
		}()
	}

	// TODO: b/448388290: Remove once no longer needed for compatibility.
	if lis, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%d", *adsPort)); err != nil {
		log.FatalContextf(ctx, "Failed to listen: %v", err)
	} else {
		// Gather stats to how frequently this endpoint is being pinged.  Once
		// we see it taper down to a reasonable level, we can break old
		// perception service assets.
		stats := grpc.UnaryServerInterceptor(func(ctx context.Context, req any, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (any, error) {
			ctx, span := trace.StartSpan(ctx, "workcell_cluster_service.ads_port")
			span.AddAttributes(trace.StringAttribute("method", info.FullMethod))
			defer span.End()
			return handler(ctx, req)
		})
		server := grpc.NewServer(
			grpc.StatsHandler(&ocgrpc.ServerHandler{}),
			grpc.MaxRecvMsgSize(numBytesInGiB),
			grpc.UnaryInterceptor(stats),
		)
		adsgrpcpb.RegisterAssetDeploymentServiceServer(server, ads)
		lrogrpcpb.RegisterOperationsServer(server, operations.NewServer(assetDeploymentOps))

		reflection.Register(server)
		group.Go(func() error {
			if err := server.Serve(lis); err != nil {
				return fmt.Errorf("asset-deployment server stopped with an error: %w", err)
			}
			log.InfoContext(groupCtx, "asset-deployment server stopped successfully")
			return nil
		})
		go func() {
			<-groupCtx.Done()
			log.InfoContextf(groupCtx, "shutting down asset-deployment server")
			server.GracefulStop()
		}()
	}

	if lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *solutionDeploymentPort)); err != nil {
		log.FatalContextf(ctx, "Failed to listen: %v", err)
	} else {
		server := grpc.NewServer(
			grpc.StatsHandler(&ocgrpc.ServerHandler{}),
			grpc.MaxSendMsgSize(numBytesInGiB),
			grpc.MaxRecvMsgSize(numBytesInGiB),
		)
		solutiondeploymentgrpcpb.RegisterSolutionDeploymentServiceServer(server, deployService)
		lrogrpcpb.RegisterOperationsServer(server, operations.NewServer(solutionDeploymentOps))

		reflection.Register(server)
		group.Go(func() error {
			if err := server.Serve(lis); err != nil {
				return fmt.Errorf("deployment-v1 server stopped with an error: %w", err)
			}
			log.InfoContext(groupCtx, "deployment-v1 server stopped successfully")
			return nil
		})
		go func() {
			<-groupCtx.Done()
			log.InfoContextf(groupCtx, "shutting down deployment-v1 server")
			server.GracefulStop()
		}()
	}

	if lis, err := net.Listen("tcp", fmt.Sprintf(":%d", *assetInstancesPort)); err != nil {
		log.FatalContextf(ctx, "Failed to listen: %v", err)
	} else {
		server := grpc.NewServer(
			grpc.StatsHandler(&ocgrpc.ServerHandler{}),
			grpc.MaxSendMsgSize(numBytesInGiB),
			grpc.MaxRecvMsgSize(numBytesInGiB),
		)
		assetinstancesgrpcpb.RegisterAssetInstancesServer(server, assetInstances)
		assetinstancesgrpcpb.RegisterAssetInstancesReaderServer(server, assetInstances)

		reflection.Register(server)
		group.Go(func() error {
			if err := server.Serve(lis); err != nil {
				return fmt.Errorf("asset-instances-v1 server stopped with an error: %w", err)
			}
			log.InfoContext(groupCtx, "asset-instances-v1 server stopped successfully")
			return nil
		})
		go func() {
			<-groupCtx.Done()
			log.InfoContextf(groupCtx, "shutting down asset-instances-v1 server")
			server.GracefulStop()
		}()
	}

	log.InfoContext(ctx, "Starting an idle workcell spec")
	if err := deployService.StartIdleWorkcellSpec(ctx); err != nil {
		log.WarningContextf(ctx, "Failed to start idle workcell spec: %v", err)
	}
	log.InfoContext(ctx, "Startup complete")
}
