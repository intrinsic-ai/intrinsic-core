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

// Package localsolution constructs a LocalSolution proto from a set of assets
// and asset instances. It does some light validation.
package localsolution

import (
	"fmt"
	"maps"
	"regexp"
	"slices"

	"intrinsic/assets/idutils"
	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"

	"google.golang.org/protobuf/reflect/protoregistry"

	assetpb "intrinsic/assets/build_defs/asset_go_proto"
	icpb "intrinsic/assets/proto/v1/instance_config_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"
)

var validInstanceNameRegexp = regexp.MustCompile(`^[a-z]([a-z0-9_]*[a-z0-9])?$`)

func validateInstanceName(name string) error {
	if !validInstanceNameRegexp.MatchString(name) {
		return fmt.Errorf("instance name %q is invalid: name must start with a lowercase letter, must use only lowercase letters, numbers and underscores, and must not end with an underscore", name)
	}
	return nil
}

// Instance represents an asset instance configuration.
type Instance struct {
	Name               string
	Asset              string
	ConfigPath         string
	ConfigRunfilesPath string
}

// Options contains options for creating a LocalSolution proto.
type Options struct {
	AssetInfos           []*assetpb.AssetInfo
	AssetLocalInfos      []*assetpb.AssetLocalInfo
	AssetCatalogRefInfos []*assetpb.AssetCatalogRefInfo
	Instances            []*Instance
	ObjectWorldUpdates   []*owupb.ObjectWorldUpdate
	DefaultOperationMode opmodepb.OperationMode
	DisplayName          string
}

// New creates a new LocalSolution proto.
func New(opts Options) (*assetpb.LocalSolution, error) {
	types := map[string]*protoregistry.Types{}
	for _, ai := range opts.AssetInfos {
		id, err := idutils.IDFromProto(ai.GetId())
		if err != nil {
			return nil, fmt.Errorf("AssetInfo contains an invalid asset id: %v", err)
		}
		if ai.GetFileDescriptorSet() == nil {
			continue
		}
		t, err := registryutil.NewTypesFromFileDescriptorSet(ai.GetFileDescriptorSet())
		if err != nil {
			return nil, fmt.Errorf("cannot parse file descriptor set protos for %q: %v", id, err)
		}
		types[id] = t
	}

	assets := map[string]*assetpb.LocalSolution_Asset{}
	for _, la := range opts.AssetLocalInfos {
		asset, err := idutils.IDFromProto(la.GetId())
		if err != nil {
			return nil, fmt.Errorf("AssetLocalInfo contains an invalid asset id: %v", err)
		}
		if _, exists := assets[asset]; exists {
			return nil, fmt.Errorf("solution contains multiple %q assets", asset)
		}
		assets[asset] = &assetpb.LocalSolution_Asset{
			Variant: &assetpb.LocalSolution_Asset_Local{
				Local: la,
			},
		}
	}
	for _, ca := range opts.AssetCatalogRefInfos {
		asset, err := idutils.IDFromProto(ca.GetIdVersion().GetId())
		if err != nil {
			return nil, fmt.Errorf("AssetCatalogRefInfo contains an invalid asset id: %v", err)
		}
		if _, exists := assets[asset]; exists {
			return nil, fmt.Errorf("solution contains multiple %q assets", asset)
		}
		assets[asset] = &assetpb.LocalSolution_Asset{
			Variant: &assetpb.LocalSolution_Asset_Catalog{
				Catalog: ca,
			},
		}
	}

	seenNames := map[string]struct{}{}
	var instances []*assetpb.AssetInstanceInfo
	for _, inst := range opts.Instances {
		if err := validateInstanceName(inst.Name); err != nil {
			return nil, err
		}
		asset := inst.Asset
		if _, exists := assets[asset]; !exists {
			return nil, fmt.Errorf("solution contains instance with no corresponding asset type %q", asset)
		}
		if _, exists := seenNames[inst.Name]; exists {
			return nil, fmt.Errorf("solution contains multiple asset instances named %q", inst.Name)
		}

		// If the instance has a config then we parse it with the file descriptors
		// if available to confirm that it is valid for this asset at build time.
		var parsed *icpb.InstanceConfig
		if inst.ConfigPath != "" {
			if t, exists := types[asset]; exists {
				config := &icpb.InstanceConfig{}
				if err := protoio.ReadTextProto(inst.ConfigPath, config, protoio.WithResolver(t)); err != nil {
					return nil, fmt.Errorf("failed to parse asset configuration: %w", err)
				}
				parsed = config
			}
		}

		seenNames[inst.Name] = struct{}{}
		instances = append(instances, &assetpb.AssetInstanceInfo{
			Name:               inst.Name,
			Asset:              inst.Asset,
			ConfigRunfilesPath: inst.ConfigRunfilesPath,
			Parsed:             parsed,
		})
	}

	return &assetpb.LocalSolution{
		Assets:               slices.Collect(maps.Values(assets)),
		Instances:            instances,
		ObjectWorldUpdates:   opts.ObjectWorldUpdates,
		DefaultOperationMode: opts.DefaultOperationMode,
		DisplayName:          opts.DisplayName,
	}, nil
}
