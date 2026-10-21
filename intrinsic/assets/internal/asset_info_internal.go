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

// Package assetinfointernal contains an internal service to manage assets
// within a solution.
package assetinfointernal

import (
	"context"
	"fmt"

	"intrinsic/assets/dependencies/graph"
	"intrinsic/assets/dependencies/runtimegraph"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/skills/internal/skillcomposer"

	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"

	aiipb "intrinsic/assets/proto/v1alpha1/asset_info_internal_go_proto"
	gridpb "intrinsic/resources/proto/geometric_resource_data_go_proto"
	rrpb "intrinsic/resources/proto/resource_registry_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"
)

// ResourceReader has methods to retrieve resource instances from the currently
// running application.
type ResourceReader interface {
	ResourceInstancesAsMap(ctx context.Context, includeAllAssetTypes bool) (map[string]*rrpb.ResourceInstance, map[string]*rtrpb.ResourceTypeRuntime, error)
	ResourceInstance(ctx context.Context, id string) (*rrpb.ResourceInstance, *rtrpb.ResourceTypeRuntime, error)
	GetGeometricResourceSetData(ctx context.Context) ([]*gridpb.GeometricResourceInstanceData, *owupb.ObjectWorldUpdates, error)
}

// Server contains data associated with a resource registry server.
type Server struct {
	appClient     asgrpcpb.HotSharedStateApplicationServiceClient
	rr            ResourceReader
	rtrClient     resourcetyperuntime.Client
	skillComposer skillcomposer.Client
}

func (s *Server) GetGeometricResourceSetData(ctx context.Context, _ *aiipb.GetGeometricResourceSetDataRequest) (*gridpb.GeometricResourceSetData, error) {
	ctx, span := trace.StartSpan(ctx, "assetinfointernal.GeometricResourceSetData")
	defer span.End()

	rids, updates, err := s.rr.GetGeometricResourceSetData(ctx)
	if err != nil {
		return nil, fmt.Errorf("could not get world data from resource set: %w", err)
	}

	return &gridpb.GeometricResourceSetData{
		InstanceData: rids,
		Updates:      updates,
	}, nil
}

func (s *Server) GetResourceTypeRuntime(ctx context.Context, request *aiipb.GetResourceTypeRuntimeRequest) (*aiipb.GetResourceTypeRuntimeResponse, error) {
	ctx, span := trace.StartSpan(ctx, "assetinfointernal.GetResourceTypeRuntime")
	defer span.End()

	resp, err := s.rtrClient.Get(ctx, request.GetIdVersion())
	if err != nil {
		return nil, grpcstatus.Errorf(codes.NotFound, "could not find runtime data for resources with type id_version %q", request.GetIdVersion())
	}
	return &aiipb.GetResourceTypeRuntimeResponse{
		ResourceTypeRuntime: resp,
	}, nil
}

func (s *Server) GetConnectedServices(ctx context.Context, request *aiipb.GetConnectedServicesRequest) (*aiipb.GetConnectedServicesResponse, error) {
	ctx, span := trace.StartSpan(ctx, "assetinfointernal.GetConnectedServices")
	defer span.End()

	ri, rtr, err := s.rr.ResourceInstance(ctx, request.GetName())
	if err != nil {
		return nil, err
	}

	cr := []*aiipb.ConnectedService{}
	// For now we only check for resources that both have geometry and services.
	// In the future these self-loop dependencies may be replaced by a more
	// general graph search over asset dependency relationships.
	hasGeometry := rtr.GetSceneObject() != nil || rtr.GetWorldFragment() != nil
	if ri.GetHasServices() && hasGeometry {
		cr = append(cr, &aiipb.ConnectedService{Name: ri.GetName()})
	}
	return &aiipb.GetConnectedServicesResponse{
		ConnectedServices: cr,
	}, nil
}

func skillDetailsToInternalSkillData(skillDetails *skillcomposer.SkillDetails) (*aiipb.InternalSkillData, error) {
	idVersion, err := idutils.IDOrIDVersionProtoFrom(skillDetails.Skill.GetIdVersion())
	if err != nil {
		return nil, err
	}
	resp := &aiipb.InternalSkillData{
		IdVersion:  idVersion,
		Sideloaded: skillDetails.Skill.GetSideloaded(),
	}
	if addresses := skillDetails.Addresses; addresses != nil {
		if addresses.Validate != "" || addresses.Project != "" || addresses.Execute != "" || addresses.SkillInfo != "" {
			resp.GrpcTargets = &aiipb.InternalSkillData_GrpcTargets{
				Validate:  addresses.Validate,
				Project:   addresses.Project,
				Execute:   addresses.Execute,
				SkillInfo: addresses.SkillInfo,
			}
		}
	}
	return resp, nil
}

func (s *Server) BatchGetInternalSkillData(ctx context.Context, request *aiipb.BatchGetInternalSkillDataRequest) (*aiipb.BatchGetInternalSkillDataResponse, error) {
	ctx, span := trace.StartSpan(ctx, "assetinfointernal.BatchGetInternalSkillData")
	defer span.End()

	skillDetails, err := s.skillComposer.BatchGetSkills(ctx, request.GetIds())
	if err != nil {
		return nil, err
	}
	var internalSkillData []*aiipb.InternalSkillData
	for _, skillDetail := range skillDetails {
		isd, err := skillDetailsToInternalSkillData(skillDetail)
		if err != nil {
			return nil, err
		}
		internalSkillData = append(internalSkillData, isd)
	}
	return &aiipb.BatchGetInternalSkillDataResponse{
		InternalSkillData: internalSkillData,
	}, nil
}

func (s *Server) CheckSolutionValidity(ctx context.Context, request *aiipb.CheckSolutionValidityRequest) (*aiipb.CheckSolutionValidityResponse, error) {
	ctx, span := trace.StartSpan(ctx, "assetinfointernal.CheckSolutionValidity")
	defer span.End()

	appRes, err := s.appClient.GetCurrentApplication(ctx, &asgrpcpb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, grpcstatus.Errorf(codes.Internal, "could not get current application: %v", err)
	}

	rtrs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient)
	if err != nil {
		return nil, grpcstatus.Errorf(codes.Internal, "could not get resource type runtimes: %v", err)
	}

	sc, err := runtimegraph.NewSolutionContext(
		ctx,
		appRes.GetApplication().GetResources().GetResourceInstances(),
		rtrs,
		runtimegraph.WithPlatformRuntime(),
	)
	if err != nil {
		return nil, grpcstatus.Errorf(codes.Internal, "could not create solution context: %v", err)
	}

	// Treat all errors added to the report as warnings.
	rep := report.New(report.AsWarningIfType[error]())
	if err := graph.ValidateAll(ctx, sc, graph.WithReport(rep)); err != nil {
		return nil, grpcstatus.Errorf(codes.Internal, "validation failed: %v", err)
	}
	resp := &aiipb.CheckSolutionValidityResponse{}
	if len(rep.Warnings()) > 0 {
		resp.ValidationErrors = rep.ToExtendedStatus().Proto()
	}
	return resp, nil
}

// NewServerOpts contains various GRPC clients used by the registry.
type NewServerOpts struct {
	AppClient     asgrpcpb.HotSharedStateApplicationServiceClient
	RR            ResourceReader
	RTRClient     resourcetyperuntime.Client
	SkillComposer skillcomposer.Client
}

// NewServer creates a resource registry server for the provided cluster.
func NewServer(opts NewServerOpts) *Server {
	return &Server{
		appClient:     opts.AppClient,
		rr:            opts.RR,
		rtrClient:     opts.RTRClient,
		skillComposer: opts.SkillComposer,
	}
}
