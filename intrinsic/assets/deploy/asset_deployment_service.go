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

// Package assetdeploymentservice implements AssetDeploymentService (ADS) that is responsible for
// managing the deployment of skills and resource instances to the workcell.
package assetdeploymentservice

import (
	"context"
	"fmt"
	"slices"
	"time"

	"intrinsic/assets/dependencies/graph"
	"intrinsic/assets/dependencies/platform"
	"intrinsic/assets/dependencies/resolver"
	"intrinsic/assets/dependencies/runtimegraph"
	"intrinsic/assets/deploy/privileges"
	"intrinsic/assets/deploy/render"
	"intrinsic/assets/deploy/resourcefixer"
	"intrinsic/assets/deploy/resourcewriter"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/kubernetes/acl/clientcontext"
	"intrinsic/kubernetes/workcell_spec/runtimedbtransfer"
	"intrinsic/longrunning/go/operations"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/resources/service/resourceworld"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"go.opencensus.io/plugin/ocgrpc"
	"go.opencensus.io/trace"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	adsgrpcpb "intrinsic/assets/proto/asset_deployment_go_proto"
	pb "intrinsic/assets/proto/asset_deployment_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	apppb "intrinsic/config/proto/application_go_proto"
	datafilespb "intrinsic/config/proto/data_files_go_proto"
	tpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	ppb "intrinsic/math/proto/pose_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	rcpb "intrinsic/resources/proto/runtime_context_go_proto"
	socpb "intrinsic/scene/proto/v1/scene_object_config_go_proto"
	simpb "intrinsic/simulation/service/proto/first_party/simulation_service_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	owrpb "intrinsic/world/public/proto/object_world_refs_go_proto"
	grpcowspb "intrinsic/world/public/proto/object_world_service_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	dpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

const (
	// With the removal of the workcell-spec, this is always constant.  If we
	// wanted to be conservative, we could call "GetWorkcellStatus" on the
	// transfer service and grab the "name" field.
	appChartName = "intrinsic-app-chart"

	// TODO - b/371204053: Remove or figure out correct init world when there
	// are multiple init worlds.
	defaultInitWorldID = "init_world"

	// TODO - b/447219121: Remove this once we no longer support simulation
	// reset.  Right now this guards against unnecessary resets when the world
	// the simulation service is targeting has not changed.
	defaultBeliefWorldID = "world"

	// This is a default timeout that can conservatively be applied to all
	// operations to ensure that we at least continue to process operations.
	// This can be updated if we have better stats on how long all operations
	// should actually take.
	fallbackOperationTimeout = 3 * time.Minute
)

// World covers the requirements deploy has on the resourceworld.Client.
type World interface {
	UpdateResourceSetWorldRelations(context.Context, string) error
	UpdateWorldFromResourceSet(context.Context, string, ...resourceworld.UpdateWorldFromResourceSetOption) (resourceworld.UpdateWorldFromResourceSetResult, error)
}

// simClient covers the requirements deploy has on the simgrpcpb.SimulationServiceClient.
type simClient interface {
	ResetSimulation(ctx context.Context, req *simpb.ResetSimulationRequest, opts ...grpc.CallOption) (*emptypb.Empty, error)
}

// transfer covers the requirements to call the transfer service either
// directly or through a client.
type transfer interface {
	ApplyWorkcellSpec(ctx context.Context, ws *tpb.WorkcellSpec) error
}

type deploy struct {
	writer           *resourcewriter.Client
	world            World
	simClient        simClient // TODO - b/447219121: remove
	appClient        asgrpcpb.HotSharedStateApplicationServiceClient
	transferClient   transfer
	rtrClient        resourcetyperuntime.Client
	k8sClient        kubernetes.Interface
	runner           *operations.Runner
	clusterParams    render.ClusterParams
	initDataParams   render.InitDataFilesParams
	onSolutionUpdate func(context.Context, *apppb.Application, []*rtrpb.ResourceTypeRuntime) error
}

// Options contains parameters needed to create an instance of `deploy` that implements
// the Asset Deployment Service
type Options struct {
	ApplicationClient     asgrpcpb.HotSharedStateApplicationServiceClient
	World                 World
	OWSClient             grpcowspb.ObjectWorldServiceClient
	ACIClient             acigrpcpb.AssetCatalogInternalClient
	SimClient             simClient // TODO - b/447219121: remove
	TransferServiceClient transfer
	RTRClient             resourcetyperuntime.Client
	K8sClient             kubernetes.Interface
	Runner                *operations.Runner
	Validator             privileges.Validator
	ClusterParams         render.ClusterParams
	InitDataFilesParams   render.InitDataFilesParams
	OnSolutionUpdate      func(context.Context, *apppb.Application, []*rtrpb.ResourceTypeRuntime) error
}

type configureServiceDynamicOptions struct {
	Name              string
	Config            *anypb.Any
	FileDescriptorSet *dpb.FileDescriptorSet
	K8sClient         kubernetes.Interface
}
type fConfigureServiceDynamic func(ctx context.Context, opts configureServiceDynamicOptions) error

// Functions assigned as vars so they can be mocked out for testing.
var (
	configureServiceDynamic fConfigureServiceDynamic = defaultConfigureServiceDynamic
)

// New creates a new instance of the Asset Deployment Service
func New(opts Options) adsgrpcpb.AssetDeploymentServiceServer {
	return &deploy{
		writer: resourcewriter.NewClient(resourcewriter.NewClientOpts{
			AppClient: opts.ApplicationClient,
			OWSClient: opts.OWSClient,
			RTRClient: opts.RTRClient,
			ACIClient: opts.ACIClient,
			Validator: opts.Validator,
		}),
		world:            opts.World,
		simClient:        opts.SimClient,
		appClient:        opts.ApplicationClient,
		transferClient:   opts.TransferServiceClient,
		rtrClient:        opts.RTRClient,
		k8sClient:        opts.K8sClient,
		runner:           opts.Runner,
		clusterParams:    opts.ClusterParams,
		initDataParams:   opts.InitDataFilesParams,
		onSolutionUpdate: opts.OnSolutionUpdate,
	}
}

type instance struct {
	// name specifies the name of the instance.  Required.
	name string
	// asset defines the asset that backs the instance.  Required.
	asset *idpb.IdVersion
	// parent defines the parent of scene object.  Optional.
	parent *owrpb.ObjectReferenceWithEntityFilter
	// parentTThis is the pose of the scene object relative to the parent.  Optional.
	parentTThis *ppb.Pose
	// config is the parameters provided to the service instance.  Optional.
	config *anypb.Any
	// sceneObjectConfig is the scene object configuration for the instance.  Optional.
	sceneObjectConfig *socpb.SceneObjectConfig
	// dataFiles specifies the file references attached to the instance and
	// potentially downloaded on behalf of the service.  This will eventually
	// be replaced by data assets.
	dataFiles *datafilespb.DataFiles
}

func (d *deploy) CreateResourceFromCatalog(ctx context.Context, req *pb.CreateResourceFromCatalogRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "asset_deployment_service.CreateResourceFromCatalog")
	span.AddAttributes(trace.StringAttribute("world_id", req.GetWorldId()))
	defer span.End()

	log.InfoContextf(ctx, "CreateResourceFromCatalog: %q", req.GetTypeIdVersion())

	if err := idutils.ValidateName(req.GetConfiguration().GetName()); err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid name: %v", err)
	}

	var asset *idpb.IdVersion
	if parts, err := idutils.NewIDVersionParts(req.GetTypeIdVersion()); err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid type_id_version: %v", err)
	} else {
		asset = parts.IDVersionProto()
	}

	// Ensure the Asset ID is not reserved (platform.RuntimeAssetID).
	if err := platform.ValidateIDNotReserved(asset.GetId()); err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "instance %q has a reserved ID: %v", req.GetConfiguration().GetName(), err)
	}
	// Ensure the Asset instance is not reserved (platform.RuntimeInstanceName).
	if err := platform.ValidateInstanceNameNotReserved(req.GetConfiguration().GetName()); err != nil {
		return nil, err
	}

	op, err := d.runner.ScheduleNew(ctx, "instance/create", func(ctx context.Context) (proto.Message, error) {
		// The context passed into Enqueue is disconnected from the context
		// passed into the RPC by WithoutCancel.  Downstream calls can hang
		// indefinitely if we do not provide a fallback.
		ctx, cancel := context.WithTimeout(ctx, fallbackOperationTimeout)
		defer cancel()
		if err := d.create(ctx, &instance{
			name:              req.GetConfiguration().GetName(),
			asset:             asset,
			parent:            req.GetConfiguration().GetParent(),
			parentTThis:       req.GetConfiguration().GetParentTThis(),
			config:            req.GetConfiguration().GetConfiguration(),
			sceneObjectConfig: req.GetConfiguration().GetSceneObjectConfig(),
			dataFiles:         req.GetConfiguration().GetDataFiles(),
		}, req.GetWorldId()); err != nil {
			return nil, err
		}
		return &pb.CreateResourceFromCatalogResponse{
			Name: req.GetConfiguration().GetName(),
		}, nil
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op.Proto(), nil
}

func (d *deploy) DeleteResource(ctx context.Context, req *pb.DeleteResourceRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "asset_deployment_service.DeleteResource")
	span.AddAttributes(trace.StringAttribute("world_id", req.GetWorldId()))
	defer span.End()

	log.InfoContextf(ctx, "DeleteResource: %q", req.GetName())

	op := operations.NewWithPrefix("instance/delete")
	if err := d.runner.Schedule(ctx, op, func(ctx context.Context) (proto.Message, error) {
		// The context passed into Enqueue is disconnected from the context
		// passed into the RPC by WithoutCancel.  Downstream calls can hang
		// indefinitely if we do not provide a fallback.
		ctx, cancel := context.WithTimeout(ctx, fallbackOperationTimeout)
		defer cancel()
		if err := d.deleteResource(ctx, req, op); err != nil {
			return nil, err
		}
		return &emptypb.Empty{}, nil
	}); err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op.Proto(), nil
}

