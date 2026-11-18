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

// Package assetinstancesservice implements the asset instances gRPC service
package assetinstancesservice

import (
	"context"
	"maps"
	"slices"

	"intrinsic/assets/idutils"
	"intrinsic/assets/instances/instanceconversion"
	"intrinsic/resources/service/resourceregistryutil"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/util/pagination/pagetoken"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	aigrpcpb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	dependencypb "intrinsic/assets/proto/v1/dependency_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
)

type service struct {
	appClient asgrpcpb.HotSharedStateApplicationServiceClient
	rtrClient resourcetyperuntime.Client
}

type paginatedInstance struct {
	*aigrpcpb.ListAssetInstancesRequest
}

func (r paginatedInstance) ClearPagination() {
	r.PageToken = ""
	r.PageSize = 0
}

func nameAsKey(s *aigrpcpb.AssetInstance) []string {
	return []string{
		s.GetName(),
	}
}

// dependencyMatchers returns the criteria that must be met to match a
// dependency.  This may be an empty list for a dependency that is specified,
// but is empty.  A user may specify this on a resolved dependency that simply
// wants to reference another instance.
func dependencyMatchers(fulfills *dependencypb.Dependency) []func(*aigrpcpb.AssetInstance) bool {
	var criteria []func(*aigrpcpb.AssetInstance) bool
	if len(fulfills.GetRequires()) > 0 {
		requires := make(map[string]struct{})
		for _, uri := range fulfills.GetRequires() {
			requires[uri] = struct{}{}
		}
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			unmet := maps.Clone(requires)
			for _, iface := range i.GetMetadata().GetProvides() {
				delete(unmet, iface.GetUri())
			}
			return len(unmet) == 0
		})
	}
	if fulfills.GetRequiresObject() != nil {
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			// This should be updated to use object details if/when that is added.
			// This is an indirect, but still reasonable way of doing it.
			switch i.GetMetadata().GetAssetType() {
			case atpb.AssetType_ASSET_TYPE_SCENE_OBJECT,
				atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
				return true
			default:
				return false
			}
		})
	}
	return criteria
}

// filterMatcher creates an matcher function that returns true if all of a
// filter's criteria are met.  It returns nil if the filter does not specify
// any criteria.
func filterMatcher(f *aigrpcpb.ListAssetInstancesRequest_Filter) func(*aigrpcpb.AssetInstance) bool {
	var criteria []func(*aigrpcpb.AssetInstance) bool
	if at := f.GetAssetType(); at != atpb.AssetType_ASSET_TYPE_UNSPECIFIED {
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			return at == i.GetMetadata().GetAssetType()
		})
	}
	if tag := f.GetAssetTag(); tag != atagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			return tag == i.GetMetadata().GetAssetTag()
		})
	}
	if f.Asset != nil {
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			return *f.Asset == i.GetAsset()
		})
	}
	if id := f.GetId(); id != nil {
		criteria = append(criteria, func(i *aigrpcpb.AssetInstance) bool {
			return proto.Equal(id, i.GetMetadata().GetIdVersion().GetId())
		})
	}
	criteria = append(criteria, dependencyMatchers(f.GetFulfills())...)
	if len(criteria) == 0 {
		return nil
	}
	return func(i *aigrpcpb.AssetInstance) bool {
		for _, c := range criteria {
			if !c(i) {
				return false
			}
		}
		return true
	}
}

// multiFilterMatcher creates an matcher function that returns true if any
// filter matches.  Returns nil if no filtering should be applied.
func multiFilterMatcher(filters []*aigrpcpb.ListAssetInstancesRequest_Filter) func(*aigrpcpb.AssetInstance) bool {
	if len(filters) == 0 {
		return nil // Unfiltered
	}
	var matchers []func(*aigrpcpb.AssetInstance) bool
	for _, filter := range filters {
		filterMatch := filterMatcher(filter)
		if filterMatch == nil {
			// A filter with no constraints has been specified.  This is the same as
			// an unfiltered search, since filters OR.
			return nil
		}
		matchers = append(matchers, filterMatcher(filter))
	}
	return func(i *aigrpcpb.AssetInstance) bool {
		for _, matcher := range matchers {
			if matcher(i) {
				return true
			}
		}
		return false
	}
}

