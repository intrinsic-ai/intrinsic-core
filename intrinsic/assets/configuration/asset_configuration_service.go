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

// Package assetconfigurationservice provides functionalities to help
// configure an Asset instance.
package assetconfigurationservice

import (
	"context"
	"maps"
	"slices"
	"strings"

	"intrinsic/assets/dependencies/traversal"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	acgrpcpb "intrinsic/assets/proto/v1/asset_configuration_go_proto"
	dependencypb "intrinsic/assets/proto/v1/dependency_go_proto"
	rccpb "intrinsic/assets/proto/v1/recommended_configuration_config_go_proto"
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"
	"intrinsic/assets/typeutils"
	rrpb "intrinsic/resources/proto/resource_registry_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	"intrinsic/resources/service/resourcereader"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/util/proto/names"
	"intrinsic/util/proto/registryutil"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/reflect/protoreflect"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	"google.golang.org/protobuf/types/known/anypb"
)

type assetConfigurationService struct {
	rr        *resourcereader.Client
	rtrClient resourcetyperuntime.Client
}

// Options contain options to configure the Asset Configuration Service
type Options struct {
	RRClient  *resourcereader.Client
	RTRClient resourcetyperuntime.Client
}

type assetInfo struct {
	rtr               *rtrpb.ResourceTypeRuntime
	rcc               *rccpb.RecommendedConfigurationConfig
	assetType         atpb.AssetType
	configMessageName protoreflect.FullName
	defaultConfig     *anypb.Any
}

func (s *assetConfigurationService) RecommendAssetConfiguration(ctx context.Context, req *acgrpcpb.RecommendAssetConfigurationRequest) (*acgrpcpb.RecommendAssetConfigurationResponse, error) {
	if req.GetName() == "" {
		return nil, status.Error(codes.InvalidArgument, "no name provided")
	}

	ris, rtrs, err := s.getAllResourcesAndInstances(ctx)
	if err != nil {
		return nil, err
	}

	return s.recommendAssetConfiguration(req, ris, rtrs)
}

func (s *assetConfigurationService) BatchRecommendAssetConfigurations(ctx context.Context, req *acgrpcpb.BatchRecommendAssetConfigurationsRequest) (*acgrpcpb.BatchRecommendAssetConfigurationsResponse, error) {
	if len(req.GetRequests()) == 0 {
		return &acgrpcpb.BatchRecommendAssetConfigurationsResponse{}, nil
	}

	for i, subReq := range req.GetRequests() {
		if subReq.GetName() == "" {
			return nil, status.Errorf(codes.InvalidArgument, "no name provided for request %d", i)
		}
	}

	// Fetch once for all requests in the batch.
	ris, rtrs, err := s.getAllResourcesAndInstances(ctx)
	if err != nil {
		return nil, err
	}

	responses := make([]*acgrpcpb.RecommendAssetConfigurationResponse, len(req.GetRequests()))
	for i, subReq := range req.GetRequests() {
		subResp, err := s.recommendAssetConfiguration(subReq, ris, rtrs)
		if err != nil {
			return nil, err
		}
		responses[i] = subResp
	}

	return &acgrpcpb.BatchRecommendAssetConfigurationsResponse{
		Responses: responses,
	}, nil
}

func (s *assetConfigurationService) recommendAssetConfiguration(req *acgrpcpb.RecommendAssetConfigurationRequest, ris map[string]*rrpb.ResourceInstance, rtrs map[string]*rtrpb.ResourceTypeRuntime) (*acgrpcpb.RecommendAssetConfigurationResponse, error) {
	name := req.GetName()
	info, err := assetInfoFromIdentifierPrefetched(name, ris, rtrs)
	if err != nil {
		return nil, err
	}

	// Services and Hardware Devices may not have a config. Throw an error if those Asset instances are
	// attempted to be configured with this service.
	if info.configMessageName == "" && (info.assetType == atpb.AssetType_ASSET_TYPE_SERVICE || info.assetType == atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE) {
		return nil, status.Errorf(codes.FailedPrecondition, "%q has no configuration message", name)
	}

	switch source := info.rcc.GetSource(); source {
	case rccpb.RecommendedConfigurationConfig_SOURCE_UNSPECIFIED, rccpb.RecommendedConfigurationConfig_SOURCE_PLATFORM_DEFAULT:
		config, err := s.getInitialConfig(req, info)
		if err != nil {
			return nil, err
		}
		res, err := platformDefaultResolution(config, info.rtr.GetMetadata().GetFileDescriptorSet(), ris, rtrs)
		if err != nil {
			return nil, err
		}
		return &acgrpcpb.RecommendAssetConfigurationResponse{
			Config: res,
		}, nil
	case rccpb.RecommendedConfigurationConfig_SOURCE_NO_RECOMMENDATION:
		return &acgrpcpb.RecommendAssetConfigurationResponse{
			Config: req.GetInputConfiguration(),
		}, nil
	default:
		return nil, status.Errorf(codes.Internal, "the Asset %q has an unsupported configuration source %q", name, source)
	}
}

