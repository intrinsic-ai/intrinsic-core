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

// Package deployservice provides a service to deploy applications on the cluster.
package deployservice

import (
	"context"
	"fmt"
	"maps"
	"path"
	"slices"
	"time"

	"intrinsic/assets/conversion/applicationasset"
	"intrinsic/assets/conversion/runtime"
	"intrinsic/assets/dependencies/graph"
	"intrinsic/assets/dependencies/platform"
	"intrinsic/assets/dependencies/runtimegraph"
	"intrinsic/assets/deploy/privileges"
	"intrinsic/assets/deploy/render"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/instances/instanceconversion"
	"intrinsic/assets/viewutils"
	"intrinsic/config/applicationview"
	"intrinsic/config/convertrunnables"
	"intrinsic/kubernetes/acl/clientcontext"
	"intrinsic/kubernetes/workcell_spec/runtimedbtransfer"
	"intrinsic/kubernetes/workcell_spec/transfersvc"
	"intrinsic/longrunning/go/operations"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/skills/internal/skillruntime"
	"intrinsic/stats/go/telemetry"
	"intrinsic/util/go/pointer"
	"intrinsic/util/go/xiter"
	"intrinsic/util/proto/descriptorcompatibility"
	espb "intrinsic/util/status/extended_status_go_proto"

	backoff "github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"github.com/pborman/uuid"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	instlopb "intrinsic/assets/proto/installation_origin_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	aigrpcpb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	icpb "intrinsic/assets/proto/v1/instance_config_go_proto"
	solutiondeploymentpb "intrinsic/assets/proto/v1/solution_deployment_go_proto"
	solutionpb "intrinsic/assets/proto/v1/solution_go_proto"
	conductorgrpcpb "intrinsic/conductor/proto/conductor_go_proto"
	conductorpb "intrinsic/conductor/proto/conductor_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	cpb "intrinsic/config/proto/cluster_go_proto"
	commonpb "intrinsic/config/proto/common_go_proto"
	datafilespb "intrinsic/config/proto/data_files_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	processpb "intrinsic/config/proto/process_go_proto"
	rspb "intrinsic/config/proto/resource_set_go_proto"
	idbcsgrpcpb "intrinsic/kubernetes/data_store/proto/cluster_service_go_proto"
	idbcspb "intrinsic/kubernetes/data_store/proto/cluster_service_go_proto"
	deploypb "intrinsic/kubernetes/workcell_spec/proto/deploy_go_proto"
	transferpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	loggerservicepb "intrinsic/logging/proto/logger_service_go_proto"
	mspb "intrinsic/modified_solution/proto/v1/modified_solution_go_proto"
	resourcecataloginternalgrpcpb "intrinsic/resources/catalog/proto/resource_catalog_internal_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	scpb "intrinsic/skills/proto/skill_registry_config_go_proto"
	srpb "intrinsic/skills/proto/skill_runtime_go_proto"
	svsgrpcpb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	svspb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	csgrpcpb "intrinsic/storage/hot_shared_state/proto/v1/cluster_service_go_proto"
	cspb "intrinsic/storage/hot_shared_state/proto/v1/cluster_service_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	"google.golang.org/protobuf/types/known/emptypb"
)

const deploymentPrefix = "deployment-v1"

var (
	newAppDeploymentID      = uuid.New
	newSolutionDeploymentID = uuid.New
	newOperationName        = uuid.New
	userEmailFromContext    = func(ctx context.Context) (string, error) {
		return "", nil
	}
)

// The DeployService can deploy applications on the cluster.
type DeployService struct {
	conductorClient               conductorgrpcpb.ConductorServiceClient
	clusterClient                 csgrpcpb.HotSharedStateClusterServiceClient
	applicationClient             asgrpcpb.HotSharedStateApplicationServiceClient
	cloudClusterClient            idbcsgrpcpb.ClusterServiceClient
	transferService               *transfersvc.TransferService
	aciClient                     acigrpcpb.AssetCatalogInternalClient
	resourceCatalogInternalClient resourcecataloginternalgrpcpb.ResourceCatalogInternalClient
	resourceTypeRuntimeClient     resourcetyperuntime.Client
	skillRuntimeClient            skillruntime.Client
	loggerClient                  loggerservicepb.DataLoggerClient
	onSolutionUpdate              func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error
	validator                     privileges.Validator
	svsc                          svsgrpcpb.SolutionVersionServiceClient
	runner                        *operations.Runner

	defaultWorkcellSpec func(context.Context) (*transferpb.WorkcellSpec, error)
	clusterInfo         transfersvc.ClusterInfo
	clusterParams       render.ClusterParams
	initDataFiles       render.InitDataFilesParams
}

// Options contains options for a deploy service.
type Options struct {
	ConductorClient               conductorgrpcpb.ConductorServiceClient
	ClusterClient                 csgrpcpb.HotSharedStateClusterServiceClient
	CloudClusterClient            idbcsgrpcpb.ClusterServiceClient
	ApplicationClient             asgrpcpb.HotSharedStateApplicationServiceClient
	AssetCatalogInternalClient    acigrpcpb.AssetCatalogInternalClient
	ResourceCatalogInternalClient resourcecataloginternalgrpcpb.ResourceCatalogInternalClient
	ResourceTypeRuntimeClient     resourcetyperuntime.Client
	SkillRuntimeClient            skillruntime.Client
	TransferService               *transfersvc.TransferService
	SolutionVersionServiceClient  svsgrpcpb.SolutionVersionServiceClient
	LoggerClient                  loggerservicepb.DataLoggerClient
	OnSolutionUpdate              func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error
	Validator                     privileges.Validator
	Runner                        *operations.Runner

	// DefaultWorkcellSpec is a closure to retrieve the default workcell spec
	// to be run with solutions. This is a function of the state of
	// intrinsic-base (i.e. its values), so this may change during the
	// operation of the deploy service.  Not all values will restart the
	// workcell cluster service.
	DefaultWorkcellSpec func(context.Context) (*transferpb.WorkcellSpec, error)
	ClusterInfo         transfersvc.ClusterInfo
	render.ClusterParams
	render.InitDataFilesParams
}

// New creates a new [DeployService].
func New(opts Options) *DeployService {
	return &DeployService{
		conductorClient:               opts.ConductorClient,
		clusterClient:                 opts.ClusterClient,
		cloudClusterClient:            opts.CloudClusterClient,
		applicationClient:             opts.ApplicationClient,
		aciClient:                     opts.AssetCatalogInternalClient,
		resourceCatalogInternalClient: opts.ResourceCatalogInternalClient,
		resourceTypeRuntimeClient:     opts.ResourceTypeRuntimeClient,
		skillRuntimeClient:            opts.SkillRuntimeClient,
		transferService:               opts.TransferService,
		clusterInfo:                   opts.ClusterInfo,
		svsc:                          opts.SolutionVersionServiceClient,
		loggerClient:                  opts.LoggerClient,
		defaultWorkcellSpec:           opts.DefaultWorkcellSpec,
		clusterParams:                 opts.ClusterParams,
		initDataFiles:                 opts.InitDataFilesParams,
		onSolutionUpdate:              opts.OnSolutionUpdate,
		validator:                     opts.Validator,
		runner:                        opts.Runner,
	}
}