func (d *deploy) UpdateResource(ctx context.Context, req *pb.UpdateResourceRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "asset_deployment_service.UpdateResource")
	span.AddAttributes(trace.StringAttribute("world_id", req.GetWorldId()))
	defer span.End()

	log.InfoContextf(ctx, "UpdateResource: %q", req.GetResource().GetName())

	op, err := d.runner.ScheduleNew(ctx, "instance/update", func(ctx context.Context) (proto.Message, error) {
		// The context passed into Enqueue is disconnected from the context
		// passed into the RPC by WithoutCancel.  Downstream calls can hang
		// indefinitely if we do not provide a fallback.
		ctx, cancel := context.WithTimeout(ctx, fallbackOperationTimeout)
		defer cancel()
		return d.updateResource(ctx, req)
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op.Proto(), nil
}

// create will add a given instance to the solution and updated the provided
// world.  A default will be used if worldID is empty.
func (d *deploy) create(ctx context.Context, i *instance, worldID string) error {
	if worldID == "" {
		log.InfoContextf(ctx, "world_id not provided, assuming %q", defaultInitWorldID)
		worldID = defaultInitWorldID
	}

	name := i.name
	// TODO(b/297063488): undo the changes on error.
	appResp, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	app := appResp.GetApplication()
	if err != nil {
		return fmt.Errorf("failed to get current application while updating %q: %w", name, err)
	}
	simulated, err := render.IsSimulated(app)
	if err != nil {
		return err
	}

	if err := d.world.UpdateResourceSetWorldRelations(ctx, worldID); err != nil {
		return fmt.Errorf("failed to update resource set to world relations: %w", err)
	}

	// 1. Forward the call to resource registry service to:
	// - verify that resource type exists in the resource catalog.
	// - verify that changes can be applied to the world successfully.
	// - update application's resourceset to contain the newly requested resource instance.

	ctxWithAuth, err := clientcontext.ToContextFromIncoming(ctx)
	if err != nil {
		return clientcontext.ErrGRPC(err)
	}
	hasServices, err := d.writer.CreateResourceInstance(ctxWithAuth, &resourcewriter.CreateResourceInstanceOpts{
		TypeIDVersion:     idutils.IDVersionFromProtoUnchecked(i.asset),
		Name:              i.name,
		Parent:            i.parent,
		ParentTThis:       i.parentTThis,
		Configuration:     i.config,
		SceneObjectConfig: i.sceneObjectConfig,
		DataFiles:         i.dataFiles,
	})
	if err != nil {
		switch {
		case errors.Is(err, resourcewriter.ErrResourceInstanceAlreadyExists):
			return status.Errorf(codes.AlreadyExists, "instance already exists with id %q", name)
		case errors.Is(err, resourcewriter.ErrInvalidResourceTypeID):
			return status.Errorf(codes.InvalidArgument, "invalid asset id %q", idutils.IDVersionFromProtoUnchecked(i.asset))
		case errors.Is(err, resourcewriter.ErrParentSetMissingWorldFragment):
			return status.Errorf(codes.InvalidArgument, "parent was set but asset doesn't contain a world fragment")
		case errors.Is(err, resourcewriter.ErrResourceTypeNotFound):
			return status.Errorf(codes.NotFound, "asset id %q cannot be found in catalog", idutils.IDVersionFromProtoUnchecked(i.asset))
		case errors.Is(err, resourcewriter.ErrInvalidWorldFragmentUpdate):
			return status.Errorf(codes.FailedPrecondition, "%v", err)
		case errors.Is(err, resourcewriter.ErrInvalidSceneObjectUpdate):
			return status.Errorf(codes.FailedPrecondition, "%v", err)
		case errors.Is(err, resourcewriter.ErrSecurityContext):
			return status.Errorf(codes.PermissionDenied, "%v", err)
		case errors.Is(err, resourcefixer.ErrMissingCameraIdentifier):
			return status.Errorf(codes.InvalidArgument, "failed to update config for %q: %v", name, err)
		default:
			return status.Errorf(codes.Internal, "could not create instance %q: %v", name, err)
		}
	}
	log.InfoContextf(ctxWithAuth, "Successfully instantiated resource instance: %q", name)

	// Expect for this logic to be reorganized in the future to be more
	// explicit about how and when the world is updated. resourceworld retains
	// much of the structure from when world operations were implemented in the
	// resource registry and how it expects to work with state no longer makes
	// sense with ADS directly managing state of worlds and deployments.
	log.InfoContextf(ctx, "Updating the world")
	// 2. Update the world from the updated resourceset.
	if _, err := d.world.UpdateWorldFromResourceSet(ctx, worldID); err != nil {
		return fmt.Errorf("failed to update world with %q: %w", name, err)
	}
	log.InfoContextf(ctx, "Updated the world successfully")

	if hasServices {
		if err := d.deployResourcesChart(ctx); err != nil {
			return fmt.Errorf("failed to deploy created resource %q: %w", name, err)
		}
	} else {
		log.InfoContextf(ctx, "Resource %q has no services, skip deploy step.", name)
	}

	// TODO - b/447219121: Remove this once we no longer support simulation
	// reset.  Right now this guards against unnecessary resets when the world
	// the simulation service is targeting has not changed.
	if simulated && worldID == defaultBeliefWorldID {
		// Sim reset can take a long time, but has also been known to hang.
		// Enforce a conservative deadline.
		ctx, cancel := context.WithTimeout(ctx, 30*time.Second)
		defer cancel()
		log.InfoContextf(ctx, "Resetting simulation for %q", name)
		if _, err := d.simClient.ResetSimulation(ctx, &simpb.ResetSimulationRequest{}); err != nil {
			return fmt.Errorf("failed to reset simulation for %q: %w", name, err)
		}
		log.InfoContextf(ctx, "Reset simulation successfully for %q", name)
	}

	// TODO: b/519718697 - resourcewriter and resourceworld update HSS and
	// runtimedb, so we have to fetch this again after they are called.  Getting
	// rid of this will require inlining much of the logic from those libraries
	// and adopting a plan, validate, act approach, similar to installed assets.
	if currentRTRs, err := resourcetyperuntime.GetAll(ctx, d.rtrClient); err != nil {
		log.WarningContextf(ctx, "failed to get runtime types for onSolutionUpdate: %v", err)
	} else if appResp, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{}); err != nil {
		log.WarningContextf(ctx, "failed to get current application for onSolutionUpdate: %v", err)
	} else if err := d.onSolutionUpdate(ctx, appResp.GetApplication(), currentRTRs); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed after create: %v", err)
	}

	return nil
}

