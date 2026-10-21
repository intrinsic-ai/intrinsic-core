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

// Package main is the entrypoint for creating HardwareDevice Asset bundles.
package main

import (
	"context"
	"flag"

	"intrinsic/assets/hardware_devices/build_defs/hardwaredevicegen"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"

	log "github.com/golang/glog"
)

var (
	manifestPath      = flag.String("manifest", "", "Path to the HardwareDeviceManifest textproto file.")
	localAssetSpecs   = intrinsicflag.MultiString("local_asset", nil, "Spec of <asset_info_path>=<bundle_path> for a local asset to add to the manifest. Can be repeated.")
	catalogAssetPaths = intrinsicflag.MultiString("catalog_asset", nil, "Path to serialized AssetCatalogRefInfo proto for an asset to add to the manifest. Can be repeated.")
	outputBundlePath  = flag.String("output_bundle", "", "Output path for the .tar bundle.")
)

func main() {
	intrinsic.Init()

	ctx := context.Background()
	if err := hardwaredevicegen.CreateHardwareDeviceBundle(ctx, &hardwaredevicegen.CreateHardwareDeviceBundleOptions{
		ManifestPath:             *manifestPath,
		LocalAssetSpecs:          *localAssetSpecs,
		AssetCatalogRefInfoPaths: *catalogAssetPaths,
		OutputBundlePath:         *outputBundlePath,
	}); err != nil {
		log.Exitf("failed to create HardwareDevice Asset bundle: %v", err)
	}
}