// StartIdleWorkcellSpec applies the default workcell spec to the cluster in order to cache container images.
func (s *DeployService) StartIdleWorkcellSpec(ctx context.Context) error {
	ctx, span := trace.StartSpan(ctx, "StartIdleWorkcellSpec")
	defer span.End()

	// Only start the idle workcell spec if there isn't a workcell spec already
	// installed. This avoids unnecessarily updating an existing solution
	// deployment with defaults.
	if ca, err := s.transferService.CurrentExclusiveChart(ctx); err != nil {
		return fmt.Errorf("failed to get current exclusive chart: %w", err)
	} else if running, err := transfersvc.IsSolutionRunning(ca); err != nil {
		return fmt.Errorf("failed to check running state of exclusive chart: %w", err)
	} else if running {
		log.InfoContextf(ctx, "Solution deployment already exists, skipping StartIdleWorkcellSpec")
		return nil
	}

	if err := s.applyIdleWorkcellSpec(ctx); err != nil {
		return fmt.Errorf("applyIdleWorkcellSpec failed to set an idle state for the cluster: %w", err)
	}

	return nil
}

func (s *DeployService) applyIdleWorkcellSpec(ctx context.Context) error {
	defaultWorkcellSpec, err := s.defaultWorkcellSpec(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "s.defaultWorkcellSpec(ctx) failed: %v", err)
		return fmt.Errorf("failed to get default workcell spec: %w", err)
	}
	ws, err := render.WorkcellSpecFromApplication(&render.ApplicationParams{
		DefaultWorkcellSpec: defaultWorkcellSpec,
		ClusterParams:       s.clusterParams,
		InitDataFilesParams: s.initDataFiles,
		Simulated:           !s.clusterInfo.CanDoPhysicalExecution, // Unvalidated heuristic for most likely next operation mode.
	})
	if err != nil {
		log.ErrorContextf(ctx, "WorkcellSpecFromApplication(...) failed: %v", err)
		return fmt.Errorf("failed to render the solution charts: %w", err)
	}

	log.InfoContext(ctx, "Calling the transfer service to deploy intrinsic-app-chart, resources, and skills charts")
	if err := s.transferService.ApplyWorkcellSpec(ctx, ws); err != nil {
		log.ErrorContextf(ctx, "s.transferService.ApplyWorkcellSpec(ws=%v) failed: %v", ws, err)
		return fmt.Errorf("failed to apply the workcell spec: %w", err)
	}
	return nil
}

// updateApplicationInHSS updates the running application in the cluster document in the hot shared
// state service (go/intrinsic-hss).
func (s *DeployService) updateApplicationInHSS(ctx context.Context, app *apb.Application) error {
	if _, err := s.applicationClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application: app,
		Overwrite:   true,
	}); err != nil {
		return fmt.Errorf("failed to set current application: %w", err)
	}
	return nil
}

func checkOperationMode(app *apb.Application, clusterInfo transfersvc.ClusterInfo) error {
	switch app.GetOperationMode() {
	case opmodepb.OperationMode_REAL_HARDWARE:
		if !clusterInfo.CanDoPhysicalExecution {
			return fmt.Errorf("cluster %s cannot run a real solution", clusterInfo.Name)
		}
	case opmodepb.OperationMode_SIMULATION:
		if !clusterInfo.CanDoSim {
			return fmt.Errorf("%v cannot run a simulation", clusterInfo.Name)
		}
	default:
		return fmt.Errorf("cannot deploy an app with operation mode %v", app.GetOperationMode())
	}

	return nil
}

func retryBackOffExponential(ctx context.Context) backoff.BackOff {
	b := backoff.NewExponentialBackOff()
	b.InitialInterval = 1 * time.Second
	// After MaxElapsedTime the ExponentialBackOff stops.
	b.MaxElapsedTime = 30 * time.Second
	return backoff.WithContext(b, ctx)
}

func retryBackOffConstant(ctx context.Context, interval time.Duration, maxDuration time.Duration) backoff.BackOff {
	numRetries := uint64(maxDuration.Nanoseconds()/interval.Nanoseconds()) + 1
	b := backoff.WithMaxRetries(backoff.NewConstantBackOff(interval), numRetries)
	return backoff.WithContext(b, ctx)
}

func shouldRetry(e error) bool {
	switch c := status.Code(e); c {
	case codes.Unavailable, codes.ResourceExhausted, codes.DeadlineExceeded, codes.Aborted:
		return true
	}
	return false
}

// callWithRetries calls the given function and retries retriable errors such as "quota exceeded"
// with an exponential backoff.
func callWithRetries(f func() (any, error), b backoff.BackOff) (any, error) {
	var resp any
	if err := backoff.Retry(func() error {
		var err error
		resp, err = f()
		if err != nil {
			if shouldRetry(err) {
				log.Warningf("Got error: %s, retrying with backoff", err.Error())
				return err
			}
			return backoff.Permanent(err)
		}
		return nil
	}, b); err != nil {
		return nil, err
	}
	return resp, nil
}

func applySceneObjectConfig(config *icpb.InstanceConfig_SceneObjectInstanceConfig, ri *ripb.ResourceInstance) {
	ri.SceneObjectConfig = config.GetSceneObjectConfig()
}

func applyServiceConfig(config *icpb.InstanceConfig_ServiceInstanceConfig, ri *ripb.ResourceInstance) {
	ri.Configuration = config.GetServiceConfig()
	if config.GetScheduledNodeHostname() != "" {
		ri.SchedulingConfig = &ripb.ResourceInstance_SchedulingConfig{
			RequiredNodeHostname: config.GetScheduledNodeHostname(),
		}
	}
	// TODO(b/409140939): Remove.
	if len(config.GetDataFiles()) > 0 {
		ri.DataFiles = &datafilespb.DataFiles{
			Files: config.GetDataFiles(),
		}
	}
}

func applyHardwareDeviceConfig(config *icpb.InstanceConfig_HardwareDeviceInstanceConfig, ri *ripb.ResourceInstance) {
	applySceneObjectConfig(config.GetSceneObject(), ri)
	applyServiceConfig(config.GetService(), ri)
}

func convertResourceInstance(name string, instance *apb.Application_Instance, rtr *rtrpb.ResourceTypeRuntime) (*ripb.ResourceInstance, error) {
	ri := &ripb.ResourceInstance{
		Name:          name,
		TypeIdVersion: idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()),
	}

	switch v := instance.GetConfig().GetVariant().(type) {
	case *icpb.InstanceConfig_SceneObject:
		applySceneObjectConfig(v.SceneObject, ri)
	case *icpb.InstanceConfig_Service:
		applyServiceConfig(v.Service, ri)
	case *icpb.InstanceConfig_HardwareDevice:
		applyHardwareDeviceConfig(v.HardwareDevice, ri)
	case nil:
	default:
		return nil, status.Errorf(codes.InvalidArgument, "instance %q has unknown config variant: %v", name, instance.GetConfig().GetVariant())
	}

	return ri, nil
}