func (d *deploy) deleteResource(ctx context.Context, req *pb.DeleteResourceRequest, op *operations.Operation) error {
	if req.GetWorldId() == "" {
		log.InfoContextf(ctx, "world_id not provided, assuming %q", defaultInitWorldID)
		req.WorldId = defaultInitWorldID
	}

	name := req.GetName()

	app, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return errors.Wrapf(err, "failed to get current application while deleting resource %q", name)
	}

	// Run dependency validation for the state of the Solution with the instance deleted.
	appClone := proto.Clone(app.GetApplication()).(*apppb.Application)
	appClone.GetResources().ResourceInstances = slices.DeleteFunc(appClone.GetResources().GetResourceInstances(), func(ri *ripb.ResourceInstance) bool {
		return ri.GetName() == name
	})
	// Treat all validation errors added to the report as warnings.
	// This would have to be changed when we introduce different validation policies,
	// such as STRICT, RELAXED, etc.
	r := report.New(report.AsWarningIfType[error]())
	if err := d.validateDependencies(ctx, appClone, graph.WithReport(r), graph.WithOnlyReferencesTo(name)); err != nil {
		log.ErrorContextf(ctx, "dependency validation failed during deletion of instance %q: %v", name, err)
		return status.Errorf(codes.FailedPrecondition, "dependency validation failed during deletion of instance %q: %v", name, err)
	} else if len(r.Warnings()) > 0 {
		log.WarningContextf(ctx, "dependencies validation warnings: %v", r.Warnings())
		op.SetMetadata(&pb.DeleteResourceMetadata{
			Warnings: r.ToExtendedStatus(
				report.WithTitlePrefix(fmt.Sprintf("Validation errors when deleting instance %q", name)),
			).Proto(),
		})
	}

	// Remove the instance from the world and the application proto.
	hasServices, err := d.writer.DeleteResourceInstance(ctx, req.GetName(), req.GetWorldId())
	if err != nil {
		switch {
		case errors.Is(err, resourcewriter.ErrResourceNotFound):
			return status.Errorf(codes.NotFound, "could not find instance %q", name)
		default:
			return status.Errorf(codes.Internal, "could not delete resource instance %q: %v", name, err)
		}
	}

	if hasServices {
		if err := d.deployResourcesChart(ctx); err != nil {
			return err
		}
	} else {
		log.InfoContextf(ctx, "Resource %q has no services, skip deploy step.", name)
	}

	// TODO: b/519718697 - resourcewriter and resourceworld update HSS and
	// runtimedb, so we have to fetch this again after they are called.  Getting
	// rid of this will require inlining much of the logic from those libraries
	// and adopting a plan, validate, act approach, similar to installed assets.
	if currentRTRs, err := resourcetyperuntime.GetAll(ctx, d.rtrClient); err != nil {
		log.WarningContextf(ctx, "failed to get runtime types for onSolutionUpdate: %v", err)
	} else if appResp, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{}); err != nil {
		log.WarningContextf(ctx, "failed to get current application for onSolutionUpdate: %v", err)
	} else if err := d.onSolutionUpdate(ctx, appResp.GetApplication(), currentRTRs); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed after delete: %v", err)
	}

	return nil
}