func (s *assetConfigurationService) GetAssetRecommendationInfo(ctx context.Context, req *acgrpcpb.GetAssetRecommendationInfoRequest) (*acgrpcpb.AssetRecommendationInfo, error) {
	if req.GetName() == "" {
		return nil, status.Error(codes.InvalidArgument, "no name provided")
	}

	info, err := s.assetInfoFromIdentifier(ctx, req.GetName())
	if err != nil {
		return nil, err
	}

	switch source := info.rcc.GetSource(); source {
	case rccpb.RecommendedConfigurationConfig_SOURCE_UNSPECIFIED, rccpb.RecommendedConfigurationConfig_SOURCE_PLATFORM_DEFAULT:
		return &acgrpcpb.AssetRecommendationInfo{Name: req.GetName(), HasRecommendation: true}, nil
	case rccpb.RecommendedConfigurationConfig_SOURCE_NO_RECOMMENDATION:
		return &acgrpcpb.AssetRecommendationInfo{Name: req.GetName(), HasRecommendation: false}, nil
	default:
		return nil, status.Errorf(codes.Internal, "the Asset %q has an unsupported configuration source %q", req.GetName(), source)
	}
}

// getInitialConfig determines the starting configuration for asset recommendations.
// It uses the configuration from the request if provided. Otherwise, it falls back
// to the asset's default configuration. If no default exists, it returns an
// empty configuration.
func (s *assetConfigurationService) getInitialConfig(req *acgrpcpb.RecommendAssetConfigurationRequest, info *assetInfo) (*anypb.Any, error) {
	config := req.GetInputConfiguration()
	if config == nil && info.defaultConfig != nil {
		config = info.defaultConfig
	}

	if config == nil {
		// Create a new empty config message.
		types, err := registryutil.NewTypesFromFileDescriptorSet(info.rtr.GetMetadata().GetFileDescriptorSet())
		if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to create types from the file descriptor set: %v", err)
		}

		msgType, err := types.FindMessageByName(info.configMessageName)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to find message %q: %v", info.configMessageName, err)
		}

		config = &anypb.Any{}
		if err := config.MarshalFrom(msgType.New().Interface()); err != nil {
			return nil, status.Errorf(codes.Internal, "failed to marshal config message: %v", err)
		}
	} else if config.MessageName() != info.configMessageName {
		return nil, status.Errorf(codes.InvalidArgument, "input config message must be of type %q, but got %q", info.configMessageName, config.MessageName())
	}

	return config, nil
}

// getAllResourcesAndInstances fetches all resources and instances from the
// runtime DB, validates that no Asset ID has multiple installed versions, and
// returns:
// - ResourceInstances by instance name
// - ResourceTypeRuntimes by Asset ID
func (s *assetConfigurationService) getAllResourcesAndInstances(ctx context.Context) (map[string]*rrpb.ResourceInstance, map[string]*rtrpb.ResourceTypeRuntime, error) {
	ris, rtrsByIDV, err := s.rr.ResourceInstancesAsMap(
		ctx,
		true, // includeAllAssetTypes
	)
	if err != nil {
		return nil, nil, err
	}

	idToRTRs := make(map[string][]*rtrpb.ResourceTypeRuntime)
	for _, rtr := range rtrsByIDV {
		id := idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId())
		idToRTRs[id] = append(idToRTRs[id], rtr)
	}

	rtrsByID := make(map[string]*rtrpb.ResourceTypeRuntime, len(idToRTRs))
	for id, matchingRTRs := range idToRTRs {
		if len(matchingRTRs) > 1 {
			var idvs []string
			for _, rtr := range matchingRTRs {
				idvs = append(idvs, idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()))
			}
			return nil, nil, status.Errorf(codes.Internal, "found more than one Asset that could match %q: %v", id, strings.Join(idvs, ","))
		}
		rtrsByID[id] = matchingRTRs[0]
	}

	return ris, rtrsByID, nil
}

