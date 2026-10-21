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

// Package runtimegraph constructs a reduced representation of a Solution from runtime information
// for dependency validation.
package runtimegraph

import (
	"context"
	"fmt"

	"intrinsic/assets/dependencies/graph"
	"intrinsic/assets/dependencies/graphutils"
	"intrinsic/assets/dependencies/runtimeasset"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	"intrinsic/executive/go/behaviortree"
	"intrinsic/util/proto/names"

	log "github.com/golang/glog"

	processassetpb "intrinsic/assets/processes/proto/process_asset_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	aipb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

type solutionContextOpts struct {
	includePlatformRuntime bool
}

// SolutionContextOption is an option for configuring NewSolutionContext.
type SolutionContextOption func(*solutionContextOpts)

// WithPlatformRuntime adds the Intrinsic Runtime Asset (and its instance) to the SolutionContext.
func WithPlatformRuntime() SolutionContextOption {
	return func(opts *solutionContextOpts) {
		opts.includePlatformRuntime = true
	}
}

// NewSolutionContext parses runtime information and returns a populated graph.SolutionContext.
func NewSolutionContext(ctx context.Context, ris []*ripb.ResourceInstance, rtrs []*rtrpb.ResourceTypeRuntime, options ...SolutionContextOption) (*graph.SolutionContext, error) {
	opts := &solutionContextOpts{}
	for _, opt := range options {
		opt(opts)
	}

	assets, err := assetsFromRuntime(ctx, rtrs)
	if err != nil {
		return nil, err
	}
	instances, err := instancesFromRuntime(assets, ris)
	if err != nil {
		return nil, err
	}

	sc := &graph.SolutionContext{
		Assets:    assets,
		Instances: instances,
	}

	if opts.includePlatformRuntime {
		asset, err := assetFromInstalledAsset(ctx, runtimeasset.InstalledAsset())
		if err != nil {
			return nil, err
		}
		sc.Assets[asset.ID] = asset
		inst := instanceFromAssetInstance(runtimeasset.AssetInstance())
		sc.Instances[inst.Name] = inst
	}

	return sc, nil
}

func instanceFromAssetInstance(ai *aipb.AssetInstance) *graph.InstanceContext {
	var config *anypb.Any
	if cfg := ai.GetConfig(); cfg != nil {
		if service := cfg.GetService(); service != nil {
			config = service.GetServiceConfig()
		} else if hw := cfg.GetHardwareDevice(); hw != nil && hw.GetService() != nil {
			config = hw.GetService().GetServiceConfig()
		}
	}
	return &graph.InstanceContext{
		Asset:  ai.GetAsset(),
		Config: config,
		Name:   ai.GetName(),
	}
}

func assetFromInstalledAsset(ctx context.Context, ia *iapb.InstalledAsset) (*graph.AssetContext, error) {
	info := &graph.AssetContext{
		ID:           ia.GetName(),
		ProvidesURIs: make(map[string]bool),
	}
	if md := ia.GetMetadata(); md != nil {
		info.AssetType = md.GetAssetType()
		info.FileDescriptorSet = md.GetFileDescriptorSet()
		info.Version = md.GetIdVersion().GetVersion()
		for _, p := range md.GetProvides() {
			info.ProvidesURIs[p.GetUri()] = true
		}
	}
	if processData := ia.GetDeploymentData().GetProcess(); processData != nil {
		references, err := referencesFromProcess(ctx, processData.GetProcess())
		if err != nil {
			return nil, err
		}
		info.References = references
	}
	return info, nil
}

func assetsFromRuntime(ctx context.Context, rtrs []*rtrpb.ResourceTypeRuntime) (map[string]*graph.AssetContext, error) {
	assets := make(map[string]*graph.AssetContext)
	for _, rtr := range rtrs {
		info, err := processRuntimeAsset(ctx, rtr)
		if err != nil {
			return nil, err
		}
		assets[info.ID] = info
	}
	return assets, nil
}

func instancesFromRuntime(assets map[string]*graph.AssetContext, ris []*ripb.ResourceInstance) (map[string]*graph.InstanceContext, error) {
	instances := make(map[string]*graph.InstanceContext)
	for _, ri := range ris {
		id, err := idutils.RemoveVersionFrom(ri.GetTypeIdVersion())
		if err != nil {
			return nil, fmt.Errorf("invalid type_id_version for instance %q: %w", ri.GetName(), err)
		}
		if _, ok := assets[id]; !ok {
			return nil, fmt.Errorf("asset %q for instance %q not found", id, ri.GetName())
		}

		instances[ri.GetName()] = &graph.InstanceContext{
			Asset:  id,
			Config: ri.GetConfiguration(),
			Name:   ri.GetName(),
		}
	}
	return instances, nil
}

// processRuntimeAsset extracts metadata and capability information from a ResourceTypeRuntime.
func processRuntimeAsset(ctx context.Context, rtr *rtrpb.ResourceTypeRuntime) (*graph.AssetContext, error) {
	if rtr.GetMetadata().GetIdVersion().GetId() == nil {
		return nil, fmt.Errorf("failed to retrieve ID from resource type runtime")
	}
	id, err := idutils.IDFromProto(rtr.GetMetadata().GetIdVersion().GetId())
	if err != nil {
		return nil, fmt.Errorf("invalid asset ID: %w", err)
	}
	info := &graph.AssetContext{
		ID:      id,
		Version: rtr.GetMetadata().GetIdVersion().GetVersion(),
	}

	assetType := rtr.GetMetadata().GetAssetType()
	info.AssetType = assetType

	switch assetType {
	case atypepb.AssetType_ASSET_TYPE_DATA:
		if rtr.GetData() != nil && rtr.GetData().GetData() != nil {
			protoName, err := names.AnyToProtoName(rtr.GetData().GetData())
			if err != nil {
				return nil, err
			}
			info.ProvidesURIs = map[string]bool{interfaceutils.DataURIPrefix + protoName: true}
		}

	case atypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE, atypepb.AssetType_ASSET_TYPE_SERVICE:
		if serviceDef := rtr.GetServiceDef(); serviceDef != nil {
			provides, err := graphutils.ExtractGRPCInterfacesFromPrefixes(serviceDef.GetServiceProtoPrefixes())
			if err != nil {
				return nil, err
			}
			info.FileDescriptorSet = rtr.GetMetadata().GetFileDescriptorSet()
			info.ConfigMessageName = serviceDef.GetConfigMessageFullName()
			info.ProvidesURIs = provides
		}

	case atypepb.AssetType_ASSET_TYPE_PROCESS:
		references, err := referencesFromProcess(ctx, rtr.GetProcess())
		if err != nil {
			return nil, err
		}
		info.References = references
	case atypepb.AssetType_ASSET_TYPE_SCENE_OBJECT:
	case atypepb.AssetType_ASSET_TYPE_SKILL:
		if skill := rtr.GetSkill(); skill != nil {
			info.FileDescriptorSet = rtr.GetMetadata().GetFileDescriptorSet()
			info.ConfigMessageName = skill.GetDetails().GetParameter().GetMessageFullName()
		}
	case atypepb.AssetType_ASSET_TYPE_UNSPECIFIED:
		// TODO(b/517345754): Currently just log instead of returning an error.
		// Solutions with legacy pose estimators have an unspecified Asset type.
		log.WarningContextf(ctx, "unspecified Asset type for %q: %v", id, assetType)
	default:
		return nil, fmt.Errorf("unknown Asset type for %q: %v", id, assetType)
	}

	return info, nil
}

func referencesFromProcess(ctx context.Context, process *processassetpb.ProcessAsset) (map[string]graph.ReferencedAsset, error) {
	var usages map[string][]graph.AssetUsage

	tree := process.GetBehaviorTree()
	if tree != nil {
		visitor := &processAssetUsageVisitor{
			usages: make(map[string][]graph.AssetUsage),
		}
		if err := behaviortree.Walk(ctx, tree, visitor, behaviortree.VisitCalledTreeState()); err != nil {
			return nil, fmt.Errorf("failed to walk behavior tree: %w", err)
		}
		usages = visitor.usages
	}

	references := make(map[string]graph.ReferencedAsset)
	// Add entries for all declared Assets from the map contained in the Asset.
	// These are the Assets that the author of the Process manually (or with
	// tooling) specified.
	for assetID, reference := range process.GetAssets() {
		var declaredVersion string
		if reference.GetCatalog() != nil {
			declaredVersion = reference.GetCatalog().GetIdVersion().GetVersion()
		}
		references[assetID] = graph.ReferencedAsset{
			Declared:        true,
			DeclaredVersion: declaredVersion,
			// This could be an empty list of usages if an Asset is explicitly
			// referenced but not actually used.
			Usages: usages[assetID],
		}
	}
	// Add any Assets that have usages but aren't in the explicit map. This would
	// be Assets that either have no source to pull from (e.g., sideloaded) or
	// that were forgotten.
	for assetID, usages := range usages {
		if _, ok := references[assetID]; ok {
			continue
		}
		references[assetID] = graph.ReferencedAsset{
			Usages: usages,
		}
	}

	return references, nil
}

type processAssetUsageVisitor struct {
	usages map[string][]graph.AssetUsage
}

func (v *processAssetUsageVisitor) Visit(ctx context.Context, element behaviortree.VisitElement) error {
	node := element.Node()
	if node == nil {
		return nil
	}

	if call := node.GetTask().GetCallBehavior(); call != nil {
		assetID := call.GetSkillId()
		v.usages[assetID] = append(v.usages[assetID], graph.AssetUsage{
			SourceDescription: fmt.Sprintf("Skill node %q", node.GetName()),
			Configuration:     call.GetParameters(),
		})
	}

	return nil
}