func getID(asset *apb.Application_Asset) *idpb.Id {
	switch asset.Variant.(type) {
	case *apb.Application_Asset_Catalog:
		return asset.GetCatalog().GetId()
	case *apb.Application_Asset_Skill:
		return asset.GetSkill().GetMetadata().GetId()
	case *apb.Application_Asset_Service:
		return asset.GetService().GetMetadata().GetId()
	case *apb.Application_Asset_Data:
		return asset.GetData().GetMetadata().GetIdVersion().GetId()
	case *apb.Application_Asset_SceneObject:
		return asset.GetSceneObject().GetMetadata().GetId()
	case *apb.Application_Asset_HardwareDevice:
		return asset.GetHardwareDevice().GetMetadata().GetId()
	case *apb.Application_Asset_Process:
		return asset.GetProcess().GetMetadata().GetIdVersion().GetId()
	default:
		return nil
	}
}

func (s *DeployService) getClusterNoApp(ctx context.Context) (*cpb.Cluster, string, error) {
	// Retry if HotSharedStateClusterService is not available yet. This is not uncommon when running
	// intrinsic integration tests.
	clusterResAny, err := callWithRetries(func() (any, error) {
		return s.clusterClient.GetCurrentCluster(ctx, &cspb.GetCurrentClusterRequest{
			// checkOperationMode does not need the application, only the cluster metadata. This can save
			// megabytes of transfer for large applications.
			View: cspb.ClusterView_CLUSTER_VIEW_NO_APPLICATION,
		})
	}, retryBackOffExponential(ctx))
	if err != nil {
		return nil, "", fmt.Errorf("get current cluster: %v", err)
	}

	resp := clusterResAny.(*cspb.GetCurrentClusterResponse)
	return resp.GetCluster(), resp.GetRevisionToken(), nil
}

// writeClusterDocToFirestore reads the current cluster from the onprem volume and
// uploads it to Firestore under the corresponding name.
func (s *DeployService) writeClusterDocToFirestore(ctx context.Context, cluster *cpb.Cluster, app *apb.Application) error {
	clusterForBackup := proto.Clone(cluster).(*cpb.Cluster)
	// Strip application since it can be too large for a backup.
	clusterForBackup.Application = applicationview.MetadataOnly(app)
	// TODO: ensonic - wrap this in retries with backoff and hardfail if it does not succeeds.
	if err := s.updateAppMetadataInFirestore(ctx, clusterForBackup); err != nil {
		return fmt.Errorf("update app metadata in Firestore failed: %w", err)
	}
	return nil
}

func (s *DeployService) updateAppMetadataInFirestore(ctx context.Context, clusterForBackup *cpb.Cluster) error {
	ctx, err := clientcontext.ToContextFromIncoming(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "updateAppMetadataInFirestore: adding identity information from incoming context to outgoing context failed: %v", err)
		return clientcontext.ErrGRPC(err)
	}
	res, err := s.cloudClusterClient.GetCluster(ctx, &idbcspb.GetClusterRequest{Name: s.clusterInfo.Name})
	if c := status.Code(err); c == codes.NotFound {
		// Very improbable case that the cluster is not available in Firestore. Then use the cluster
		// proto from HSS "as is".
		if _, err := s.cloudClusterClient.SetCluster(ctx, &idbcspb.SetClusterRequest{
			Cluster:   clusterForBackup,
			Overwrite: true,
		}); err != nil {
			return fmt.Errorf("cannot overwrite the cluster document in Firestore: %w", err)
		}
		return nil
	} else if c != codes.OK {
		return fmt.Errorf("cannot obtain the cluster document from Firestore: %w", err)
	}

	// If there already is a cluster proto in Firestore, don't overwrite it completely, update only
	// the application within it.
	cluster := res.GetCluster()
	cluster.Application = clusterForBackup.GetApplication()
	if _, err := s.cloudClusterClient.SetCluster(ctx, &idbcspb.SetClusterRequest{
		Cluster:       cluster,
		RevisionToken: res.GetRevisionToken(),
	}); err != nil {
		return fmt.Errorf("cannot update the cluster document in Firestore: %w", err)
	}
	return nil
}

func (s *DeployService) createModifiedSolutionFromBranchEnabledSolutionID(ctx context.Context, branchName string) (*mspb.ModifiedSolution, error) {
	ms, err := s.modifiedSolutionForBranch(ctx, branchName)
	switch status.Code(err) {
	case codes.OK:
		log.InfoContextf(ctx, "Retrieved modified solution for branch %q", branchName)
		return ms, nil
	case codes.PermissionDenied, codes.Unauthenticated, codes.NotFound:
		// Return auth and not found codes as-is.
		log.ErrorContextf(ctx, "Retrieving modified solution for branch %q failed, returning error as is: %v", branchName, err)
		return nil, err
	default:
		// Wrap all other errors.
		log.ErrorContextf(ctx, "Retrieving modified solution for branch %q failed, returning internal error: %v", branchName, err)
		return nil, status.Errorf(codes.Internal, "failed to get modified solution from branch %v", err)
	}
}

func (s *DeployService) DeployApplication(ctx context.Context, req *deploypb.DeployApplicationRequest) (*deploypb.DeployApplicationResponse, error) {
	// AlwaysSample as deployments are infrequent and often slow.
	ctx, span := trace.StartSpan(telemetry.WithAlwaysSample(ctx), "DeployApplication")
	defer span.End()

	span.AddAttributes(
		trace.StringAttribute("name", req.GetApplication().GetMetadata().GetDisplayName()),
		trace.StringAttribute("cluster", s.clusterInfo.Name),
	)

	if branchName := req.GetApplication().GetMetadata().GetName(); branchName != "" {
		op, err := s.scheduleVersionedSolution(ctx, branchName, req.GetApplication().GetOperationMode(), true /* allowRunning */, solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_UNSPECIFIED)
		if err != nil {
			return nil, err
		}
		op.Wait()
		if err := status.ErrorProto(op.Proto().GetError()); err != nil {
			return nil, err
		}
		return &deploypb.DeployApplicationResponse{}, nil
	}

	log.InfoContext(ctx, "Solution is not a branch.  No save data will be retrieved.")
	var validateDependencies deployValidator = func(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) error {
		_, err := s.validateDependencies(ctx, app, rts)
		if err != nil {
			// TODO(b/536078799): As a temporary measure, we only log the errors here instead of fatally
			// returning them. Once we are reasonably confident that this won't cause breakages for
			// existing users, we should change this to return the error here.
			log.ErrorContextf(ctx, "dependency validation failed in DeployApplication: %v", err)
		}
		return nil
	}

	if req.GetApplication() == nil {
		req.Application = &apb.Application{}
	}
	if req.GetApplication().GetMetadata() == nil {
		req.GetApplication().Metadata = &commonpb.Metadata{}
	}
	req.GetApplication().Metadata.SolutionDeploymentId = newSolutionDeploymentID()

	if _, err := s.deployApplication(ctx, req.GetApplication(), nil, nil, deployOpts{validate: validateDependencies}); err != nil {
		log.ErrorContextf(ctx, "deployApplication failed: %v", err)
		return nil, err
	}
	return &deploypb.DeployApplicationResponse{}, nil
}

type deployValidator func(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) error

type deployOpts struct {
	// validate is a callback to run validation during solution deployment.
	validate deployValidator
}

