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

// Package resourcereader contains functions that read resource instances from
// a currently running application.
package resourcereader

import (
	"context"
	"fmt"
	"slices"

	"intrinsic/assets/idutils"
	"intrinsic/assets/services/inspection"
	"intrinsic/resources/service/handles"
	"intrinsic/resources/service/resourcetyperuntime"

	"github.com/pkg/errors"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	rspb "intrinsic/config/proto/resource_set_go_proto"
	gridpb "intrinsic/resources/proto/geometric_resource_data_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rrpb "intrinsic/resources/proto/resource_registry_go_proto"
	rsdpb "intrinsic/resources/proto/resource_service_definition_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	socpb "intrinsic/scene/proto/v1/scene_object_config_go_proto"
	grpcrsspb "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service_go_proto"
	rsspb "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	ingressAddress = `istio-ingressgateway.app-ingress.svc.cluster.local:80`
)

// Client has methods to retrieve resource instances from the currently running
// application.
type Client struct {
	RSSClient grpcrsspb.HotSharedStateResourceSetServiceClient
	RTRClient resourcetyperuntime.Client
}

// serviceDefRequiresSchedulingConfig calculates from the a type's service
// definition whether or not it requires scheduling on a a real-time PC node to
// run.
func serviceDefRequiresSchedulingConfig(sd *rsdpb.ResourceServiceDefinition) bool {
	for _, image := range sd.GetRealSpec().GetImage() {
		if image.GetRequiresRtpcNode() {
			return true
		}
	}
	// Simulation needing this should probably be an error.  Or, in the
	// future, if we run both the sim and real specs simultaneously,
	// we should probably have two flags in the instance, or just have
	// the front-end calculate this directly.
	for _, image := range sd.GetSimSpec().GetImage() {
		if image.GetRequiresRtpcNode() {
			return true
		}
	}
	return false
}

// resourceFamilyIDFromAssetTag provides a conversion from an asset tag to a
// family ID string.  This may be lossy in the future if tags are not a strict
// subset of allowed families.
func resourceFamilyIDFromAssetTag(tag atagpb.AssetTag) string {
	switch tag {
	case atagpb.AssetTag_ASSET_TAG_CAMERA:
		return "cameras"
	case atagpb.AssetTag_ASSET_TAG_GRIPPER:
		return "gripper"
	default:
		return ""
	}
}

// convertResourceInstanceWithRuntime converts the internal ResourceInstance
// representation to the public ResourceInstance representation. The public
// proto hides some implementation details.
func convertResourceInstanceWithRuntime(internal *ripb.ResourceInstance, rtr *rtrpb.ResourceTypeRuntime) *rrpb.ResourceInstance {
	return &rrpb.ResourceInstance{
		Name:                     internal.GetName(),
		TypeId:                   idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()),
		Configuration:            internal.GetConfiguration(),
		SceneObjectConfig:        internal.GetSceneObjectConfig(),
		HasServices:              rtr.GetServiceDef() != nil,
		ResourceFamilyId:         resourceFamilyIDFromAssetTag(rtr.GetMetadata().GetAssetTag()),
		DataFiles:                internal.GetDataFiles(),
		RequiresSchedulingConfig: serviceDefRequiresSchedulingConfig(rtr.GetServiceDef()),
		ScheduledNodeHostname:    internal.GetSchedulingConfig().GetRequiredNodeHostname(),
	}
}

func makeGeometricResourceInstanceWithConfig(name string, rtr *rtrpb.ResourceTypeRuntime, config *anypb.Any, sceneObjectConfig *socpb.SceneObjectConfig) (*gridpb.GeometricResourceInstanceData, error) {
	return &gridpb.GeometricResourceInstanceData{
		Name:              name,
		WorldFragment:     rtr.GetWorldFragment(),
		SceneObject:       rtr.GetSceneObject(),
		Configuration:     config,
		SceneObjectConfig: sceneObjectConfig,
	}, nil
}

