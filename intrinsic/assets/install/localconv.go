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

// Package localconv contains asset to runtime data conversion functions.
package localconv

import (
	"context"

	"intrinsic/assets/conversion/runtime"
	"intrinsic/assets/idutils"
	"intrinsic/assets/metadatautils"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	dpb "intrinsic/assets/proto/documentation_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	iopb "intrinsic/assets/proto/installation_origin_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	mpb "intrinsic/assets/proto/metadata_go_proto"
	processedassetpb "intrinsic/assets/proto/v1/processed_asset_go_proto"
	vpb "intrinsic/assets/proto/vendor_go_proto"
	applicationpb "intrinsic/config/proto/application_go_proto"
	rtpb "intrinsic/resources/proto/resource_type_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
)

const (
	// errComponent is used to indicate the origin of extended status errors
	errComponent = "ai.intrinsic.installed_assets_service"
)

// CatalogAssetToRuntime converts a catalog Asset to a ResourceTypeRuntime message.
func CatalogAssetToRuntime(ctx context.Context, a *acpb.Asset, options ...runtime.ToRuntimeOption) (*rtrpb.ResourceTypeRuntime, error) {
	options = append([]runtime.ToRuntimeOption{
		runtime.WithInstallationOrigin(iopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG),
		runtime.WithVersion(a.GetMetadata().GetIdVersion().GetVersion()),
	}, options...)

	switch v := a.GetDeploymentData().GetAssetSpecificDeploymentData().(type) {
	case *acpb.Asset_AssetDeploymentData_DataSpecificDeploymentData:
		// TODO(449824543): Remove this once the AssetCatalog santizes old Data.
		da := v.DataSpecificDeploymentData.GetData()
		da.Metadata = metadatautils.ToInAssetMetadata(da.GetMetadata())
		return runtime.DataToRuntime(ctx, da, options...)
	case *acpb.Asset_AssetDeploymentData_ProcessSpecificDeploymentData:
		// TODO(449824543): Remove this once the AssetCatalog santizes old Processes.
		pa := v.ProcessSpecificDeploymentData.GetProcess()
		pa.Metadata = metadatautils.ToInAssetMetadata(pa.GetMetadata())
		return runtime.ProcessToRuntime(ctx, pa, options...)
	case *acpb.Asset_AssetDeploymentData_SceneObjectSpecificDeploymentData:
		return runtime.SceneObjectToRuntime(ctx, v.SceneObjectSpecificDeploymentData.GetManifest(), options...)
	case *acpb.Asset_AssetDeploymentData_ServiceSpecificDeploymentData:
		return runtime.ServiceToRuntime(ctx, v.ServiceSpecificDeploymentData.GetManifest(), options...)
	case *acpb.Asset_AssetDeploymentData_SkillSpecificDeploymentData:
		return runtime.SkillToRuntime(ctx, v.SkillSpecificDeploymentData.GetManifest(), options...)
	case *acpb.Asset_AssetDeploymentData_HardwareDeviceSpecificDeploymentData:
		return runtime.HardwareDeviceToRuntime(ctx, v.HardwareDeviceSpecificDeploymentData.GetManifest(), options...)
	case nil:
		return nil, status.Errorf(codes.InvalidArgument, "unspecified deployment data")
	default:
		return nil, status.Errorf(codes.InvalidArgument, "invalid deployment data type: %T", v)
	}
}