func (s *DeployService) deployApplication(
	ctx context.Context,
	app *apb.Application,
	sideloadedResourceTypes []*rtrpb.ResourceTypeRuntime,
	sideloadedPBTs []*mspb.ModifiedSolution_SideloadedParameterizableBehaviorTree,
	opts deployOpts,
) (*solutiondeploymentpb.SolutionDeployment, error) {
	ctx, span := trace.StartSpan(ctx, "DeployService.deployApplication")
	defer span.End()

	if cat := app.GetMetadata().GetCategory(); cat != commonpb.Metadata_INSTANCE && cat != commonpb.Metadata_BRANCH {
		log.ErrorContextf(ctx, "invalid app category of %v", cat)
		return nil, status.Errorf(codes.Internal, "request to deploy an application of category %q, want INSTANCE or BRANCH", cat)
	}
	if app.GetMetadata().GetSolutionDeploymentId() == "" {
		log.ErrorContext(ctx, "missing solution deployment ID")
		return nil, status.Error(codes.Internal, "request to deploy an application without a solution deployment ID")
	}

	span.AddAttributes(
		trace.StringAttribute("name", app.GetMetadata().GetDisplayName()),
		trace.StringAttribute("cluster", s.clusterInfo.Name))

	if err := checkOperationMode(app, s.clusterInfo); err != nil {
		log.ErrorContextf(ctx, "checkOperationMode(%v, %v) failed with: %v", app, s.clusterInfo, err)
		return nil, status.Errorf(codes.FailedPrecondition, "solution %q: %v", app.GetMetadata().GetDisplayName(), err)
	}

	cluster, _, err := s.getClusterNoApp(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "get cluster: %v", err)
	}

	log.InfoContext(ctx, "Gathering asset data from asset and resource catalogs")
	rts, err := runtimedbtransfer.GatherResourceTypes(ctx, app, sideloadedResourceTypes, runtimedbtransfer.GatherResourceTypesOpts{
		ACIClient: s.aciClient,
		RCIClient: s.resourceCatalogInternalClient,
		Validator: s.validator,
	})
	if err != nil {
		// We pass the error through to preserve gRPC status codes. This causes
		// unauthenticated errors to show a hint to run 'inctl auth login'.
		return nil, err
	}

	log.InfoContext(ctx, "Normalizing the application proto")
	if err := normalizeApp(ctx, app, rts); err != nil {
		log.ErrorContextf(ctx, "normalizeApp failed: %v", err)
		return nil, err
	}

	// After normalizeApp, all assets are in the resource set.
	for _, ri := range app.GetResources().GetResourceInstances() {
		idVersion, err := idutils.IDOrIDVersionProtoFrom(ri.GetTypeIdVersion())
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "application contains an asset instance %q with an invalid ID version: %v", ri.GetName(), err)
		}
		// Ensure the Asset ID is not reserved.
		if err := platform.ValidateIDNotReserved(idVersion.GetId()); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "instance %q has a reserved ID: %v", ri.GetName(), err)
		}
		// Ensure the Asset instance name is not reserved.
		if err := platform.ValidateInstanceNameNotReserved(ri.GetName()); err != nil {
			return nil, err
		}
	}

	if opts.validate != nil {
		if err := opts.validate(ctx, app, rts); err != nil {
			return nil, err
		}
	}

	log.InfoContext(ctx, "Getting the default workcell spec")
	defaultWorkcellSpec, err := s.defaultWorkcellSpec(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "Failed to get default workcell spec: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to get default workcell spec: %v", err)
	}

	log.InfoContext(ctx, "Rendering the workcell spec")
	simulated, err := render.IsSimulated(app)
	if err != nil {
		log.ErrorContextf(ctx, "render.IsSimulated failed: %v", err)
		return nil, status.Errorf(codes.Internal, "render workcell spec: %v", err)
	}
	appDeploymentID := newAppDeploymentID()
	ws, err := render.WorkcellSpecFromApplication(&render.ApplicationParams{
		DefaultWorkcellSpec:     defaultWorkcellSpec,
		ResourceInstances:       app.GetResources().GetResourceInstances(),
		ResourceTypes:           rts,
		SkillDeploymentRuntimes: slices.Collect(xiter.Filter(pointer.NotNil, xiter.Map(render.ResourceTypeRuntimeToSkillDeploymentData, maps.Values(rts)))),
		AppDeploymentID:         appDeploymentID,
		SolutionDeploymentID:    app.GetMetadata().GetSolutionDeploymentId(),
		Simulated:               simulated,
		ClusterParams:           s.clusterParams,
		InitDataFilesParams:     s.initDataFiles,
	})
	if err != nil {
		log.ErrorContextf(ctx, "WorkcellSpecFromApplication(...) failed: %v", err)
		return nil, status.Errorf(codes.Internal, "render workcell spec: %v", err)
	}

	log.InfoContext(ctx, "Writing cluster doc to firestore")
	// strip app proto, store it in cluster proto and write to firestore
	if err := s.writeClusterDocToFirestore(ctx, cluster, app); err != nil {
		log.WarningContextf(ctx, "Failed to write cluster doc to firestore: %v", err)
	}

	log.InfoContext(ctx, "Populating runtimedb for pBTs")
	// pBTs are only stored in the modified solution currently.  None have been
	// released to the asset catalog, so just pass that in here.
	if err := runtimedbtransfer.PopulateSkillRuntimesForPBTBasedSkills(ctx, asSkillRuntimes(sideloadedPBTs), s.skillRuntimeClient); err != nil {
		log.ErrorContextf(ctx, "PopulateSkillRuntimesForPBTBasedSkills(...) failed: %v", err)
		return nil, status.Errorf(codes.Internal, "populate skill runtime db: %v", err)
	}
	log.InfoContext(ctx, "Populating runtimedb for assets")
	if err = runtimedbtransfer.PopulateResourceRuntimeTypes(ctx, rts, s.resourceTypeRuntimeClient); err != nil {
		log.ErrorContextf(ctx, "PopulateResourceRuntimeTypes(ctx, %v, runtimeClient) failed: %v", rts, err)
		return nil, status.Errorf(codes.Internal, "populate resource runtime db: %v", err)
	}

	log.InfoContext(ctx, "Setting the application in HSS")
	if err := s.updateApplicationInHSS(ctx, app); err != nil {
		log.ErrorContextf(ctx, "Failed to update the application in HSS: %v", err)
		return nil, status.Errorf(codes.Internal, "unable to save proposed solution state: %v", err)
	}

	log.InfoContext(ctx, "Notifying solution update")
	if err := s.onSolutionUpdate(ctx, app, slices.Collect(maps.Values(rts))); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed during deploy: %v", err)
	}

	log.InfoContext(ctx, "Starting the solution on the conductor")
	// Start the solution waiting for services to be available every five seconds for two minutes.
	if _, err := callWithRetries(func() (any, error) {
		// identity data is needed downstream of this call, so we copy it from the incoming context.
		ctxWithAuth, err := clientcontext.ToContextFromIncoming(ctx)
		if err != nil {
			log.ErrorContextf(ctx, "DeployApplication: adding identity information from incoming context to outgoing context failed: %v", err)
			return nil, clientcontext.ErrGRPC(err)
		}
		return s.conductorClient.StartSolution(ctxWithAuth, &conductorpb.StartSolutionRequest{
			OperationMode:           app.GetOperationMode(),
			SkipInvalidWorldUpdates: app.GetMetadata().GetName() != "",
		})
	}, retryBackOffConstant(ctx, 5*time.Second, 2*time.Minute)); err != nil {
		log.ErrorContextf(ctx, "s.conductorClient.StartSolution(ctxWithAuth, %v) failed: %v", app.GetOperationMode(), err)
		return nil, status.Errorf(codes.Internal, "conductor failed to start solution: %v", err)
	}

	log.InfoContext(ctx, "Calling the transfer service to deploy intrinsic-app-chart, resources, and skills charts")
	if err := s.transferService.ApplyWorkcellSpec(ctx, ws); err != nil {
		log.ErrorContextf(ctx, "s.transferService.ApplyWorkcellSpec(ws=%v) failed: %v", ws, err)
		return nil, status.Errorf(codes.Internal, "failed to create and apply workcell spec: %v", err)
	}

	log.InfoContextf(ctx, "DeployApplication done")
	solution, err := asSolution(app, rts)
	if err != nil {
		log.ErrorContextf(ctx, "asSolution(app, rts) failed: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to convert local state to a solution: %v", err)
	}
	return &solutiondeploymentpb.SolutionDeployment{
		Name:          app.GetMetadata().GetSolutionDeploymentId(),
		DisplayName:   app.GetMetadata().GetDisplayName(),
		SolutionId:    app.GetMetadata().GetName(),
		OperationMode: app.GetOperationMode(),
		Solution:      solution,
	}, nil
}