func viewOrDefault(view aigrpcpb.AssetInstanceView, defaultView aigrpcpb.AssetInstanceView) (aigrpcpb.AssetInstanceView, error) {
	switch view {
	case aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_UNSPECIFIED:
		return defaultView, nil
	case aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_BASIC,
		aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_DETAIL,
		aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_FULL:
		return view, nil
	default:
		return aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_UNSPECIFIED, status.Errorf(codes.Unimplemented, "unsupported view %q", view)
	}
}

func (s *service) getApplication(ctx context.Context) (*apb.Application, string, error) {
	resp, err := s.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, "", status.Errorf(codes.Internal, "failed to get current application: %v", err)
	}
	return resp.GetApplication(), resp.GetRevisionToken(), nil
}

func (s *service) GetAssetInstance(ctx context.Context, req *aigrpcpb.GetAssetInstanceRequest) (*aigrpcpb.AssetInstance, error) {
	if view, err := viewOrDefault(req.GetView(), aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_DETAIL); err != nil {
		return nil, err
	} else {
		req.View = view
	}

	app, _, err := s.getApplication(ctx)
	if err != nil {
		return nil, err
	}

	var ri *ripb.ResourceInstance
	for _, r := range app.GetResources().GetResourceInstances() {
		if r.GetName() == req.GetName() {
			ri = r
			break
		}
	}
	if ri == nil {
		return nil, status.Errorf(codes.NotFound, "instance %q not found", req.GetName())
	}

	rtr, err := s.rtrClient.Get(ctx, ri.GetTypeIdVersion())
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not get runtime info for asset %q: %v", ri.GetTypeIdVersion(), err)
	}

	return instanceconversion.AsView(instanceconversion.ConvertResourceInstanceToAssetInstance(ri, rtr), req.GetView()), nil
}

func (s *service) batchGetRTRs(ctx context.Context, ris []*ripb.ResourceInstance) (map[string]*rtrpb.ResourceTypeRuntime, error) {
	seen := make(map[string]struct{}, len(ris))
	uniqueIDVs := make([]string, 0, len(ris))
	for _, ri := range ris {
		idv := ri.GetTypeIdVersion()
		if _, ok := seen[idv]; !ok {
			seen[idv] = struct{}{}
			uniqueIDVs = append(uniqueIDVs, idv)
		}
	}
	rtrs, err := s.rtrClient.BatchGet(ctx, uniqueIDVs)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not get runtime info for assets: %v", err)
	}
	rtrByIDV := make(map[string]*rtrpb.ResourceTypeRuntime, len(rtrs))
	for _, rtr := range rtrs {
		rtrByIDV[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtr
	}
	return rtrByIDV, nil
}

func (s *service) BatchGetAssetInstances(ctx context.Context, req *aigrpcpb.BatchGetAssetInstancesRequest) (*aigrpcpb.BatchGetAssetInstancesResponse, error) {
	if len(req.GetNames()) == 0 {
		return nil, status.Errorf(codes.InvalidArgument, "no names were requested")
	}
	if view, err := viewOrDefault(req.GetView(), aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_DETAIL); err != nil {
		return nil, err
	} else {
		req.View = view
	}

	app, _, err := s.getApplication(ctx)
	if err != nil {
		return nil, err
	}
	riByName := make(map[string]*ripb.ResourceInstance)
	for _, ri := range app.GetResources().GetResourceInstances() {
		riByName[ri.GetName()] = ri
	}

	ris := make([]*ripb.ResourceInstance, 0, len(req.GetNames()))
	for _, name := range req.GetNames() {
		ri, found := riByName[name]
		if !found {
			return nil, status.Errorf(codes.NotFound, "instance %q not found", name)
		}
		ris = append(ris, ri)
	}

	rtrByIDV, err := s.batchGetRTRs(ctx, ris)
	if err != nil {
		return nil, err
	}

	instances := make([]*aigrpcpb.AssetInstance, 0, len(ris))
	for _, ri := range ris {
		rtr := rtrByIDV[ri.GetTypeIdVersion()]
		instances = append(instances, instanceconversion.AsView(instanceconversion.ConvertResourceInstanceToAssetInstance(ri, rtr), req.GetView()))
	}

	return &aigrpcpb.BatchGetAssetInstancesResponse{AssetInstances: instances}, nil
}

