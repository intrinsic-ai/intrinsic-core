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

// Package bundle contains utils for working with Asset bundles.
package bundle

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"

	"intrinsic/assets/data/databundle"
	"intrinsic/assets/hardware_devices/hardwaredevicebundle"
	"intrinsic/assets/imageutils"
	"intrinsic/assets/ioutils"
	"intrinsic/assets/processes/processbundle"
	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/scene_objects/sceneobjectbundle"
	"intrinsic/assets/services/servicebundle"
	"intrinsic/skills/skillbundle"
	"intrinsic/util/proto/descriptor"

	"google.golang.org/protobuf/proto"

	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	rmpb "intrinsic/assets/catalog/proto/v1/release_metadata_go_proto"
	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	processassetpb "intrinsic/assets/processes/proto/process_asset_go_proto"
	assettagpb "intrinsic/assets/proto/asset_tag_go_proto"
	assettypepb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	processedassetpb "intrinsic/assets/proto/v1/processed_asset_go_proto"
	sompb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"
	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	apppb "intrinsic/config/proto/application_go_proto" 
	psmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
)

const (
	// LINT.IfChange(data_bundle_file)
	dataAssetFileName = "data_asset.binpb"
	// LINT.ThenChange(//intrinsic/assets/data/databundle.go:bundle_file)
	// LINT.IfChange(hardware_device_bundle_file)
	hardwareDeviceManifestFileName = "hardware_device_manifest.binpb"
	// LINT.ThenChange(//intrinsic/assets/hardware_devices/hardwaredevicebundle.go:bundle_file)
	// LINT.IfChange(process_bundle_file)
	processManifestFileName = "process_manifest.binpb"
	// LINT.ThenChange(//intrinsic/assets/processes/processbundle.go:bundle_file)
	// LINT.IfChange(scene_object_bundle_file)
	sceneObjectManifestPathInTar = "scene_object_manifest.binarypb"
	// LINT.ThenChange(//intrinsic/assets/scene_objects/sceneobjectbundle.go:bundle_file)
	// LINT.IfChange(service_bundle_file)
	serviceManifestPathInTar = "service_manifest.binarypb"
	// LINT.ThenChange(//intrinsic/assets/services/servicebundle.go:bundle_file)
	// LINT.IfChange(skill_bundle_file)
	skillManifestPathInTar = "skill_manifest.binpb"
	// LINT.ThenChange(//intrinsic/skills/skillbundle.go:bundle_file)
)

// bundleType is used to return the type of a bundle file.
type bundleType int

// The different bundle types that can be detected from a file.
const (
	bundleTypeData bundleType = iota
	bundleTypeHardwareDevice
	bundleTypeProcess
	bundleTypeSceneObject
	bundleTypeService
	bundleTypeSkill
)

var (
	errNoValidTypeDetected   = errors.New("no recognized manifest detected")
	errMultipleTypesDetected = errors.New("invalid bundle")

	// ErrMissingProvider is returned when a CatalogFileDescriptorProvider is required to fetch
	// catalog references (such as in a hardware device bundle) but none was provided.
	ErrMissingProvider = errors.New("missing catalog file descriptor provider")
)

// detectBundleType will return the type of bundle a file represents.  It does
// not do any validation of the particular file, just provides an indication
// what sort of processing should be done on the file.
func detectBundleType(ctx context.Context, path string) (bundleType, error) {
	f, err := os.Open(path)
	if err != nil {
		return 0, fmt.Errorf("could not open %q: %v", path, err)
	}
	defer f.Close()

	lookup := map[string]bundleType{
		dataAssetFileName:              bundleTypeData,
		hardwareDeviceManifestFileName: bundleTypeHardwareDevice,
		processManifestFileName:        bundleTypeProcess,
		sceneObjectManifestPathInTar:   bundleTypeSceneObject,
		serviceManifestPathInTar:       bundleTypeService,
		skillManifestPathInTar:         bundleTypeSkill,
	}

	var bt bundleType
	var found int
	if err := ioutils.WalkTarFile(ctx, f, ioutils.WithFallbackHandler(func(_ context.Context, path string, _ io.Reader) error {
		if val, ok := lookup[path]; ok {
			found++
			bt = val
		}
		return nil
	})); err != nil {
		return bt, err
	}
	switch found {
	case 0:
		return 0, errNoValidTypeDetected
	case 1:
		return bt, nil
	default:
		return 0, errMultipleTypesDetected
	}
}