func (s *DeployService) StopApplication(ctx context.Context, req *deploypb.StopApplicationRequest) (*deploypb.StopApplicationResponse, error) {
	log.InfoContextf(ctx, "StopApplication(): called with wait=%v", req.GetWait())

	if err := s.stopSolution(ctx); err != nil {
		return nil, fmt.Errorf("StopApplication(): stopSolution() failed: %w", err)
	}

	return &deploypb.StopApplicationResponse{}, nil
}

func (s *DeployService) stopSolution(ctx context.Context) error {
	log.InfoContextf(ctx, "Getting current cluster state")
	cluster, revisionToken, err := s.getClusterNoApp(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "failed to get the cluster from HSS: %v", err)
		return fmt.Errorf("failed to get the current solution state: %w", err)
	}

	// is_incomplete was set by HSS because we requested a view (NO_APPLICATION).
	// Our goal is to clear the application here, so we clear is_incomplete so
	// HSS will accept cluster back as a message.
	cluster.Metadata.IsIncomplete = false
	log.InfoContextf(ctx, "Clearing the application in HSS")
	if _, err := s.clusterClient.SetCurrentCluster(ctx, &cspb.SetCurrentClusterRequest{
		Cluster:       cluster,
		RevisionToken: revisionToken,
	}); err != nil {
		log.ErrorContextf(ctx, "failed to clear the application document in HSS: %v", err)
		return fmt.Errorf("failed to cluster the solution state: %w", err)
	}

	if err := s.applyIdleWorkcellSpec(ctx); err != nil {
		log.ErrorContextf(ctx, "s.applyIdleWorkcellSpec(ctx) failed: %v", err)
		return status.Errorf(codes.Internal, "failed to move charts back to an idle state: %v", err)
	}

	if err := s.resourceTypeRuntimeClient.Clear(ctx); err != nil {
		log.ErrorContextf(ctx, "s.resourceTypeRuntimeClient.Clear(ctx) failed: %v", err)
		return fmt.Errorf("clearing installed assets failed: %w", err)
	}

	if err := s.skillRuntimeClient.Clear(ctx); err != nil {
		log.ErrorContextf(ctx, "s.skillRuntimeClient.Clear(ctx) failed: %v", err)
		return fmt.Errorf("clearing installed behavior trees failed: %w", err)
	}

	log.InfoContextf(ctx, "Stopping the solution in the conductor")
	if _, err := s.conductorClient.StopSolution(ctx, &conductorpb.StopSolutionRequest{}); err != nil {
		log.ErrorContextf(ctx, "conductor failed to stop solution: %v", err)
		return status.Errorf(codes.Internal, "conductor failed to stop solution: %v", err)
	}

	if err := s.onSolutionUpdate(ctx, nil, nil); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed during stop: %v", err)
	}

	return nil
}

// modifiedSolutionForBranch uses the solution version service to get the modified solution from the
// given branch. If no modified solution is found, it returns an error.
func (s *DeployService) modifiedSolutionForBranch(ctx context.Context, branchID string) (*mspb.ModifiedSolution, error) {
	ctxWithAuth, err := clientcontext.ToContextFromIncoming(ctx)
	if err != nil {
		log.ErrorContextf(ctx, "modifiedSolutionForBranch: adding identity information from incoming context to outgoing context failed: %v", err)
		return nil, clientcontext.ErrGRPC(err)
	}
	dd, err := s.svsc.GetDeploymentData(ctxWithAuth, &svspb.GetDeploymentDataRequest{
		BranchId: branchID,
	})
	if err != nil {
		return nil, err
	}
	return dd.GetModifiedSolution(), nil
}

// asSkillRuntimes converts sideloaded parameterizable behavior trees to skill runtimes.
func asSkillRuntimes(sideloadedPBTs []*mspb.ModifiedSolution_SideloadedParameterizableBehaviorTree) []*srpb.SkillRuntime {
	var srs []*srpb.SkillRuntime
	for _, sideloadedPBT := range sideloadedPBTs {
		srs = append(srs, &srpb.SkillRuntime{
			IdVersion: sideloadedPBT.GetBehaviorTree().GetDescription().GetIdVersion(),
			Registration: &scpb.BehaviorTreeRegistration{
				BehaviorTree: sideloadedPBT.GetBehaviorTree(),
			},
		})
	}
	return srs
}