func (d *deploy) validateDependencies(ctx context.Context, app *apppb.Application, opts ...graph.ValidateGraphOption) error {
	rtrs, err := resourcetyperuntime.GetAll(ctx, d.rtrClient)
	if err != nil {
		return errors.Wrap(err, "failed to get resource type runtimes for validation")
	}
	sc, err := runtimegraph.NewSolutionContext(
		ctx,
		app.GetResources().GetResourceInstances(),
		rtrs,
		runtimegraph.WithPlatformRuntime(),
	)
	if err != nil {
		return errors.Wrap(err, "failed to build solution context for validation")
	}
	return graph.ValidateAll(ctx, sc, opts...)
}

func (d *deploy) updateResource(ctx context.Context, req *pb.UpdateResourceRequest) (*pb.Resource, error) {
	if req.GetWorldId() == "" {
		log.InfoContextf(ctx, "world_id not provided, assuming %q", defaultInitWorldID)
		req.WorldId = defaultInitWorldID
	}

	name := req.GetResource().GetName()

	log.InfoContextf(ctx, "updating ResourceSet world relations for %q", name)
	if err := d.world.UpdateResourceSetWorldRelations(ctx, req.GetWorldId()); err != nil {
		return nil, fmt.Errorf("failed to update resource set to world relations: %w", err)
	}

	r, hasServices, supportsDynamicReconfiguration, fds, err := d.updateInRegistry(ctx, req)
	if err != nil {
		return nil, fmt.Errorf("failed to update resource %q: %w", name, err)
	}

	appResp, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, fmt.Errorf("failed to get current application while updating %q: %w", name, err)
	}
	app := appResp.GetApplication()
	simulated, err := render.IsSimulated(app)
	if err != nil {
		return nil, err
	}

	log.InfoContextf(ctx, "Updating the world")
	if _, err := d.world.UpdateWorldFromResourceSet(ctx, req.GetWorldId()); err != nil {
		return nil, fmt.Errorf("failed to update world with %q: %w", name, err)
	}
	log.InfoContextf(ctx, "Updated the world successfully")

	if hasServices {
		if supportsDynamicReconfiguration && req.GetResource().GetConfiguration().GetConfiguration() != nil {
			log.InfoContextf(ctx, "Updating %q service config via dynamic reconfiguration", name)
			if err := configureServiceDynamic(ctx, configureServiceDynamicOptions{
				Name:              name,
				Config:            req.GetResource().GetConfiguration().GetConfiguration(),
				FileDescriptorSet: fds,
				K8sClient:         d.k8sClient,
			}); err != nil {
				return nil, err
			}
		}

		if err := d.deployResourcesChart(ctx); err != nil {
			return nil, err
		}
	} else {
		log.InfoContextf(ctx, "%q has no services, skip service update.", name)
	}

	// TODO - b/447219121: Remove this once we no longer support simulation
	// reset.  Right now this guards against unnecessary resets when the world
	// the simulation service is targeting has not changed.
	if simulated && req.GetWorldId() == defaultBeliefWorldID {
		log.InfoContextf(ctx, "Resetting simulation for %q", name)
		if _, err := d.simClient.ResetSimulation(ctx, &simpb.ResetSimulationRequest{}); err != nil {
			return nil, fmt.Errorf("failed to reset simulation for %q: %w", name, err)
		}
		log.InfoContextf(ctx, "Reset simulation successfully for %q", name)
	}

	// TODO: b/519718697 - resourcewriter and resourceworld update HSS and
	// runtimedb, so we have to fetch this again after they are called.  Getting
	// rid of this will require inlining much of the logic from those libraries
	// and adopting a plan, validate, act approach, similar to installed assets.
	if currentRTRs, err := resourcetyperuntime.GetAll(ctx, d.rtrClient); err != nil {
		log.WarningContextf(ctx, "failed to get runtime types for onSolutionUpdate: %v", err)
	} else if appResp, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{}); err != nil {
		log.WarningContextf(ctx, "failed to get current application for onSolutionUpdate: %v", err)
	} else if err := d.onSolutionUpdate(ctx, appResp.GetApplication(), currentRTRs); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed after update: %v", err)
	}

	return r, nil
}