// assetInfoFromIdentifier returns the [assetInfo] for the provided Asset.
//
// An identifier for the Asset is required as an input to retrieve the asset info.
//
// For Services and Hardware Devices that are to be configured, their instance names in the solution
// are the expected identifiers.
//
// For Skills, the Asset IDs are the expected identifiers.
//
// Currently, no other Assets can be configured through this service, and will error out.
func (s *assetConfigurationService) assetInfoFromIdentifier(ctx context.Context, identifier string) (*assetInfo, error) {
	if idutils.IsID(identifier) {
		rtr, err := resourcetyperuntime.GetFromID(ctx, s.rtrClient, identifier)
		if err != nil {
			return nil, err
		}
		return skillAssetInfo(rtr)
	}

	// Identifier is a Service/HWD instance name.
	_, rtr, err := s.rr.ResourceInstance(ctx, identifier)
	if err != nil {
		return nil, err
	}
	return instanceAssetInfo(rtr), nil
}

// assetInfoFromIdentifierPrefetched returns the [assetInfo] for the provided
// Asset. Compared to [assetInfoFromIdentifier], it uses pre-fetched maps of
// resource instances and resource type runtimes for efficiency.
func assetInfoFromIdentifierPrefetched(id string, ris map[string]*rrpb.ResourceInstance, rtrs map[string]*rtrpb.ResourceTypeRuntime) (*assetInfo, error) {
	if idutils.IsID(id) {
		rtr, exists := rtrs[id]
		if !exists {
			return nil, status.Errorf(codes.NotFound, "requested id %q not found", id)
		}
		return skillAssetInfo(rtr)
	}

	// 'id' is a Service/HWD instance name.
	ri, exists := ris[id]
	if !exists {
		return nil, status.Errorf(codes.NotFound, "no Asset instance with name %q found", id)
	}
	assetID, err := idutils.RemoveVersionFrom(ri.GetTypeId())
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not remove version from %q: %v", ri.GetTypeId(), err)
	}
	rtr, exists := rtrs[assetID]
	if !exists {
		return nil, status.Errorf(codes.Internal, "could not find runtime info for Asset %q", ri.GetTypeId())
	}
	return instanceAssetInfo(rtr), nil
}

func skillAssetInfo(rtr *rtrpb.ResourceTypeRuntime) (*assetInfo, error) {
	if rtr.GetMetadata().GetAssetType() != atpb.AssetType_ASSET_TYPE_SKILL {
		id := idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId())
		return nil, status.Errorf(codes.InvalidArgument, "an Asset ID was provided in the request, but %q is not a Skill; use the instance name of the Asset instead", id)
	}

	switch v := rtr.GetVariant().(type) {
	case *rtrpb.ResourceTypeRuntime_Skill:
		return &assetInfo{
			rtr:               rtr,
			rcc:               v.Skill.GetDetails().GetOptions().GetRecommendedConfigurationConfig(),
			assetType:         atpb.AssetType_ASSET_TYPE_SKILL,
			configMessageName: protoreflect.FullName(v.Skill.GetDetails().GetParameter().GetMessageFullName()),
			defaultConfig:     v.Skill.GetDetails().GetParameter().GetDefaultValue(),
		}, nil
	default:
		return nil, status.Errorf(codes.Internal, "received unsupported Asset type %T when Skill was expected", rtr.GetVariant())
	}
}

func instanceAssetInfo(rtr *rtrpb.ResourceTypeRuntime) *assetInfo {
	return &assetInfo{
		rtr:               rtr,
		rcc:               rtr.GetServiceDef().GetRecommendedConfigurationConfig(),
		assetType:         rtr.GetMetadata().GetAssetType(),
		configMessageName: protoreflect.FullName(rtr.GetServiceDef().GetConfigMessageFullName()),
		defaultConfig:     rtr.GetDefaultConfiguration(),
	}
}

func platformDefaultResolution(config *anypb.Any, fds *descriptorpb.FileDescriptorSet, ris map[string]*rrpb.ResourceInstance, rtrs map[string]*rtrpb.ResourceTypeRuntime) (*anypb.Any, error) {
	if err := traversal.ForEachResolvedDependency(config, fds, func(annotations *dependencypb.Dependency, msg *rdpb.ResolvedDependency, _ traversal.FieldContext) (*rdpb.ResolvedDependency, error) {
		if msg == nil {
			msg = &rdpb.ResolvedDependency{}
		}
		if msg.GetName() != "" {
			return msg, nil
		}
		requires := annotations.GetRequires()
		requiresObject := annotations.GetRequiresObject() != nil
		if len(requires) == 0 && !requiresObject {
			return msg, nil
		}

		requiresData, requiresGRPC, err := parseRequirements(requires)
		if err != nil {
			return msg, err
		}

		if requiresData != "" && (len(requiresGRPC) > 0 || requiresObject) {
			return msg, status.Error(codes.InvalidArgument, "cannot mix data requirements with gRPC or object requirements")
		}

		var candidate string
		if requiresData != "" {
			candidate, err = findUniqueDataDependency(
				slices.Collect(maps.Values(rtrs)), requiresData,
			)
		} else {
			candidate, err = findUniqueInstanceDependency(
				ris, rtrs, requiresGRPC, requiresObject,
			)
		}

		if err != nil {
			return msg, err
		}

		msg.Name = candidate
		return msg, nil
	}); err != nil {
		return nil, err
	}

	return config, nil
}