// normalizeApp combines the different approaches we have to storing the
// solution definition into the canonical one the rest of the system expects.
// Currently this removes the assets and instances fields from the input, and
// combines them with any existing resource set.  The provided app can be
// mutated, but this function is idempotent.  The resulting app always has a
// resource set.  Each resource instance always has a configuration.
func normalizeApp(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) error {
	rtsByID := map[string]*rtrpb.ResourceTypeRuntime{}
	for _, rt := range rts {
		id := idutils.IDFromProtoUnchecked(rt.GetMetadata().GetIdVersion().GetId())
		rtsByID[id] = rt
	}

	// At this point we need to transfer the asset instances from the newer
	// application instance map to the resource instances in the resource set.
	// We will clear the instance map afterwards. We eventually plan to retire
	// the resource set but we will first have to update all users of the
	// resource set to work with the newer instance map.
	resourceInstances := app.GetResources().GetResourceInstances()
	names := make(map[string]struct{})
	for _, ri := range resourceInstances {
		name := ri.GetName()
		if _, exists := names[name]; exists {
			log.ErrorContextf(ctx, "Multiple asset instances named %q", name)
			return status.Errorf(codes.InvalidArgument, "multiple asset instances named %q", name)
		}
		names[name] = struct{}{}
	}

	for name, i := range app.GetInstances() {
		if _, exists := names[name]; exists {
			log.ErrorContextf(ctx, "Multiple asset instances named %q", name)
			return status.Errorf(codes.InvalidArgument, "multiple asset instances named %q", name)
		}
		names[name] = struct{}{}

		var idProto *idpb.Id
		if asset, ok := app.Assets[i.GetAsset()]; ok {
			idProto = getID(asset)
		} else {
			log.ErrorContextf(ctx, "Instance %q references unknown asset %q", name, i.GetAsset())
			return status.Errorf(codes.InvalidArgument, "instance %q references unknown asset %q", name, i.GetAsset())
		}
		id, err := idutils.IDFromProto(idProto)
		if err != nil {
			log.ErrorContextf(ctx, "Application contains an asset instance with invalid ID: %v", err)
			return status.Errorf(codes.InvalidArgument, "application contains an asset instance with invalid ID: %v", err)
		}
		// The lookup here is by id, rather than by name, which means we've
		// already enforced a single-version constraint.  This will likely need
		// to change if the modified solution service begins to backfill the
		// assets map and clearing sideloaded_resource_types.
		rt, exists := rtsByID[id]
		if !exists {
			log.ErrorContextf(ctx, "Missing runtime data for instance %q, which uses asset %q", name, id)
			return status.Errorf(codes.Internal, "cannot find asset runtime data for an asset instance")
		}

		if ri, err := convertResourceInstance(name, i, rt); err != nil {
			return err
		} else {
			resourceInstances = append(resourceInstances, ri)
		}
	}

	// Ensure that all ResourceInstances have explicitly specified configurations, using the default
	// if the instance does not specify one explicitly.
	//
	// This step is needed for instances coming from three difference sources:
	// - ResourceInstances that were converted above in convertResourceInstance and for which the
	//   source Instance did not specify a configuration.
	// - Modified solutions that were saved before this step was added.
	// - Applications that were released before we stopped populating the ResourceSet's instances when
	//   building the application target.
	//
	// This step won't be needed for the last two sources once those stored solutions are no longer
	// supported. At that point, we only need this step for the first case, and we could potentially
	// move this logic into convertResourceInstance (and maybe just validate here that all instances
	// have configurations).
	for _, ri := range resourceInstances {
		id, err := idutils.RemoveVersionFrom(ri.GetTypeIdVersion())
		if err != nil {
			log.ErrorContextf(ctx, "Invalid type_id_version: %v", err)
			return status.Errorf(codes.InvalidArgument, "invalid type_id_version: %v", err)
		}
		rt, exists := rtsByID[id]
		if !exists {
			return status.Errorf(codes.Internal, "cannot find resource type %q", id)
		}
		if ri.GetConfiguration() == nil {
			ri.Configuration = rt.GetDefaultConfiguration()
		}
		if ri.GetSceneObjectConfig() == nil {
			ri.SceneObjectConfig = rt.GetDefaultSceneObjectConfig()
		}
	}

	resources := &rspb.ResourceSet{
		ResourceInstances:  resourceInstances,
		ObjectWorldUpdates: app.GetResources().GetObjectWorldUpdates(),
	}
	if len(app.GetObjectWorldUpdates()) > 0 {
		resources.ObjectWorldUpdates = &owupb.ObjectWorldUpdates{
			Updates: app.GetObjectWorldUpdates(),
		}
	}

	// Set the catalog skills to what was gathered to be installed.
	var skillIDVersions []string
	for _, rt := range rts {
		if rt.GetInstallationOrigin() == instlopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG && rt.GetSkill() != nil {
			skillIDVersions = append(skillIDVersions, idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion()))
		}
	}
	var skills *apb.Application_Skills
	if len(skillIDVersions) > 0 {
		skills = &apb.Application_Skills{
			SkillIdVersions: skillIDVersions,
		}
	}

	catalogAssets := make(map[string]*apb.Application_Asset)
	for id, rtr := range rtsByID {
		if rtr.GetInstallationOrigin() == instlopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG {
			catalogAssets[id] = &apb.Application_Asset{
				Variant: &apb.Application_Asset_Catalog{
					Catalog: rtr.GetMetadata().GetIdVersion(),
				},
			}
		}
	}

	for id, rtr := range rtsByID {
		if err := descriptorcompatibility.Reconcile(rtr.GetMetadata().GetFileDescriptorSet()); err != nil {
			return status.Errorf(codes.Internal, "unable to reconcile the file descriptor set for asset %q: %v", id, err)
		}
	}

	process := app.GetProcess()
	if process != nil {
		if err := convertrunnables.MoveBytesToRunnables(process); err != nil {
			log.ErrorContextf(ctx, "convertrunnables.MoveBytesToRunnables failed: %v", err)
			return status.Errorf(codes.Internal, "cannot convert bytes to runnables: %v", err)
		}
	} else {
		process = &processpb.Process{}
	}

	// Now mutate the application.
	app.Process = process
	app.Resources = resources
	app.Skills = skills
	// We only add catalog asset IDs to the assets field for now. This way, installed assets that
	// do not have instances, can still be represented in the application.
	// Sideloaded assets are represented outside the application proto, in the modified solution.
	app.Assets = catalogAssets
	app.Instances = nil
	app.ObjectWorldUpdates = nil
	return nil
}

func (s *DeployService) CreateSolutionDeploymentFromVersionedSolution(ctx context.Context, req *solutiondeploymentpb.CreateSolutionDeploymentFromVersionedSolutionRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "DeployService.CreateSolutionDeploymentFromVersionedSolution")
	defer span.End()
	if view, err := viewOrDefault(req.GetView(), solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_BASIC); err != nil {
		return nil, err
	} else {
		req.View = view
	}
	op, err := s.scheduleVersionedSolution(ctx, req.GetSolutionId(), req.GetOperationMode(), false /* allowRunning */, req.GetView())
	if err != nil {
		return nil, err
	}
	return op.Proto(), nil
}