// Processor provides a way to process bundles of arbitrary types.  The
// processors are specific to a particular target (i.e. cluster or catalog) and
// should be for use across many bundles.
type Processor struct {
	imageutils.ImageProcessor
	gzfprocessor.GZFProcessor
	// ReferencedDataProcessor is the referenceddata.Processor to use for Data assets (see
	// ReadDataBundle).
	ReferencedDataProcessor referenceddata.Processor
}

// VersionDetails provides the specific details about a version when it is
// released to the catalog.
type VersionDetails struct {
	Version         string
	ReleaseNotes    string
	ReleaseMetadata *rmpb.ReleaseMetadata
}

// CatalogFileDescriptorProvider provides file descriptor sets for catalog assets.
type CatalogFileDescriptorProvider interface {
	BatchGet(ctx context.Context, idVersions []*idpb.IdVersion) ([]*dpb.FileDescriptorSet, error)
}

// ProcessedBundle is a bundle that has been processed and can be viewed as a
// message for use in different outbound requests.
type ProcessedBundle interface {
	// Install returns a processed asset in the form required to be installed in
	// a solution.
	Install() *iapb.CreateInstalledAssetRequest_Asset

	// Install returns a processed asset in the form required to be released to
	// the catalog.
	Release(VersionDetails) *acpb.Asset

	// Solution returns a processed asset in the form required to be included in
	// a solution.
	Solution() *assetpb.Asset


	// DeployApp returns a processed asset in the form required to be included in
	// an Application proto when deploying a solution.
	DeployApp() *apppb.Application_Asset


	// FileDescriptorSet may return nil for asset types that do not have a file
	// descriptor set or do not have one available from the given information.
	// The implementation may use provider to fetch descriptor sets for referenced
	// catalog assets. If provider is required, but is nil, then the implementation
	// returns ErrMissingProvider.
	FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error)
}

type processedDataBundle struct {
	da *dapb.DataAsset
}

func (b processedDataBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_Data{
			Data: cloneOf(b.da),
		},
	}
}

func (b processedDataBundle) Release(details VersionDetails) *acpb.Asset {
	da := cloneOf(b.da)
	m := cloneOf(da.GetMetadata())
	m.IdVersion.Version = details.Version
	m.ReleaseNotes = details.ReleaseNotes
	return &acpb.Asset{
		Metadata:        m,
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_DataSpecificDeploymentData{
				DataSpecificDeploymentData: &acpb.Asset_DataDeploymentData{
					Data: da,
				},
			},
		},
	}
}

func (b processedDataBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_Data{
					Data: cloneOf(b.da),
				},
			},
		},
	}
}


func (b processedDataBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_Data{
			Data: cloneOf(b.da),
		},
	}
}



func (b processedDataBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	return cloneOf(b.da.GetFileDescriptorSet()), nil
}

type processedHardwareDeviceBundle struct {
	manifest *hdmpb.ProcessedHardwareDeviceManifest
}

func (b processedHardwareDeviceBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_HardwareDevice{
			HardwareDevice: cloneOf(b.manifest),
		},
	}
}

func (b processedHardwareDeviceBundle) Release(details VersionDetails) *acpb.Asset {
	manifest := cloneOf(b.manifest)

	// Take the first tag, if one exists.  Validation can be done later on the
	// deployment data.
	var tag assettagpb.AssetTag
	if len(manifest.GetMetadata().GetAssetTags()) > 1 {
		tag = manifest.GetMetadata().GetAssetTags()[0]
	}
	return &acpb.Asset{
		Metadata: &metadatapb.Metadata{
			IdVersion: &idpb.IdVersion{
				Id:      manifest.GetMetadata().GetId(),
				Version: details.Version,
			},
			AssetType:     assettypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE,
			AssetTag:      tag,
			DisplayName:   manifest.GetMetadata().GetDisplayName(),
			Documentation: manifest.GetMetadata().GetDocumentation(),
			Vendor:        manifest.GetMetadata().GetVendor(),
			ReleaseNotes:  details.ReleaseNotes,
		},
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_HardwareDeviceSpecificDeploymentData{
				HardwareDeviceSpecificDeploymentData: &acpb.Asset_HardwareDeviceDeploymentData{
					Manifest: manifest,
				},
			},
		},
	}
}