// ProcessedAssetToRuntime converts a processed asset to a ResourceTypeRuntime.
func ProcessedAssetToRuntime(ctx context.Context, asset *processedassetpb.ProcessedAsset, options ...runtime.ToRuntimeOption) (*rtrpb.ResourceTypeRuntime, error) {
	options = append(options,
		runtime.WithInstallationOrigin(iopb.InstallationOrigin_INSTALLATION_ORIGIN_SIDELOADED),
	)

	switch v := asset.GetVariant().(type) {
	case *processedassetpb.ProcessedAsset_Data:
		return runtime.DataToRuntime(ctx, v.Data, options...)
	case *processedassetpb.ProcessedAsset_HardwareDevice:
		return runtime.HardwareDeviceToRuntime(ctx, v.HardwareDevice, options...)
	case *processedassetpb.ProcessedAsset_Process:
		return runtime.ProcessToRuntime(ctx, v.Process, options...)
	case *processedassetpb.ProcessedAsset_SceneObject:
		return runtime.SceneObjectToRuntime(ctx, v.SceneObject, options...)
	case *processedassetpb.ProcessedAsset_Service:
		return runtime.ServiceToRuntime(ctx, v.Service, options...)
	case *processedassetpb.ProcessedAsset_Skill:
		return runtime.SkillToRuntime(ctx, v.Skill, options...)
	case nil:
		return nil, status.Errorf(codes.InvalidArgument, "asset variant is unspecified")
	default:
		return nil, status.Errorf(codes.InvalidArgument, "unknown asset variant %T in the processed asset", v)
	}
}

// CatalogResourceToRuntime converts a catalog ResourceType to a ResourcetypeRuntime message.
func CatalogResourceToRuntime(rt *rtpb.ResourceType) *rtrpb.ResourceTypeRuntime {
	if rt == nil {
		return nil
	}
	return &rtrpb.ResourceTypeRuntime{
		Metadata:             assetMetadataFromResourceType(rt),
		InstallationOrigin:   iopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG,
		ServiceDef:           rt.GetServiceDef(),
		DefaultConfiguration: rt.GetDefaultConfiguration(),
		WorldFragment:        rt.GetWorldFragment(),
		SceneObject:          rt.GetSceneObject(),
	}
}

// LocalInstalledAssetToRuntime converts a local installed asset (from a request) to a
// ResourceTypeRuntime.
func LocalInstalledAssetToRuntime(ctx context.Context, asset *iapb.CreateInstalledAssetRequest_Asset, options ...runtime.ToRuntimeOption) (*rtrpb.ResourceTypeRuntime, error) {
	options = append(options,
		runtime.WithInstallationOrigin(iopb.InstallationOrigin_INSTALLATION_ORIGIN_SIDELOADED),
	)

	switch v := asset.GetVariant().(type) {
	case *iapb.CreateInstalledAssetRequest_Asset_Data:
		return runtime.DataToRuntime(ctx, v.Data, options...)
	case *iapb.CreateInstalledAssetRequest_Asset_HardwareDevice:
		return runtime.HardwareDeviceToRuntime(ctx, v.HardwareDevice, options...)
	case *iapb.CreateInstalledAssetRequest_Asset_Process:
		return runtime.ProcessToRuntime(ctx, v.Process, options...)
	case *iapb.CreateInstalledAssetRequest_Asset_Service:
		return runtime.ServiceToRuntime(ctx, v.Service, options...)
	case *iapb.CreateInstalledAssetRequest_Asset_SceneObject:
		return runtime.SceneObjectToRuntime(ctx, v.SceneObject, options...)
	case *iapb.CreateInstalledAssetRequest_Asset_Skill:
		return runtime.SkillToRuntime(ctx, v.Skill, options...)
	case nil:
		return nil, status.Errorf(codes.InvalidArgument, "unspecified variant type")
	default:
		return nil, status.Errorf(codes.InvalidArgument, "invalid variant type: %T", v)
	}
}