func (s *service) ListAssetInstances(ctx context.Context, req *aigrpcpb.ListAssetInstancesRequest) (*aigrpcpb.ListAssetInstancesResponse, error) {
	if view, err := viewOrDefault(req.GetView(), aigrpcpb.AssetInstanceView_ASSET_INSTANCE_VIEW_BASIC); err != nil {
		return nil, err
	} else {
		req.View = view
	}

	pageSize, err := resourceregistryutil.PageSize(req)
	if err != nil {
		return nil, err
	}

	startAfters, err := resourceregistryutil.StartAfters(paginatedInstance{req})
	if err != nil {
		return nil, err
	}

	app, _, err := s.getApplication(ctx)
	if err != nil {
		return nil, err
	}
	rtrByIDV, err := s.batchGetRTRs(ctx, app.GetResources().GetResourceInstances())
	if err != nil {
		return nil, err
	}

	instances := make([]*aigrpcpb.AssetInstance, 0, len(app.GetResources().GetResourceInstances()))
	for _, ri := range app.GetResources().GetResourceInstances() {
		rtr := rtrByIDV[ri.GetTypeIdVersion()]
		instances = append(instances, instanceconversion.ConvertResourceInstanceToAssetInstance(ri, rtr))
	}

	remove := func(i *aigrpcpb.AssetInstance) bool {
		return (slices.Compare(nameAsKey(i), startAfters) <= 0)
	}
	if accepted := multiFilterMatcher(req.GetStrictFilters()); accepted != nil {
		remove = func(i *aigrpcpb.AssetInstance) bool {
			return (slices.Compare(nameAsKey(i), startAfters) <= 0 || !accepted(i))
		}
	}
	instances = slices.DeleteFunc(instances, remove)
	slices.SortFunc(instances, func(lhs *aigrpcpb.AssetInstance, rhs *aigrpcpb.AssetInstance) int {
		return slices.Compare(nameAsKey(lhs), nameAsKey(rhs))
	})

	for i, instance := range instances {
		instances[i] = instanceconversion.AsView(instance, req.GetView())
	}

	if len(instances) <= int(pageSize) {
		return &aigrpcpb.ListAssetInstancesResponse{
			AssetInstances: instances,
		}, nil
	}

	instances = instances[:pageSize]
	paginatedInstance{req}.ClearPagination()

	nextPageToken, err := pagetoken.Opacify(req, "", nameAsKey(instances[pageSize-1]))
	if err != nil {
		return nil, status.Error(codes.Internal, "could not generate next page token")
	}

	return &aigrpcpb.ListAssetInstancesResponse{
		AssetInstances: instances,
		NextPageToken:  nextPageToken,
	}, nil
}

// Options are the values required for creating a new installed assets server.
type Options struct {
	AppClient asgrpcpb.HotSharedStateApplicationServiceClient
	RTRClient resourcetyperuntime.Client
}

// New creates a new implementation of an asset instances server using opts.
func New(opts Options) aigrpcpb.AssetInstancesServer {
	return &service{
		appClient: opts.AppClient,
		rtrClient: opts.RTRClient,
	}
}