func (b processedHardwareDeviceBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_HardwareDevice{
					HardwareDevice: cloneOf(b.manifest),
				},
			},
		},
	}
}

func (b processedHardwareDeviceBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	if b.manifest == nil {
		return nil, nil
	}

	var fdss []*dpb.FileDescriptorSet
	var mergeKeys []string
	var catalogIdVersions []*idpb.IdVersion
	var catalogKeys []string
	for k, asset := range b.manifest.GetAssets() {
		switch v := asset.GetVariant().(type) {
		case *hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Service:
			fdss = append(fdss, v.Service.GetAssets().GetFileDescriptorSet())
			mergeKeys = append(mergeKeys, k)
		case *hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_SceneObject:
			fdss = append(fdss, v.SceneObject.GetAssets().GetFileDescriptorSet())
			mergeKeys = append(mergeKeys, k)
		case *hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Data:
			fdss = append(fdss, v.Data.GetFileDescriptorSet())
			mergeKeys = append(mergeKeys, k)
		case *hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Catalog:
			catalogIdVersions = append(catalogIdVersions, v.Catalog.GetIdVersion())
			catalogKeys = append(catalogKeys, k)
		}
	}

	if len(catalogIdVersions) > 0 {
		if provider == nil {
			return nil, ErrMissingProvider
		}
		catalogFDSs, err := provider.BatchGet(ctx, catalogIdVersions)
		if err != nil {
			return nil, fmt.Errorf("failed to get catalog file descriptor sets: %w", err)
		}
		if len(catalogFDSs) != len(catalogIdVersions) {
			return nil, fmt.Errorf("provider returned %d file descriptor sets, expected %d", len(catalogFDSs), len(catalogIdVersions))
		}
		fdss = append(fdss, catalogFDSs...)
		mergeKeys = append(mergeKeys, catalogKeys...)
	}

	merged, err := descriptor.MergeFileDescriptorSets(fdss, descriptor.WithKeys(mergeKeys))
	if err != nil {
		return nil, fmt.Errorf("failed to merge file descriptor sets: %w", err)
	}
	if len(merged.GetFile()) == 0 {
		return nil, nil
	}
	return merged, nil
}


func (b processedHardwareDeviceBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_HardwareDevice{
			HardwareDevice: cloneOf(b.manifest),
		},
	}
}



type processedProcessBundle struct {
	pa *processassetpb.ProcessAsset
}

func (b processedProcessBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_Process{
			Process: cloneOf(b.pa),
		},
	}
}

func (b processedProcessBundle) Release(details VersionDetails) *acpb.Asset {
	pa := cloneOf(b.pa)
	m := cloneOf(pa.GetMetadata())
	m.IdVersion.Version = details.Version
	m.ReleaseNotes = details.ReleaseNotes

	return &acpb.Asset{
		Metadata:        m,
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_ProcessSpecificDeploymentData{
				ProcessSpecificDeploymentData: &acpb.Asset_ProcessDeploymentData{
					Process: pa,
				},
			},
		},
	}
}

func (b processedProcessBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_Process{
					Process: cloneOf(b.pa),
				},
			},
		},
	}
}


func (b processedProcessBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_Process{
			Process: cloneOf(b.pa),
		},
	}
}



func (b processedProcessBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	return nil, nil
}

type processedSceneObjectBundle struct {
	manifest *sompb.ProcessedSceneObjectManifest
}

func (b processedSceneObjectBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_SceneObject{
			SceneObject: cloneOf(b.manifest),
		},
	}
}