func (c *Client) resourceTypeRuntimesFromResourceSet(ctx context.Context, resourceSet *rspb.ResourceSet) (map[string]*rtrpb.ResourceTypeRuntime, error) {
	idVersionSet := make(map[string]struct{})
	for _, ri := range resourceSet.GetResourceInstances() {
		idVersionSet[ri.GetTypeIdVersion()] = struct{}{}
	}
	uniqueIDVersions := make([]string, 0, len(idVersionSet))
	for tv := range idVersionSet {
		uniqueIDVersions = append(uniqueIDVersions, tv)
	}

	rtrs := make(map[string]*rtrpb.ResourceTypeRuntime, len(uniqueIDVersions))
	for chunk := range slices.Chunk(uniqueIDVersions, 10) {
		chunkRtrs, err := c.RTRClient.BatchGet(ctx, chunk)
		if err != nil {
			return nil, fmt.Errorf("could not get resource runtime data: %w", err)
		}
		for _, rtr := range chunkRtrs {
			rtrs[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtr
		}
	}
	return rtrs, nil
}

func (c *Client) resourceTypeRuntimes(ctx context.Context) (map[string]*rtrpb.ResourceTypeRuntime, error) {
	installed, err := resourcetyperuntime.GetAll(ctx, c.RTRClient)
	if err != nil {
		return nil, fmt.Errorf("could not retrieve resource runtime data: %w", err)
	}
	rtrs := make(map[string]*rtrpb.ResourceTypeRuntime, len(installed))
	for _, rtr := range installed {
		rtrs[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtr
	}
	return rtrs, nil
}

// ResourceInstancesAsMap returns a map of resource instance names to their values.
// If includeAllAssetTypes is false, the ResourceTypeRuntime map includes only the
// entries needed to reconstruct each resource instance. If true, all entries are
// fetched from the runtime DB, which incurs a performance cost.
func (c *Client) ResourceInstancesAsMap(ctx context.Context, includeAllAssetTypes bool) (map[string]*rrpb.ResourceInstance, map[string]*rtrpb.ResourceTypeRuntime, error) {
	resp, err := c.RSSClient.GetCurrentResourceSet(ctx, &rsspb.GetCurrentResourceSetRequest{})
	if err != nil {
		// Don't wrap the error here, fully delegate it to the HSS service. It will set the correct gRPC
		// status code (covered by a unit test).
		return nil, nil, err
	}
	rs := resp.GetResourceSet()

	var rtrs map[string]*rtrpb.ResourceTypeRuntime
	if includeAllAssetTypes {
		rtrs, err = c.resourceTypeRuntimes(ctx)
	} else {
		rtrs, err = c.resourceTypeRuntimesFromResourceSet(ctx, rs)
	}

	ris := make(map[string]*rrpb.ResourceInstance, len(rs.GetResourceInstances()))
	for _, ri := range rs.GetResourceInstances() {
		rtr, exists := rtrs[ri.GetTypeIdVersion()]
		if !exists {
			return nil, nil, fmt.Errorf("could not find runtime info for resource type %q", ri.GetTypeIdVersion())
		}
		ris[ri.GetName()] = convertResourceInstanceWithRuntime(ri, rtr)
	}

	// Generate resource handles for every resource that has a service.
	for n, ri := range ris {
		rtr := rtrs[ri.GetTypeId()]
		rh, err := handles.Convert(ri, ingressAddress, rtr.GetServiceDef().GetServiceProtoPrefixes())
		if err != nil {
			return nil, nil, fmt.Errorf("could not retrieve resource handles for resource %q: %v", n, err)
		}
		ris[n].ResourceHandle = rh
		if topic, ok := inspection.Topic(n, rtr); ok {
			ris[n].ServiceInspectionTopic = &topic
		}
	}

	return ris, rtrs, nil
}

// ResourceInstance returns the ResourceInstance proto for the given id. Returns
// an error if the resource instance could not be found.
func (c *Client) ResourceInstance(ctx context.Context, name string) (*rrpb.ResourceInstance, *rtrpb.ResourceTypeRuntime, error) {
	var internal *ripb.ResourceInstance
	if resp, err := c.RSSClient.GetCurrentResourceSet(ctx, &rsspb.GetCurrentResourceSetRequest{}); err != nil {
		// Don't wrap the error here, fully delegate it to the HSS service. It will set the correct gRPC
		// status code (covered by a unit test).
		return nil, nil, err
	} else {
		for _, ri := range resp.GetResourceSet().GetResourceInstances() {
			if ri.GetName() == name {
				internal = ri
				break
			}
		}
	}
	if internal == nil {
		return nil, nil, status.Errorf(codes.NotFound, "no resource instance with name %q found", name)
	}

	rtr, err := c.RTRClient.Get(ctx, internal.GetTypeIdVersion())
	if err != nil {
		return nil, nil, fmt.Errorf("could not find runtime info for resource type %q: %v", internal.GetTypeIdVersion(), err)
	}
	ri := convertResourceInstanceWithRuntime(internal, rtr)

	// Generate the resource handle if the resource has a service.
	rh, err := handles.Convert(ri, ingressAddress, rtr.GetServiceDef().GetServiceProtoPrefixes())
	if err != nil {
		return nil, nil, fmt.Errorf("could not retrieve resource handles for resource %q: %v", name, err)
	}
	ri.ResourceHandle = rh

	if topic, ok := inspection.Topic(name, rtr); ok {
		ri.ServiceInspectionTopic = &topic
	}

	return ri, rtr, nil
}

// ExtractGeometricResourceInstanceData returns a list of
// GeometricResourceInstanceData for a given resource set and the installed
// types that it uses.
func ExtractGeometricResourceInstanceData(rs *rspb.ResourceSet, rtrs map[string]*rtrpb.ResourceTypeRuntime) ([]*gridpb.GeometricResourceInstanceData, error) {
	var rids []*gridpb.GeometricResourceInstanceData
	for _, instance := range rs.GetResourceInstances() {
		rtr, exists := rtrs[instance.GetTypeIdVersion()]
		if !exists {
			return nil, fmt.Errorf("could not find runtime info for resource %q", instance.GetTypeIdVersion())
		}
		if rtr.GetSceneObject() == nil && rtr.GetWorldFragment() == nil {
			continue
		}

		name := instance.GetName()
		ri := convertResourceInstanceWithRuntime(instance, rtr)
		rid, err := makeGeometricResourceInstanceWithConfig(name, rtr, ri.GetConfiguration(), instance.GetSceneObjectConfig())
		if err != nil {
			return nil, errors.Wrap(err, "makeGeometricInstanceWithConfig")
		}
		rids = append(rids, rid)
	}

	return rids, nil
}

// GeometricResourceInstanceData returns a list of
// GeometricResourceInstanceData proto for a given resource set.  Each proto
// holds the information needed to reason about the geometry associated a
// resource instance.  It attempts to pull the relevant type information from
// runtimedb.  If that is already available, then use
// ExtractGeometricResourceInstanceData.
func (c *Client) GeometricResourceInstanceData(ctx context.Context, rs *rspb.ResourceSet) ([]*gridpb.GeometricResourceInstanceData, error) {
	rtrs, err := c.resourceTypeRuntimesFromResourceSet(ctx, rs)
	if err != nil {
		return nil, fmt.Errorf("could not retrieve resource runtime data: %w", err)
	}

	return ExtractGeometricResourceInstanceData(rs, rtrs)
}

// GetGeometricResourceSetData returns all of the data needed to compose a world
// from the instances and the updates in the resource set.
func (c *Client) GetGeometricResourceSetData(ctx context.Context) ([]*gridpb.GeometricResourceInstanceData, *owupb.ObjectWorldUpdates, error) {
	response, err := c.RSSClient.GetCurrentResourceSet(ctx, &rsspb.GetCurrentResourceSetRequest{})
	if err != nil {
		return nil, nil, errors.Wrap(err, "GetCurrentCluster")
	}
	resourceSet := response.GetResourceSet()
	rtrs, err := c.resourceTypeRuntimesFromResourceSet(ctx, resourceSet)
	if err != nil {
		return nil, nil, fmt.Errorf("could not retrieve resource runtime data: %w", err)
	}

	rids, err := ExtractGeometricResourceInstanceData(resourceSet, rtrs)
	if err != nil {
		return nil, nil, errors.Wrap(err, "ResourceInstanceDataAsMap")
	}

	return rids, resourceSet.GetObjectWorldUpdates(), nil
}

// NewClientOpts holds options for NewClient.
type NewClientOpts struct {
	RSSClient grpcrsspb.HotSharedStateResourceSetServiceClient
	RTRClient resourcetyperuntime.Client
}

// NewClient returns a new resource reader client with the given options.
func NewClient(opts NewClientOpts) *Client {
	return &Client{
		RSSClient: opts.RSSClient,
		RTRClient: opts.RTRClient,
	}
}