// LocalSolutionAssetToRuntime converts a local solution asset to a ResourceTypeRuntime.
func LocalSolutionAssetToRuntime(ctx context.Context, asset *applicationpb.Application_Asset, options ...runtime.ToRuntimeOption) (*rtrpb.ResourceTypeRuntime, error) {
	options = append(options,
		runtime.WithInstallationOrigin(iopb.InstallationOrigin_INSTALLATION_ORIGIN_SIDELOADED),
	)

	switch v := asset.GetVariant().(type) {
	case *applicationpb.Application_Asset_Data:
		return runtime.DataToRuntime(ctx, v.Data, options...)
	case *applicationpb.Application_Asset_HardwareDevice:
		return runtime.HardwareDeviceToRuntime(ctx, v.HardwareDevice, options...)
	case *applicationpb.Application_Asset_Process:
		return runtime.ProcessToRuntime(ctx, v.Process, options...)
	case *applicationpb.Application_Asset_SceneObject:
		return runtime.SceneObjectToRuntime(ctx, v.SceneObject, options...)
	case *applicationpb.Application_Asset_Service:
		return runtime.ServiceToRuntime(ctx, v.Service, options...)
	case *applicationpb.Application_Asset_Skill:
		return runtime.SkillToRuntime(ctx, v.Skill, options...)
	case nil:
		return nil, status.Errorf(codes.InvalidArgument, "asset variant is unspecified")
	default:
		return nil, status.Errorf(codes.InvalidArgument, "invalid asset type in the application")
	}
}

// assetTagFromResourceFamilyID provides a best-effort conversion to asset
// tags.  This is lossy as the set of tags does not cover all allowed families
// for resources.
func assetTagFromResourceFamilyID(family string) atagpb.AssetTag {
	switch family {
	case "other":
		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED
	case "cameras":
		return atagpb.AssetTag_ASSET_TAG_CAMERA
	case "gripper":
		return atagpb.AssetTag_ASSET_TAG_GRIPPER
	default:
		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED
	}
}

// assetTypeFromResourceType determines the appropriate asset type enum for a
// resource type.  Ambiguous cases where a resource has both or neither a
// service are undefined.
func assetTypeFromResourceType(rt *rtpb.ResourceType) atpb.AssetType {
	return assetType(
		rt.GetServiceDef() != nil,
		rt.GetWorldFragment() != nil || rt.GetSceneObject() != nil,
	)
}

func assetType(hasService, hasSceneObject bool) atpb.AssetType {
	if hasService && hasSceneObject {
		return atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE
	}
	if hasService {
		return atpb.AssetType_ASSET_TYPE_SERVICE
	}
	if hasSceneObject {
		return atpb.AssetType_ASSET_TYPE_SCENE_OBJECT
	}
	return atpb.AssetType_ASSET_TYPE_UNSPECIFIED
}

func idVersionProtoFromResourceType(rt *rtpb.ResourceType) *idpb.IdVersion {
	var idVersion *idpb.IdVersion
	id := rt.GetId()
	v := rt.GetMetadata().GetVersion().GetVersion()
	ivp, ivpErr := idutils.NewIDVersionParts(rt.GetIdVersion())
	if id != nil || v != "" || ivpErr == nil {
		idVersion = &idpb.IdVersion{
			Id:      id,
			Version: v,
		}
		if id == nil && ivpErr == nil {
			idVersion.Id = ivp.IDProto()
		}
		if v == "" && ivpErr == nil {
			idVersion.Version = ivp.Version()
		}
	}

	return idVersion
}

// assetMetadataFromResourceType constructs an asset Metadata proto from the
// available fields in the resource type.
func assetMetadataFromResourceType(rt *rtpb.ResourceType) *mpb.Metadata {
	if rt == nil {
		return nil
	}

	var vendor *vpb.Vendor
	if v := rt.GetVendor(); v != "" {
		vendor = &vpb.Vendor{
			DisplayName: v,
		}
	}

	var documentation *dpb.Documentation
	if doc := rt.GetMetadata().GetDocs(); doc != "" {
		documentation = &dpb.Documentation{
			Description: doc,
		}
	}
	return &mpb.Metadata{
		IdVersion:         idVersionProtoFromResourceType(rt),
		DisplayName:       rt.GetMetadata().GetDisplayName(),
		Vendor:            vendor,
		Documentation:     documentation,
		ReleaseNotes:      rt.GetMetadata().GetVersion().GetReleaseNotes(),
		UpdateTime:        rt.GetMetadata().GetUpdateTime(),
		AssetType:         assetTypeFromResourceType(rt),
		AssetTag:          assetTagFromResourceFamilyID(rt.GetResourceFamily().GetId()),
		FileDescriptorSet: rt.GetParameterDescriptorFileset(),
	}
}