func (b processedSceneObjectBundle) Release(details VersionDetails) *acpb.Asset {
	manifest := cloneOf(b.manifest)
	return &acpb.Asset{
		Metadata: &metadatapb.Metadata{
			IdVersion: &idpb.IdVersion{
				Id:      manifest.GetMetadata().GetId(),
				Version: details.Version,
			},
			AssetType:     assettypepb.AssetType_ASSET_TYPE_SCENE_OBJECT,
			DisplayName:   manifest.GetMetadata().GetDisplayName(),
			Documentation: manifest.GetMetadata().GetDocumentation(),
			Vendor:        manifest.GetMetadata().GetVendor(),
			AssetTag:      manifest.GetMetadata().GetAssetTag(),
			ReleaseNotes:  details.ReleaseNotes,
		},
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_SceneObjectSpecificDeploymentData{
				SceneObjectSpecificDeploymentData: &acpb.Asset_SceneObjectDeploymentData{
					Manifest: manifest,
				},
			},
		},
	}
}

func (b processedSceneObjectBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_SceneObject{
					SceneObject: cloneOf(b.manifest),
				},
			},
		},
	}
}


func (b processedSceneObjectBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_SceneObject{
			SceneObject: cloneOf(b.manifest),
		},
	}
}



func (b processedSceneObjectBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	return cloneOf(b.manifest.GetAssets().GetFileDescriptorSet()), nil
}

type processedServiceBundle struct {
	manifest *smpb.ProcessedServiceManifest
}

func (b processedServiceBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_Service{
			Service: cloneOf(b.manifest),
		},
	}
}

func (b processedServiceBundle) Release(details VersionDetails) *acpb.Asset {
	manifest := cloneOf(b.manifest)
	return &acpb.Asset{
		Metadata: &metadatapb.Metadata{
			IdVersion: &idpb.IdVersion{
				Id:      manifest.GetMetadata().GetId(),
				Version: details.Version,
			},
			AssetType:     assettypepb.AssetType_ASSET_TYPE_SERVICE,
			DisplayName:   manifest.GetMetadata().GetDisplayName(),
			Documentation: manifest.GetMetadata().GetDocumentation(),
			Vendor:        manifest.GetMetadata().GetVendor(),
			AssetTag:      manifest.GetMetadata().GetAssetTag(),
			ReleaseNotes:  details.ReleaseNotes,
		},
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_ServiceSpecificDeploymentData{
				ServiceSpecificDeploymentData: &acpb.Asset_ServiceDeploymentData{
					Manifest: manifest,
				},
			},
		},
	}
}

func (b processedServiceBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_Service{
					Service: cloneOf(b.manifest),
				},
			},
		},
	}
}


func (b processedServiceBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_Service{
			Service: cloneOf(b.manifest),
		},
	}
}



func (b processedServiceBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	return cloneOf(b.manifest.GetAssets().GetFileDescriptorSet()), nil
}

type processedSkillBundle struct {
	manifest *psmpb.ProcessedSkillManifest
}

func (b processedSkillBundle) Install() *iapb.CreateInstalledAssetRequest_Asset {
	return &iapb.CreateInstalledAssetRequest_Asset{
		Variant: &iapb.CreateInstalledAssetRequest_Asset_Skill{
			Skill: cloneOf(b.manifest),
		},
	}
}

func (b processedSkillBundle) Release(details VersionDetails) *acpb.Asset {
	manifest := cloneOf(b.manifest)
	return &acpb.Asset{
		Metadata: &metadatapb.Metadata{
			IdVersion: &idpb.IdVersion{
				Id:      manifest.GetMetadata().GetId(),
				Version: details.Version,
			},
			AssetType:     assettypepb.AssetType_ASSET_TYPE_SKILL,
			DisplayName:   manifest.GetMetadata().GetDisplayName(),
			Documentation: manifest.GetMetadata().GetDocumentation(),
			Vendor:        manifest.GetMetadata().GetVendor(),
			ReleaseNotes:  details.ReleaseNotes,
		},
		ReleaseMetadata: details.ReleaseMetadata,
		DeploymentData: &acpb.Asset_AssetDeploymentData{
			AssetSpecificDeploymentData: &acpb.Asset_AssetDeploymentData_SkillSpecificDeploymentData{
				SkillSpecificDeploymentData: &acpb.Asset_SkillDeploymentData{
					Manifest: manifest,
				},
			},
		},
	}
}