// defaultConfigureServiceDynamic reconfigures a Service via its DynamicReconfiguration service.
func defaultConfigureServiceDynamic(ctx context.Context, opts configureServiceDynamicOptions) error {
	config := opts.Config

	if resolvedConfig, err := resolver.New(render.IngressAddress, render.AssetInstanceHeader).ResolveConfigDependencies(config, opts.FileDescriptorSet); err != nil {
		log.ErrorContextf(ctx, "failed to resolve config dependencies for instance %q: %v", opts.Name, err)
	} else {
		config = resolvedConfig
	}

	port, err := grpcPort(ctx, opts.K8sClient, opts.Name)
	if err != nil {
		return err
	}

	drClient, err := getDynamicReconfigurationClient(ctx, opts.Name, port)
	if err != nil {
		return err
	}
	_, err = drClient.ApplyConfiguration(ctx, &drpb.ApplyConfigurationRequest{
		Configuration: config,
	})

	return err
}

func grpcPort(ctx context.Context, k8sClient kubernetes.Interface, name string) (int32, error) {
	configMapName := render.ConfigMapName(name)
	configMap, err := k8sClient.CoreV1().ConfigMaps(render.ResourceNamespace).Get(ctx, configMapName, metav1.GetOptions{})
	if err != nil {
		return 0, errors.Wrapf(err, "failed to get configmap %q", configMapName)
	}
	runtimeContext := &rcpb.RuntimeContext{}
	if err := proto.Unmarshal(configMap.BinaryData[render.CfgMapRuntimeContextKey], runtimeContext); err != nil {
		return 0, errors.Wrapf(err, "failed to unmarshal runtime context from configmap %q", configMapName)
	}
	return runtimeContext.GetPort(), nil
}