func (s *DeployService) scheduleVersionedSolution(ctx context.Context, solutionID string, operationMode opmodepb.OperationMode, allowRunning bool, view solutiondeploymentpb.SolutionDeploymentView) (*operations.Operation, error) {
	op := operations.New(&lropb.Operation{
		Name: path.Join(deploymentPrefix, "create-from-versioned-solution", newOperationName()),
	})
	if err := op.SetMetadata(&solutiondeploymentpb.CreateSolutionDeploymentFromVersionedSolutionMetadata{}); err != nil {
		return nil, err
	}

	if err := s.runner.Schedule(ctx, op, func(ctx context.Context) (proto.Message, error) {
		ms, err := s.createModifiedSolutionFromBranchEnabledSolutionID(ctx, solutionID)
		if err != nil {
			return nil, err
		}

		if ms.GetApplication() == nil {
			return nil, status.Errorf(codes.FailedPrecondition, "no application message found in modified solution")
		}

		ctxWithAuth, err := clientcontext.ToContextFromIncoming(ctx)
		if err != nil {
			log.ErrorContextf(ctx, "CreateSolutionDeploymentFromVersionedSolution: adding identity information from incoming context to outgoing context failed: %v", err)
			return nil, clientcontext.ErrGRPC(err)
		}

		branch, err := s.svsc.GetBranch(ctxWithAuth, &svspb.GetBranchRequest{
			BranchId: solutionID,
		})
		if err != nil {
			log.ErrorContextf(ctx, "CreateSolutionDeploymentFromVersionedSolution: failed to get branch %q: %v", solutionID, err)
			return nil, err
		}
		if !allowRunning && branch.GetExecStatus().GetOperationMode() != opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED {
			log.ErrorContextf(ctx, "CreateSolutionDeploymentFromVersionedSolution: solution %q is currently running", solutionID)
			return nil, status.Errorf(codes.FailedPrecondition, "solution %q is currently running", solutionID)
		}

		app := applicationview.WithMetadataFrom(ms.GetApplication(), &apb.Application{
			Metadata: &commonpb.Metadata{
				Name:        solutionID,
				Category:    commonpb.Metadata_BRANCH,
				DisplayName: branch.GetDisplayName(),
			},
			OperationMode: operationMode,
		})
		app.Metadata.SolutionDeploymentId = newSolutionDeploymentID()

		var validateDependencies deployValidator = func(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) error {
			es, err := s.validateDependencies(ctx, app, rts)
			if err != nil {
				// TODO(b/536078799): As a temporary measure, we only log the errors here instead of fatally
				// returning them. Once we are reasonably confident that this won't cause breakages for
				// existing users, we should change this to return the error here.
				log.ErrorContextf(ctx, "dependency validation failed in CreateSolutionDeploymentFromVersionedSolution: %v", err)
			} else if es != nil {
				metadata := &solutiondeploymentpb.CreateSolutionDeploymentFromVersionedSolutionMetadata{
					Warnings: es,
				}
				if err := op.SetMetadata(metadata); err != nil {
					log.WarningContextf(ctx, "failed to set operation metadata: %v", err)
				}
			}
			return nil
		}
		solutionDeployment, err := s.deployApplication(ctx, app, ms.GetSideloadedResourceTypes(), ms.GetSideloadedParameterizableBehaviorTrees(), deployOpts{
			validate: validateDependencies,
		})
		if err != nil {
			return nil, err
		}
		return asView(solutionDeployment, view), nil
	}); err == operations.ErrQueueFull {
		return nil, status.Error(codes.ResourceExhausted, err.Error())
	} else if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op, nil
}

func (s *DeployService) DeleteSolutionDeployment(ctx context.Context, req *solutiondeploymentpb.DeleteSolutionDeploymentRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "DeployService.DeleteSolutionDeployment")
	defer span.End()
	op, err := s.scheduleDeleteSolutionDeployment(ctx)
	if err != nil {
		return nil, err
	}
	return op.Proto(), nil
}

func (s *DeployService) scheduleDeleteSolutionDeployment(ctx context.Context) (*operations.Operation, error) {
	op := operations.New(&lropb.Operation{
		Name: path.Join(deploymentPrefix, "delete", newOperationName()),
	})
	if err := op.SetMetadata(&solutiondeploymentpb.DeleteSolutionDeploymentMetadata{}); err != nil {
		return nil, err
	}

	if err := s.runner.Schedule(ctx, op, func(ctx context.Context) (proto.Message, error) {
		if err := s.stopSolution(ctx); err != nil {
			return nil, err
		}
		return &emptypb.Empty{}, nil
	}); err == operations.ErrQueueFull {
		return nil, status.Error(codes.ResourceExhausted, err.Error())
	} else if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op, nil
}

func (s *DeployService) UpdateSolutionDeployment(ctx context.Context, req *solutiondeploymentpb.UpdateSolutionDeploymentRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "DeployService.UpdateSolutionDeployment")
	defer span.End()

	if view, err := viewOrDefault(req.GetView(), solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_BASIC); err != nil {
		return nil, err
	} else {
		req.View = view
	}

	op := operations.New(&lropb.Operation{
		Name: path.Join(deploymentPrefix, "update", newOperationName()),
	})
	if err := op.SetMetadata(&solutiondeploymentpb.UpdateSolutionDeploymentMetadata{}); err != nil {
		return nil, err
	}

	if err := s.runner.Schedule(ctx, op, func(ctx context.Context) (proto.Message, error) {
		var solutionDeploymentID string
		var appRunning bool
		// Right now we only use the response here to get the solution
		// deployment id.  We could potentially use more of the response,
		// particularly the revision token as a way of using the optimistic
		// concurrency of HSS to avoid simultaneous calls mutating state in
		// different ways.  This check runs within the scheduled operation to
		// avoid operating on stale information.
		if currApp, err := s.applicationClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{}); status.Code(err) == codes.NotFound {
			if !req.GetAllowMissing() {
				return nil, status.Error(codes.NotFound, "no solution deployment is running and allow_missing was not specified")
			}
		} else if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to get current application: %v", err)
		} else {
			appRunning = true
			solutionDeploymentID = currApp.GetApplication().GetMetadata().GetSolutionDeploymentId()
		}
		if reqName := req.GetSolutionDeployment().GetName(); reqName != "" && reqName != solutionDeploymentID {
			if !appRunning {
				return nil, status.Errorf(codes.FailedPrecondition, "solution deployment name %q specified but no solution deployment is running", reqName)
			}
			return nil, status.Errorf(codes.FailedPrecondition, "solution deployment name %q does not match current solution deployment %q", reqName, solutionDeploymentID)
		}
		if solutionDeploymentID == "" {
			solutionDeploymentID = newSolutionDeploymentID()
		}

		app, err := asApplication(req.GetSolutionDeployment().GetSolution())
		if err != nil {
			log.ErrorContextf(ctx, "failed to convert solution to application: %v", err)
			return nil, status.Errorf(codes.InvalidArgument, "failed to convert solution to application: %v", err)
		}

		if email, err := userEmailFromContext(ctx); err != nil {
			log.WarningContextf(ctx, "failed to get user from context: %v", err)
		} else if email != "" {
			app.Metadata.LastUpdatedBy = email
		}
		if solutionID := req.GetSolutionDeployment().GetSolutionId(); solutionID != "" {
			app.Metadata.Category = commonpb.Metadata_BRANCH
			app.Metadata.Name = solutionID
		}
		app.Metadata.SolutionDeploymentId = solutionDeploymentID
		app.Metadata.DisplayName = req.GetSolutionDeployment().GetDisplayName()
		app.OperationMode = req.GetSolutionDeployment().GetOperationMode()

		var validateDependencies deployValidator = func(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) error {
			es, err := s.validateDependencies(ctx, app, rts)
			if err != nil {
				// TODO(b/536078799): As a temporary measure, we only log the errors here instead of fatally
				// returning them. Once we are reasonably confident that this won't cause breakages for
				// existing users, we should change this to return the error here.
				log.ErrorContextf(ctx, "dependency validation failed in UpdateSolutionDeployment: %v", err)
			} else if es != nil {
				metadata := &solutiondeploymentpb.UpdateSolutionDeploymentMetadata{
					Warnings: es,
				}
				if err := op.SetMetadata(metadata); err != nil {
					log.WarningContextf(ctx, "failed to set operation metadata: %v", err)
				}
			}
			return nil
		}
		solutionDeployment, err := s.deployApplication(ctx, app, nil, nil, deployOpts{
			validate: validateDependencies,
		})
		if err != nil {
			return nil, err
		}

		return asView(solutionDeployment, req.GetView()), nil
	}); err == operations.ErrQueueFull {
		log.ErrorContextf(ctx, "queue full: %v", err)
		return nil, status.Error(codes.ResourceExhausted, err.Error())
	} else if err != nil {
		log.ErrorContextf(ctx, "unable to enqueue operation: %v", err)
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}

	return op.Proto(), nil
}