func (b processedSkillBundle) Solution() *assetpb.Asset {
	return &assetpb.Asset{
		Source: &assetpb.Asset_Local{
			Local: &processedassetpb.ProcessedAsset{
				Variant: &processedassetpb.ProcessedAsset_Skill{
					Skill: cloneOf(b.manifest),
				},
			},
		},
	}
}


func (b processedSkillBundle) DeployApp() *apppb.Application_Asset {
	return &apppb.Application_Asset{
		Variant: &apppb.Application_Asset_Skill{
			Skill: cloneOf(b.manifest),
		},
	}
}



func (b processedSkillBundle) FileDescriptorSet(ctx context.Context, provider CatalogFileDescriptorProvider) (*dpb.FileDescriptorSet, error) {
	return cloneOf(b.manifest.GetAssets().GetFileDescriptorSet()), nil
}

// Process auto-detects a bundle type and processes it to be sent to an
// appropriate target.
func (p *Processor) ProcessFile(ctx context.Context, path string) (ProcessedBundle, error) {
	bundleType, err := detectBundleType(ctx, path)
	if err != nil {
		return nil, fmt.Errorf("failed to detect bundle type: %w", err)
	}
	switch bundleType {
	case bundleTypeData:
		da, err := databundle.ProcessFile(ctx, path,
			databundle.WithReadOptions(databundle.WithReferencedDataProcessor(p.ReferencedDataProcessor)),
		)
		if err != nil {
			return nil, fmt.Errorf("failed to process Data Asset bundle: %w", err)
		}
		return processedDataBundle{da}, nil
	case bundleTypeHardwareDevice:
		assetInliner := hardwaredevicebundle.NewLocalAssetInliner(hardwaredevicebundle.LocalAssetInlinerOptions{
			ImageProcessor:          p.ImageProcessor,
			ReferencedDataProcessor: p.ReferencedDataProcessor,
			GZFProcessor:            p.GZFProcessor,
		})

		localAssetsDir, err := os.MkdirTemp("", "local-assets")
		if err != nil {
			return nil, fmt.Errorf("failed create temporary directory for local Assets: %w", err)
		}
		defer os.RemoveAll(localAssetsDir)

		hardwareDevice, err := hardwaredevicebundle.ProcessFile(ctx, path,
			hardwaredevicebundle.WithProcessAsset(assetInliner.Process),
			hardwaredevicebundle.WithReadOptions(hardwaredevicebundle.WithExtractLocalAssetsDir(localAssetsDir)),
		)
		if err != nil {
			return nil, fmt.Errorf("failed to process HardwareDevice bundle: %w", err)
		}
		return &processedHardwareDeviceBundle{hardwareDevice}, nil
	case bundleTypeProcess:
		process, err := processbundle.ProcessFile(ctx, path)
		if err != nil {
			return nil, fmt.Errorf("failed to process Process bundle: %w", err)
		}
		return processedProcessBundle{process}, nil

	case bundleTypeSceneObject:
		sceneObject, err := sceneobjectbundle.ProcessFile(ctx, path,
			sceneobjectbundle.WithGZFProcessor(p.GZFProcessor),
		)
		if err != nil {
			return nil, fmt.Errorf("failed to process SceneObject bundle: %w", err)
		}
		return &processedSceneObjectBundle{sceneObject}, nil

	case bundleTypeService:
		service, err := servicebundle.ProcessFile(ctx, path,
			servicebundle.WithImageProcessor(p.ImageProcessor),
		)
		if err != nil {
			return nil, fmt.Errorf("failed to process Service bundle: %w", err)
		}
		return processedServiceBundle{service}, nil
	case bundleTypeSkill:
		skill, err := skillbundle.ProcessFile(ctx, path,
			skillbundle.WithImageProcessor(p.ImageProcessor),
		)
		if err != nil {
			return nil, fmt.Errorf("failed to process Skill bundle: %w", err)
		}
		return processedSkillBundle{skill}, nil
	default:
		return nil, fmt.Errorf("unknown bundle type: %v", bundleType)
	}
}

// cloneOf clones a proto message while using generics to avoid a cast.

// cloneOf is the same generic API as proto.CloneOf, which is in v2 of the
// proto package and not currently available externally.

func cloneOf[M proto.Message](m M) M {
	return proto.Clone(m).(M)
}