// getDynamicReconfigurationClient gets a DynamicReconfiguration client for the specified service.
//
// NOTE: We should just keep track of these clients in the AssetDeploymentService and pass them into
// the needed functions here. But since currently the DeployService also acts on Resources, ADS
// can't rely on keeping track of Resources internally.
func getDynamicReconfigurationClient(ctx context.Context, name string, port int32) (drpb.DynamicReconfigurationClient, error) {
	log.InfoContextf(ctx, "Creating new dynamic reconfiguration client for %q", name)
	address := fmt.Sprintf(
		"%s.%s.svc.cluster.local:%d",
		render.ResourceContainerName(name),
		render.ResourceNamespace,
		port,
	)
	conn, err := grpc.NewClient(address,
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithTransportCredentials(insecure.NewCredentials()),
	)
	if err != nil {
		return nil, errors.Wrapf(err, "failed to create dynamic reconfiguration client connection for %q at %q", name, address)
	}

	return drpb.NewDynamicReconfigurationClient(conn), nil
}

func (d *deploy) deployResourcesChart(ctx context.Context) error {
	log.InfoContextf(ctx, "deploying changes resources chart")
	asreq, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return fmt.Errorf("failed to get current application before deploying: %w", err)
	}
	app := asreq.GetApplication()

	simulated, err := render.IsSimulated(app)
	if err != nil {
		return err
	}

	log.InfoContextf(ctx, "Retrieving resource types from runtimedb")
	types, err := runtimedbtransfer.Retrieve(ctx, d.rtrClient, app)
	if err != nil {
		return errors.Wrap(err, "add resource chart")
	}

	// The initial workcell spec, with the application chart is deployed by the
	// deploy service.  We don't want to modify that deployment, however, we
	// need to update the resources chart and add new data files, as well as
	// preserve current ones.  The transfer service will simply update any
	// non-exclusive chart that is provided, however, it will garbage collect
	// any files that haven't been touched.  That is why we send all of the
	// data files in addition to the new resources chart.  If we wanted to be
	// "more correct", we could get the currently deployed workcell spec from
	// the transfer service, however, it does not store the currently deployed
	// spec and it's not actually super clear if it should in multi-chart
	// scenarios.  It's not actually enforcing that an exclusive chart is being
	// deployed every time.
	ws := new(tpb.WorkcellSpec)
	if err := render.AddResourceChart(ws, render.ResourceChartOptions{
		Instances:           app.GetResources().GetResourceInstances(),
		Types:               types,
		Parent:              appChartName,
		Simulated:           simulated,
		ClusterParams:       d.clusterParams,
		InitDataFilesParams: d.initDataParams,
	}); err != nil {
		return errors.Wrap(err, "add resource chart")
	}

	log.InfoContextf(ctx, "Applying a workcell spec on the transfer service")
	if err := d.transferClient.ApplyWorkcellSpec(ctx, ws); err != nil {
		return fmt.Errorf("failed to deploy: %w", err)
	}
	return nil
}

