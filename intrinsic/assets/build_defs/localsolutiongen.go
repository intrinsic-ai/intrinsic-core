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

// localsolutiongen creates a LocalSolution proto which contains all assets in
// a solution.
package main

import (
	"flag"
	"maps"
	"slices"
	"strings"

	"intrinsic/assets/build_defs/localsolution"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"
	"intrinsic/util/proto/protoio"

	log "github.com/golang/glog"

	assetpb "intrinsic/assets/build_defs/asset_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"
)

var opModeLookup = map[string]opmodepb.OperationMode{
	"":     opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED,
	"sim":  opmodepb.OperationMode_SIMULATION,
	"real": opmodepb.OperationMode_REAL_HARDWARE,
}

var (
	assets                 = intrinsicflag.MultiString("assets", nil, "Path to serialized AssetInfo protos. Can be repeated.")
	localAssets            = intrinsicflag.MultiString("local_assets", nil, "Spec of <asset_info_path>=<bundle_runfiles_path> for local assets. Can be repeated.")
	catalogAssets          = intrinsicflag.MultiString("catalog_assets", nil, "Path to serialized AssetCatalogRefInfo protos. Can be repeated.")
	instanceSpecs          = intrinsicflag.MultiString("instances", nil, "Spec of <name>=<asset>[=<config_path>=<config_runfiles_path>] for asset instances. Can be repeated.")
	objectWorldUpdateFiles = intrinsicflag.MultiString("object_world_updates", nil, "Path to object world updates pbtxt file. Can be repeated.")
	defaultOperationMode   = flag.String("default_operation_mode", "", "Operation mode of the app. Can be 'sim' or 'real'.")
	output                 = flag.String("output", "", "Output LocalSolution proto path.")
	displayName            = flag.String("display_name", "", "Display name of the PPR component.")
)

func main() {
	intrinsic.Init()

	assetInfos := make([]*assetpb.AssetInfo, len(*assets))
	for idx, p := range *assets {
		assetInfo := new(assetpb.AssetInfo)
		if err := protoio.ReadBinaryProto(p, assetInfo); err != nil {
			log.Exitf("failed to read AssetInfo proto at %q: %v", p, err)
		}
		assetInfos[idx] = assetInfo
	}

	localAssetInfos := make([]*assetpb.AssetLocalInfo, len(*localAssets))
	for idx, spec := range *localAssets {
		parts := strings.SplitN(spec, "=", 2)
		if len(parts) != 2 {
			log.Exitf("invalid --local_assets format %q, expected <asset_info_path>=<bundle_runfiles_path>", spec)
		}
		assetInfo := new(assetpb.AssetInfo)
		if err := protoio.ReadBinaryProto(parts[0], assetInfo); err != nil {
			log.Exitf("failed to read AssetInfo proto at %q: %v", parts[0], err)
		}
		localAssetInfos[idx] = &assetpb.AssetLocalInfo{
			AssetType:          assetInfo.GetAssetType(),
			Id:                 assetInfo.GetId(),
			BundleRunfilesPath: parts[1],
		}
	}

	assetCatalogRefInfos := make([]*assetpb.AssetCatalogRefInfo, len(*catalogAssets))
	for idx, p := range *catalogAssets {
		assetCatalogRefInfo := new(assetpb.AssetCatalogRefInfo)
		if err := protoio.ReadBinaryProto(p, assetCatalogRefInfo); err != nil {
			log.Exitf("failed to read AssetCatalogRefInfo proto at %q: %v", p, err)
		}
		assetCatalogRefInfos[idx] = assetCatalogRefInfo
	}

	instances := make([]*localsolution.Instance, len(*instanceSpecs))
	for idx, spec := range *instanceSpecs {
		parts := strings.Split(spec, "=")
		if len(parts) != 2 && len(parts) != 4 {
			log.Exitf("invalid --instances format %q, expected <name>=<asset> or <name>=<asset>=<config_path>=<config_runfiles_path>", spec)
		}
		var configPath, configRunfilesPath string
		if len(parts) == 4 {
			configPath = parts[2]
			configRunfilesPath = parts[3]
		}
		instances[idx] = &localsolution.Instance{
			Name:               parts[0],
			Asset:              parts[1],
			ConfigPath:         configPath,
			ConfigRunfilesPath: configRunfilesPath,
		}
	}

	var objectWorldUpdates []*owupb.ObjectWorldUpdate
	for _, p := range *objectWorldUpdateFiles {
		// prototext.UnmarshalOptions doesn't support Merge like
		// proto.UnmarshalOptions does, otherwise we wouldn't need this
		// temporary.
		owu := &owupb.ObjectWorldUpdates{}
		if err := protoio.ReadTextProto(p, owu); err != nil {
			log.Exitf("Failed to read object world updates: %v", err)
		}

		// TODO(b/243124435): Remove this field once ObjectWorldUpdates have fully replaced WorldUpdates.
		if len(owu.GetEntityUpdates()) != 0 {
			log.Exitf("Entity world updates found in %q, but are not supported", p)
		}

		objectWorldUpdates = append(objectWorldUpdates, owu.GetUpdates()...)
	}

	defaultOpMode, ok := opModeLookup[*defaultOperationMode]
	if !ok {
		log.Exitf("Cannot set operation mode to %q (valid values are %q)", *defaultOperationMode, slices.Collect(maps.Values(opModeLookup)))
	}

	assets, err := localsolution.New(localsolution.Options{
		AssetInfos:           assetInfos,
		AssetLocalInfos:      localAssetInfos,
		AssetCatalogRefInfos: assetCatalogRefInfos,
		Instances:            instances,
		ObjectWorldUpdates:   objectWorldUpdates,
		DefaultOperationMode: defaultOpMode,
		DisplayName:          *displayName,
	})
	if err != nil {
		log.Exitf("could not create LocalSolution proto: %v", err)
	}
	if err := protoio.WriteBinaryProto(*output, assets, protoio.WithDeterministic(true)); err != nil {
		log.Exitf("Failed to write solution proto: %v", err)
	}
}
