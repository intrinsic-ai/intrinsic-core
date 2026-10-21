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

// Package hardwaredevicegen implements creation of a HardwareDevice Asset bundle.
package hardwaredevicegen

import (
	"context"
	"fmt"
	"os"
	"strings"

	"intrinsic/assets/hardware_devices/hardwaredevicebundle"
	"intrinsic/assets/hardware_devices/hardwaredevicefix"
	"intrinsic/assets/idutils"
	"intrinsic/util/proto/protoio"

	apb "intrinsic/assets/build_defs/asset_go_proto"
	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	rpb "intrinsic/assets/proto/v1/reference_go_proto"
)

// CreateHardwareDeviceBundleOptions provides the data needed to create a HardwareDevice Asset
// bundle.
type CreateHardwareDeviceBundleOptions struct {
	// AssetCatalogRefInfoPaths are the paths to serialized AssetCatalogRefInfo protos of assets to
	// add to the manifest.
	AssetCatalogRefInfoPaths []string
	// LocalAssetSpecs are "<asset_info_path>=<bundle_path>" specs for local assets to add to the
	// manifest.
	LocalAssetSpecs []string
	// Manifest is the path to a HardwareDeviceManifest .textproto file.
	ManifestPath string
	// OutputBundlePath is the output path for the tar bundle.
	OutputBundlePath string
}

// CreateHardwareDeviceBundle creates a HardwareDevice Asset bundle on disk.
func CreateHardwareDeviceBundle(ctx context.Context, opts *CreateHardwareDeviceBundleOptions) error {
	m := &hdmpb.HardwareDeviceManifest{}
	if err := protoio.ReadTextProto(opts.ManifestPath, m); err != nil {
		return fmt.Errorf("failed to read manifest: %w", err)
	}

	assets := m.GetAssets()
	if assets == nil {
		assets = map[string]*hdmpb.HardwareDeviceManifest_Asset{}
	}
	for _, path := range opts.AssetCatalogRefInfoPaths {
		info := &apb.AssetCatalogRefInfo{}
		if err := protoio.ReadBinaryProto(path, info); err != nil {
			return fmt.Errorf("failed to read AssetCatalogRefInfo: %w", err)
		}
		id := idutils.IDFromProtoUnchecked(info.GetIdVersion().GetId())
		if _, ok := assets[id]; ok {
			return fmt.Errorf("asset %s already exists in manifest", id)
		}
		assets[id] = &hdmpb.HardwareDeviceManifest_Asset{
			Variant: &hdmpb.HardwareDeviceManifest_Asset_Catalog{
				Catalog: &rpb.CatalogAsset{
					AssetType: info.GetAssetType(),
					IdVersion: info.GetIdVersion(),
				},
			},
		}
	}
	for _, spec := range opts.LocalAssetSpecs {
		parts := strings.SplitN(spec, "=", 2)
		if len(parts) != 2 {
			return fmt.Errorf("invalid local asset spec %q, expected <asset_info_path>=<bundle_path>", spec)
		}
		infoPath, bundlePath := parts[0], parts[1]
		info := &apb.AssetInfo{}
		if err := protoio.ReadBinaryProto(infoPath, info); err != nil {
			return fmt.Errorf("failed to read AssetInfo: %w", err)
		}
		id := idutils.IDFromProtoUnchecked(info.GetId())
		if _, ok := assets[id]; ok {
			return fmt.Errorf("asset %s already exists in manifest", id)
		}
		if _, err := os.Stat(bundlePath); err != nil {
			return fmt.Errorf("asset %s has invalid bundle path %q: %w", id, bundlePath, err)
		}
		assets[id] = &hdmpb.HardwareDeviceManifest_Asset{
			Variant: &hdmpb.HardwareDeviceManifest_Asset_Local{
				Local: &rpb.LocalAsset{
					AssetType:  info.GetAssetType(),
					Id:         info.GetId(),
					BundlePath: bundlePath,
				},
			},
		}
	}
	m.Assets = assets

	if err := hardwaredevicefix.Manifest(m, hardwaredevicefix.WithPopulateOldFields(true)); err != nil {
		return fmt.Errorf("unable to make manifest compatible with the latest version of the platform: %v", err)
	}
	if err := hardwaredevicebundle.WriteFile(ctx, m, opts.OutputBundlePath); err != nil {
		return fmt.Errorf("failed to write HardwareDevice Asset bundle: %w", err)
	}

	return nil
}
