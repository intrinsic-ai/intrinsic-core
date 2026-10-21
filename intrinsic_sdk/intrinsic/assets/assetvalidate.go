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

// Package assetvalidate provides utils for validating Assets.
package assetvalidate

import (
	"context"
	"fmt"

	"intrinsic/assets/data/datavalidate"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/hardware_devices/hardwaredevicevalidate"
	"intrinsic/assets/idutils"
	"intrinsic/assets/processes/processvalidate"
	"intrinsic/assets/scene_objects/sceneobjectvalidate"
	"intrinsic/assets/services/servicevalidate"
	"intrinsic/skills/skillvalidate"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	processedassetpb "intrinsic/assets/proto/v1/processed_asset_go_proto"
	rpb "intrinsic/assets/proto/v1/reference_go_proto"
)

type assetOptions struct {
	dataOptions           []datavalidate.DataAssetOption
	hardwareDeviceOptions []hardwaredevicevalidate.ProcessedHardwareDeviceManifestOption
	processOptions        []processvalidate.ProcessAssetOption
	report                *report.Report
	sceneObjectOptions    []sceneobjectvalidate.ProcessedSceneObjectManifestOption
	serviceOptions        []servicevalidate.ProcessedServiceManifestOption
	skillOptions          []skillvalidate.ProcessedSkillManifestOption
}

// AssetOption is an option for validating an Asset.
type AssetOption func(*assetOptions)

// WithDataOptions appends options to use for validating Data Assets.
//
// Options will also apply to Data Assets in HardwareDevices.
func WithDataOptions(options ...datavalidate.DataAssetOption) AssetOption {
	return func(opts *assetOptions) {
		opts.dataOptions = append(opts.dataOptions, options...)
	}
}

// WithHardwareDeviceOptions appends options to use for validating HardwareDevices.
func WithHardwareDeviceOptions(options ...hardwaredevicevalidate.ProcessedHardwareDeviceManifestOption) AssetOption {
	return func(opts *assetOptions) {
		opts.hardwareDeviceOptions = append(opts.hardwareDeviceOptions, options...)
	}
}

// WithProcessOptions appends options to use for validating Processes.
func WithProcessOptions(options ...processvalidate.ProcessAssetOption) AssetOption {
	return func(opts *assetOptions) {
		opts.processOptions = append(opts.processOptions, options...)
	}
}

// WithSceneObjectOptions appends options to use for validating SceneObjects.
//
// Options will also apply to SceneObjects in HardwareDevices.
func WithSceneObjectOptions(options ...sceneobjectvalidate.ProcessedSceneObjectManifestOption) AssetOption {
	return func(opts *assetOptions) {
		opts.sceneObjectOptions = append(opts.sceneObjectOptions, options...)
	}
}

// WithServiceOptions appends options to use for validating Services.
//
// Options will also apply to Services in HardwareDevices.
func WithServiceOptions(options ...servicevalidate.ProcessedServiceManifestOption) AssetOption {
	return func(opts *assetOptions) {
		opts.serviceOptions = append(opts.serviceOptions, options...)
	}
}

func WithSkillOptions(options ...skillvalidate.ProcessedSkillManifestOption) AssetOption {
	return func(opts *assetOptions) {
		opts.skillOptions = append(opts.skillOptions, options...)
	}
}

// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) AssetOption {
	return func(opts *assetOptions) {
		opts.report = report
		WithDataOptions(datavalidate.WithReport(report))(opts)
		WithHardwareDeviceOptions(hardwaredevicevalidate.WithReport(report))(opts)
		WithProcessOptions(processvalidate.WithReport(report))(opts)
		WithSceneObjectOptions(sceneobjectvalidate.WithReport(report))(opts)
		WithServiceOptions(servicevalidate.WithReport(report))(opts)
		WithSkillOptions(skillvalidate.WithReport(report))(opts)
	}
}

// Asset validates an Asset.
func Asset(ctx context.Context, asset *assetpb.Asset, options ...AssetOption) error {
	opts := &assetOptions{}
	WithReport(report.New())(opts)
	for _, opt := range options {
		opt(opts)
	}

	if asset == nil {
		return fmt.Errorf("Asset must not be nil")
	}

	switch src := asset.GetSource().(type) {
	case *assetpb.Asset_Catalog:
		return catalogAsset(src.Catalog)
	case *assetpb.Asset_Local:
		switch v := src.Local.GetVariant().(type) {
		case *processedassetpb.ProcessedAsset_Data:
			return datavalidate.DataAsset(ctx, v.Data, opts.dataOptions...)
		case *processedassetpb.ProcessedAsset_HardwareDevice:
			hwdOpts := append([]hardwaredevicevalidate.ProcessedHardwareDeviceManifestOption{
				hardwaredevicevalidate.WithDataAssetOptions(opts.dataOptions...),
				hardwaredevicevalidate.WithSceneObjectOptions(opts.sceneObjectOptions...),
				hardwaredevicevalidate.WithServiceOptions(opts.serviceOptions...),
			}, opts.hardwareDeviceOptions...)

			return hardwaredevicevalidate.ProcessedHardwareDeviceManifest(ctx, v.HardwareDevice, hwdOpts...)
		case *processedassetpb.ProcessedAsset_Process:
			return processvalidate.ProcessAsset(ctx, v.Process, opts.processOptions...)
		case *processedassetpb.ProcessedAsset_SceneObject:
			return sceneobjectvalidate.ProcessedSceneObjectManifest(ctx, v.SceneObject, opts.sceneObjectOptions...)
		case *processedassetpb.ProcessedAsset_Service:
			return servicevalidate.ProcessedServiceManifest(ctx, v.Service, opts.serviceOptions...)
		case *processedassetpb.ProcessedAsset_Skill:
			return skillvalidate.ProcessedSkillManifest(ctx, v.Skill, opts.skillOptions...)
		default:
			return fmt.Errorf("unknown local Asset variant: %T", v)
		}
	default:
		return fmt.Errorf("unknown Asset source %T", src)
	}
}

func catalogAsset(ca *rpb.CatalogAsset) error {
	if ca.GetAssetType() == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
		return fmt.Errorf("asset type must be specified")
	}

	if err := idutils.ValidateIDVersionProto(ca.GetIdVersion()); err != nil {
		return fmt.Errorf("invalid id version: %w", err)
	}

	return nil
}