func (s *DeployService) GetSolutionDeployment(ctx context.Context, req *solutiondeploymentpb.GetSolutionDeploymentRequest) (*solutiondeploymentpb.SolutionDeployment, error) {
	ctx, span := trace.StartSpan(ctx, "DeployService.GetSolutionDeployment")
	defer span.End()

	if view, err := viewOrDefault(req.GetView(), solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_FULL); err != nil {
		return nil, err
	} else {
		req.View = view
	}

	resp, err := s.applicationClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if status.Code(err) == codes.NotFound {
		log.InfoContextf(ctx, "Get called with no active solution deployment: %v", err)
		return nil, status.Error(codes.NotFound, "no active solution deployment")
	} else if err != nil {
		log.ErrorContextf(ctx, "Failed to get current application: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to retreive the current solution: %v", err)
	}

	app := resp.GetApplication()
	rts, err := runtimedbtransfer.Retrieve(ctx, s.resourceTypeRuntimeClient, app)
	if err != nil {
		log.ErrorContextf(ctx, "Failed to get application: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to retrieve asset information: %v", err)
	}

	solution, err := asSolution(app, rts)
	if err != nil {
		log.ErrorContextf(ctx, "asSolution failed: %v", err)
		return nil, status.Errorf(codes.Internal, "failed to convert to solution: %v", err)
	}

	log.InfoContext(ctx, "Returning solution deployment")
	sd := &solutiondeploymentpb.SolutionDeployment{
		Name:          app.GetMetadata().GetSolutionDeploymentId(),
		DisplayName:   app.GetMetadata().GetDisplayName(),
		SolutionId:    app.GetMetadata().GetName(),
		OperationMode: app.GetOperationMode(),
		Solution:      solution,
	}
	return asView(sd, req.GetView()), nil
}

func asApplication(sol *solutionpb.Solution) (*apb.Application, error) {
	assets := make(map[string]*apb.Application_Asset)
	for id, asset := range sol.GetAssets() {
		appAsset, err := applicationasset.AssetToApplicationAsset(asset)
		if err != nil {
			return nil, fmt.Errorf("failed to convert asset %q: %w", id, err)
		}
		assets[id] = appAsset
	}

	instances := make(map[string]*apb.Application_Instance)
	for name, inst := range sol.GetInstances() {
		instances[name] = &apb.Application_Instance{
			Asset:  inst.GetAsset(),
			Config: inst.GetConfig(),
		}
	}

	return &apb.Application{
		Metadata: &commonpb.Metadata{
			Category: commonpb.Metadata_INSTANCE,
		},
		Process:            &processpb.Process{},
		Assets:             assets,
		Instances:          instances,
		ObjectWorldUpdates: sol.GetObjectWorldUpdates(),
	}, nil
}

func asSolution(app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) (*solutionpb.Solution, error) {
	rtsByID := map[string]*rtrpb.ResourceTypeRuntime{}
	for _, rt := range rts {
		id := idutils.IDFromProtoUnchecked(rt.GetMetadata().GetIdVersion().GetId())
		rtsByID[id] = rt
	}

	assets := make(map[string]*assetpb.Asset)
	for id, rtr := range rtsByID {
		asset, err := runtime.RuntimeToAsset(rtr)
		if err != nil {
			// TODO: b/517345754: Remove when all old-style pose estimators and
			// resources using world fragments have been removed from all solutions
			// and we can enforce this conversion.
			log.Warningf("Omitting %q from solution because runtime.RuntimeToAsset failed: %v", id, err)
		} else {
			assets[id] = asset
		}
	}

	instances := make(map[string]*aigrpcpb.AssetInstance)
	for _, ri := range app.GetResources().GetResourceInstances() {
		id, err := idutils.RemoveVersionFrom(ri.GetTypeIdVersion())
		if err != nil {
			return nil, fmt.Errorf("invalid type_id_version for instance %q: %w", ri.GetName(), err)
		}
		rtr, ok := rtsByID[id]
		if !ok {
			return nil, fmt.Errorf("missing runtime for instance %q (type %q)", ri.GetName(), id)
		}
		instances[ri.GetName()] = instanceconversion.ConvertResourceInstanceToAssetInstance(ri, rtr)
	}

	return &solutionpb.Solution{
		Assets:             assets,
		Instances:          instances,
		ObjectWorldUpdates: app.GetResources().GetObjectWorldUpdates().GetUpdates(),
	}, nil
}

func viewOrDefault(view solutiondeploymentpb.SolutionDeploymentView, defaultView solutiondeploymentpb.SolutionDeploymentView) (solutiondeploymentpb.SolutionDeploymentView, error) {
	switch view {
	case solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_UNSPECIFIED:
		return defaultView, nil
	case solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_BASIC,
		solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_FULL:
		return view, nil
	default:
		return solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_UNSPECIFIED, status.Errorf(codes.Unimplemented, "unsupported view %q", view)
	}
}

// asView filters the SolutionDeployment based on the requested view.
func asView(sd *solutiondeploymentpb.SolutionDeployment, view solutiondeploymentpb.SolutionDeploymentView) *solutiondeploymentpb.SolutionDeployment {
	switch view {
	case solutiondeploymentpb.SolutionDeploymentView_SOLUTION_DEPLOYMENT_VIEW_BASIC:
		return &solutiondeploymentpb.SolutionDeployment{
			Name:          sd.GetName(),
			DisplayName:   sd.GetDisplayName(),
			Solution:      basicSolutionView(sd.GetSolution()),
			SolutionId:    sd.GetSolutionId(),
			OperationMode: sd.GetOperationMode(),
		}
	default:
		return sd // Validation and defaults are handled on entry to the RPC.
	}
}

// basicSolutionView returns a basic view of the Solution proto.  This only
// describes the id or id versions of assets, and the names and assets of
// instances.  Object world updates are not provided.
func basicSolutionView(solution *solutionpb.Solution) *solutionpb.Solution {
	if solution == nil {
		return nil
	}
	assets := make(map[string]*assetpb.Asset)
	for k, v := range solution.GetAssets() {
		assets[k] = viewutils.BasicAssetView(v)
	}
	instances := make(map[string]*aigrpcpb.AssetInstance)
	for k, v := range solution.GetInstances() {
		instances[k] = instanceconversion.AsView(v, aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_BASIC)
	}
	return &solutionpb.Solution{
		Assets:    assets,
		Instances: instances,
	}
}

func (s *DeployService) validateDependencies(ctx context.Context, app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) (*espb.ExtendedStatus, error) {
	r := report.New(report.AsWarningIfType[error]())
	sc, err := runtimegraph.NewSolutionContext(ctx, app, slices.Collect(maps.Values(rts)), runtimegraph.WithPlatformRuntime())
	if err != nil {
		return nil, fmt.Errorf("failed to create solution context: %w", err)
	}
	if err := graph.ValidateAll(ctx, sc, graph.WithReport(r)); err != nil {
		return nil, err
	}
	if len(r.Warnings()) > 0 {
		extStatus := r.ToExtendedStatus(
			report.WithTitlePrefix("Validation errors during Solution deployment"),
		)
		return extStatus.Proto(), nil
	}
	return nil, nil
}