// updateInRegistry updates the instance in the resource registry.
//
// It returns:
//   - the resource that can be returned to the client.
//   - A bool indicating whether the resource has services.
//   - A bool indicating whether the service supports dynamic reconfiguration.
//   - The file descriptor set for the resource.
func (d *deploy) updateInRegistry(ctx context.Context, req *pb.UpdateResourceRequest) (*pb.Resource, bool, bool, *dpb.FileDescriptorSet, error) {
	if req.GetResource().GetIdVersion() != nil {
		return nil, false, false, nil, status.Error(codes.Unimplemented, "update of id_version is not supported")
	}

	var app *apppb.Application
	var revisionToken string
	if response, err := d.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{}); err != nil {
		return nil, false, false, nil, status.Errorf(codes.Internal, "unable to get solution information: %v", err)
	} else {
		app = response.GetApplication()
		revisionToken = response.GetRevisionToken()
	}

	name := req.GetResource().GetName()
	var instance *ripb.ResourceInstance
	for _, ri := range app.GetResources().GetResourceInstances() {
		if ri.GetName() == name {
			instance = ri
		}
	}
	if instance == nil {
		return nil, false, false, nil, status.Errorf(codes.NotFound, "could not find instance %q", name)
	}

	ivp, err := idutils.NewIDVersionParts(instance.GetTypeIdVersion())
	if err != nil {
		return nil, false, false, nil, status.Errorf(codes.Internal, "resource registry returned invalid id version: %v", err)
	}

	rtr, err := d.rtrClient.Get(ctx, instance.GetTypeIdVersion())
	if err != nil {
		return nil, false, false, nil, status.Errorf(codes.Internal, "unable to get asset information for %q: %v", name, err)
	}

	// Update resource instance and write entire cluster back.
	if c := req.GetResource().GetConfiguration().GetConfiguration(); c != nil {
		instance.Configuration = c
	}
	if soc := req.GetResource().GetConfiguration().GetSceneObjectConfig(); soc != nil {
		instance.SceneObjectConfig = soc
	}
	if df := req.GetResource().GetConfiguration().GetDataFiles(); df != nil {
		instance.DataFiles = df
	}
	if snh := req.GetResource().GetConfiguration().ScheduledNodeHostname; snh != nil {
		var sc *ripb.ResourceInstance_SchedulingConfig
		if *snh != "" {
			sc = &ripb.ResourceInstance_SchedulingConfig{
				RequiredNodeHostname: *snh,
			}
		}
		instance.SchedulingConfig = sc
	}

	if _, err := d.appClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   app,
		RevisionToken: revisionToken,
	}); err != nil {
		return nil, false, false, nil, status.Errorf(codes.Internal, "unable to set the solution state after updating %q: %v", name, err)
	}

	return &pb.Resource{
		Name:      name,
		IdVersion: ivp.IDVersionProto(),
		Configuration: &pb.ResourceConfiguration{
			Configuration:     instance.GetConfiguration(),
			DataFiles:         instance.GetDataFiles(),
			SceneObjectConfig: instance.SceneObjectConfig,
		},
	}, rtr.GetServiceDef() != nil, rtr.GetServiceDef().GetSupportsDynamicReconfiguration(), rtr.GetMetadata().GetFileDescriptorSet(), nil
}