// parseRequirements extracts and validates data and gRPC requirements from a list of requirement strings.
func parseRequirements(requires []string) (string, []string, error) {
	var requiresData string
	var requiresGRPC []string
	for _, r := range requires {
		if found := strings.HasPrefix(r, interfaceutils.DataURIPrefix); found {
			if requiresData != "" {
				return "", nil, status.Error(codes.Internal, "cannot have multiple data requirements")
			}
			requiresData = r
		} else if found := strings.HasPrefix(r, interfaceutils.GRPCURIPrefix); found {
			requiresGRPC = append(requiresGRPC, r)
		}
	}
	return requiresData, requiresGRPC, nil
}

// findUniqueDataDependency finds a Data Asset that satisfies a data requirement.
//
// A non-empty data dependency is returned only when there is a single Data Asset installed in the solution
// that satisfies the interface requirement.
func findUniqueDataDependency(rtrs []*rtrpb.ResourceTypeRuntime, requiresData string) (string, error) {
	var candidate string

	wantProtoName, _ := strings.CutPrefix(requiresData, interfaceutils.DataURIPrefix)
	for _, rtr := range rtrs {
		if rtr.GetData() == nil {
			continue
		}
		protoName, err := names.AnyToProtoName(rtr.GetData().GetData())
		if err != nil {
			return "", err
		}
		if protoName != wantProtoName {
			continue
		}

		// If the conditions are satisfied, this Data Asset is now a potential candidate for resolving
		// the provided data dependency.
		if candidate != "" {
			// Clear existing candidate since there are multiple possible dependency candidates.
			candidate = ""
			break
		}
		idp, err := idutils.NewIDVersionPartsFromProto(rtr.GetMetadata().GetIdVersion())
		if err != nil {
			return "", err
		}
		candidate = idp.ID()
	}
	return candidate, nil
}

// findUniqueInstanceDependency finds a unique resource instance that satisfies service and/or object requirements.
//
// A non-empty instance dependency is returned only when there is a single Asset instance in the solution
// that satisfies the interface requirement.
func findUniqueInstanceDependency(ris map[string]*rrpb.ResourceInstance, rtrs map[string]*rtrpb.ResourceTypeRuntime, requiresGRPC []string, requiresObject bool) (string, error) {
	var candidate string
	for instance, ri := range ris {
		assetID, err := idutils.RemoveVersionFrom(ri.GetTypeId())
		if err != nil {
			return "", status.Errorf(codes.Internal, "could not parse Asset ID from %q: %v", ri.GetTypeId(), err)
		}
		rtr, exists := rtrs[assetID]
		if !exists {
			return "", status.Errorf(codes.Internal, "failed to find runtime info for Asset type %q", ri.GetTypeId())
		}
		if !satisfiesInstanceDependencies(rtr, requiresGRPC, requiresObject) {
			continue
		}

		// If the conditions are satisfied, this instance is now a potential candidate for resolving
		// the provided dependencies.
		if candidate != "" {
			// Clear existing candidate since there are multiple possible dependency candidates.
			candidate = ""
			break
		}
		candidate = instance
	}
	return candidate, nil
}

// satisfiesInstanceDependencies checks if a resource type runtime satisfies the given dependency requirements for
// an Asset instance.
func satisfiesInstanceDependencies(rtr *rtrpb.ResourceTypeRuntime, requiresGRPC []string, requiresObject bool) bool {
	if requiresObject && !slices.Contains(typeutils.AssetTypesWithObjects(), rtr.GetMetadata().GetAssetType()) {
		return false
	}
	provides := rtr.GetMetadata().GetProvides()
	var grpcURIs []string
	for _, p := range provides {
		grpcURIs = append(grpcURIs, p.GetUri())
	}

	for _, r := range requiresGRPC {
		if !slices.Contains(grpcURIs, r) {
			return false
		}
	}
	return true
}

func New(opts Options) acgrpcpb.AssetConfigurationServiceServer {
	return &assetConfigurationService{
		rr:        opts.RRClient,
		rtrClient: opts.RTRClient,
	}
}
