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

// Package viewutils provides utilities for working with asset views.
package viewutils

import (
	"errors"
	"fmt"
	"slices"
	"strings"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"

	dataassetpb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	hardwaredevicemanifestpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	processassetpb "intrinsic/assets/processes/proto/process_asset_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	processedassetpb "intrinsic/assets/proto/v1/processed_asset_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
	sceneobjectmanifestpb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"
	servicemanifestpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	processedskillmanifestpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
)

var (
	metadataFieldName              = "metadata"
	assetSpecificMetadataOneOfName = "asset_specific_metadata"
	deploymentDataFieldName        = "deployment_data"

	errInvalidAssetType = errors.New("invalid asset type")
)

// Asset is an interface for constructing asset views.
type Asset interface {
	proto.Message

	GetMetadata() *metadatapb.Metadata
}

type customViewValue struct {
	// Field is the name of the field to set.
	Field string
	// Value is a function that returns the value to set.
	Value func() (proto.Message, error)
	// Views is the list of views for which the value should be set.
	Views []viewpb.AssetViewType
}

type fDeploymentData func() (proto.Message, error)

type fFileDescriptorSet func() (*dpb.FileDescriptorSet, error)

type assetToViewOptions struct {
	// CustomViewValues are custom values to set for some asset views.
	CustomViewValues []customViewValue

	// AllDeploymentData returns the deployment data for the asset for the ALL view.
	//
	// This option is a function rather than a message so the value doesn't need to be computed unless
	// it is actually needed.
	//
	// If provided, the returned deployment data is used to populate the deployment data. If omitted,
	// then the returned asset view will not have deployment data.
	AllDeploymentData fDeploymentData

	// FullDeploymentData returns the deployment data for the asset for the FULL view.
	//
	// This option is a function rather than a message so the value doesn't need to be computed unless
	// it is actually needed.
	//
	// If provided, the returned deployment data is used to populate the deployment data. If omitted,
	// then the returned asset view will not have deployment data.
	FullDeploymentData fDeploymentData
	// FileDescriptorSet returns a FileDescriptorSet for the asset metadata.
	//
	// This option is a function rather than a message so the value doesn't need to be computed unless
	// it is actually needed.
	//
	// If provided, the returned FileDescriptorSet is used to populate the metadata instead of the
	// FileDescriptorSet from the input metadata.
	FileDescriptorSet fFileDescriptorSet
	// Metadata is the metadata to use for the asset view, instead of the input metadata.
	Metadata *metadatapb.Metadata
}

// AssetToViewOption is an option for AssetToView.
type AssetToViewOption func(*assetToViewOptions)

// WithCustomViewValue returns an AssetToViewOption that specifies a custom value to set for some
// asset views.
func WithCustomViewValue(field string, views []viewpb.AssetViewType, value func() (proto.Message, error)) AssetToViewOption {
	return func(opts *assetToViewOptions) {
		opts.CustomViewValues = append(opts.CustomViewValues, customViewValue{
			Field: field,
			Value: value,
			Views: views,
		})
	}
}



// WithAllDeploymentData returns an AssetToViewOption that specifies a function to compute the
// DeploymentData for the ALL view.
func WithAllDeploymentData(dd fDeploymentData) AssetToViewOption {
	return func(opts *assetToViewOptions) {
		opts.AllDeploymentData = dd
	}
}



// WithFullDeploymentData returns an AssetToViewOption that specifies a function to compute the
// DeploymentData for the FULL view.
func WithFullDeploymentData(dd fDeploymentData) AssetToViewOption {
	return func(opts *assetToViewOptions) {
		opts.FullDeploymentData = dd
	}
}

// WithFileDescriptorSet returns an AssetToViewOption that specifies a function to compute the
// FileDescriptorSet.
func WithFileDescriptorSet(fds fFileDescriptorSet) AssetToViewOption {
	return func(opts *assetToViewOptions) {
		opts.FileDescriptorSet = fds
	}
}

// WithMetadata returns an AssetToViewOption that specifies the Metadata to use.
func WithMetadata(md *metadatapb.Metadata) AssetToViewOption {
	return func(opts *assetToViewOptions) {
		opts.Metadata = md
	}
}

// AssetToView returns the specified view of an Asset.
func AssetToView[T Asset](asset T, view viewpb.AssetViewType, options ...AssetToViewOption) (T, error) {
	opts := assetToViewOptions{}
	for _, option := range options {
		option(&opts)
	}
	if opts.Metadata == nil {
		opts.Metadata = proto.Clone(asset.GetMetadata()).(*metadatapb.Metadata)
	}

	assetView := newMessage(asset)

	metadataView, err := MetadataToView(opts.Metadata, view)
	if err != nil {
		return assetView, err
	}

	if err := setFieldValue(assetView, metadataFieldName, metadataView); err != nil {
		return assetView, err
	}

	// Fail early if the input type doesn't have the required fields.
	if asset.ProtoReflect().Descriptor().Oneofs().ByName(protoreflect.Name(assetSpecificMetadataOneOfName)) == nil {
		return assetView, fmt.Errorf("%w: %T does not have oneof %q", errInvalidAssetType, asset, assetSpecificMetadataOneOfName)
	}
	if asset.ProtoReflect().Descriptor().Fields().ByName(protoreflect.Name(deploymentDataFieldName)) == nil {
		return assetView, fmt.Errorf("%w: %T does not have field %q", errInvalidAssetType, asset, deploymentDataFieldName)
	}
	for _, cv := range opts.CustomViewValues {
		if asset.ProtoReflect().Descriptor().Fields().ByName(protoreflect.Name(cv.Field)) == nil {
			return assetView, fmt.Errorf("%w: %T does not have field %q", errInvalidAssetType, asset, cv.Field)
		}
	}

	switch view {
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC:
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL:
		if err := copyOneOfFieldValue(asset, assetView, assetSpecificMetadataOneOfName); err != nil {
			return assetView, err
		}
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_VERSIONS:
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL_METADATA:
		if err := mergeAllMetadataToView(asset, assetView, opts); err != nil {
			return assetView, err
		}

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL:
		fallthrough

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL:
		if err := mergeAllMetadataToView(asset, assetView, opts); err != nil {
			return assetView, err
		}

		// Optionally set the view's deployment data.
		var fDD fDeploymentData
		switch view {
		case viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL:
			fDD = opts.FullDeploymentData

		case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL:
			fDD = opts.AllDeploymentData

		default:
			return assetView, status.Errorf(codes.Internal, "unexpected asset view type %v", view.String())
		}
		if fDD != nil {
			dd, err := fDD()
			if err != nil {
				return assetView, err
			}
			if dd != nil {
				if err := setFieldValue(assetView, deploymentDataFieldName, dd); err != nil {
					return assetView, err
				}
			}
		}
	default:
		return assetView, status.Errorf(codes.InvalidArgument, "unsupported asset view type %v", view.String())
	}

	// Set custom view values.
	for _, cv := range opts.CustomViewValues {
		if slices.Contains(cv.Views, view) {
			if value, err := cv.Value(); err != nil {
				return assetView, err
			} else if err := setFieldValue(assetView, cv.Field, value); err != nil {
				return assetView, err
			}
		}
	}

	return assetView, nil
}

// MetadataToView returns the specified view of the Metadata part of an Asset.
func MetadataToView(metadata *metadatapb.Metadata, view viewpb.AssetViewType) (*metadatapb.Metadata, error) {
	metadataView := &metadatapb.Metadata{}

	switch view {
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC:
		metadataView.AssetType = metadata.GetAssetType()
		metadataView.IdVersion = metadata.GetIdVersion()
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL:
		metadataView.AssetTag = metadata.GetAssetTag()
		metadataView.AssetType = metadata.GetAssetType()
		metadataView.DisplayName = metadata.GetDisplayName()
		metadataView.Documentation = metadata.GetDocumentation()
		metadataView.IdVersion = metadata.GetIdVersion()
		metadataView.Vendor = metadata.GetVendor()
		metadataView.Provides = metadata.GetProvides()
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_VERSIONS:
		metadataView.AssetType = metadata.GetAssetType()
		metadataView.IdVersion = metadata.GetIdVersion()
		metadataView.ReleaseNotes = metadata.GetReleaseNotes()
		metadataView.UpdateTime = metadata.GetUpdateTime()
		metadataView.Vendor = metadata.GetVendor()
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL_METADATA:
		fallthrough

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL:
		fallthrough

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL:
		metadataView = metadata
	default:
		return nil, status.Errorf(codes.InvalidArgument, "unsupported asset view type %v", view.String())
	}

	return metadataView, nil
}

// ViewHasDeploymentData returns true if the view has deployment data.
func ViewHasDeploymentData(view viewpb.AssetViewType) bool {
	switch view {

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL:
		fallthrough

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL:
		return true
	default:
		return false
	}
}



// VerifyPublicView verifies that a view is accessible to public APIs.
func VerifyPublicView(view viewpb.AssetViewType) error {
	if view == viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL {
		return status.Errorf(codes.InvalidArgument, "invalid view %s", view.String())
	}
	return nil
}



func mergeAllMetadataToView[T Asset](asset T, assetView T, opts assetToViewOptions) error {
	if opts.FileDescriptorSet != nil {
		fds, err := opts.FileDescriptorSet()
		if err != nil {
			return err
		}
		assetView.GetMetadata().FileDescriptorSet = fds
	}

	if err := copyOneOfFieldValue(asset, assetView, assetSpecificMetadataOneOfName); err != nil {
		return err
	}

	return nil
}

// EnumFromString returns an AssetViewType enum from a string.
func EnumFromString(t string) (viewpb.AssetViewType, error) {
	if i, exists := viewpb.AssetViewType_value[t]; exists {
		return viewpb.AssetViewType(i), nil
	}
	return viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED, fmt.Errorf("invalid view: %q", t)
}

// EnumFromShortString returns an AssetViewType enum from a short string. The
// empty string maps to ASSET_VIEW_TYPE_UNSPECIFIED.
func EnumFromShortString(t string) (viewpb.AssetViewType, error) {
	if t == "" {
		return viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED, nil
	}
	return EnumFromString(fmt.Sprintf("ASSET_VIEW_TYPE_%s", strings.ToUpper(t)))
}

// ShortStringFromEnum returns a short string from an AssetViewType enum.
func ShortStringFromEnum(t viewpb.AssetViewType) string {
	return strings.ToLower(strings.TrimPrefix(t.String(), "ASSET_VIEW_TYPE_"))
}

func setFieldValue(m proto.Message, name string, value proto.Message) error {
	mR := m.ProtoReflect()

	fd := mR.Descriptor().Fields().ByName(protoreflect.Name(name))
	if fd == nil {
		return fmt.Errorf("field %q not found in message %v", name, mR.Descriptor().FullName())
	}

	valueR := protoreflect.ValueOf(value.ProtoReflect())
	if value == nil || !valueR.Message().IsValid() {
		mR.Clear(fd)
		return nil
	}

	mR.Set(fd, valueR)

	return nil
}

func copyOneOfFieldValue[T proto.Message](src T, dst T, name string) error {
	srcR := src.ProtoReflect()
	dstR := dst.ProtoReflect()

	od := srcR.Descriptor().Oneofs().ByName(protoreflect.Name(name))
	if od == nil {
		return fmt.Errorf("oneof %q not found in message %v", name, srcR.Descriptor().FullName())
	}

	active := srcR.WhichOneof(od)
	if active == nil {
		activeDst := dstR.WhichOneof(od)
		if activeDst != nil {
			dstR.Clear(activeDst)
		}
		return nil
	}

	fd := srcR.Descriptor().Fields().ByName(protoreflect.Name(active.Name()))
	if fd == nil {
		return fmt.Errorf("field %q not found in message %v", active.Name(), srcR.Descriptor().FullName())
	}
	dstR.Set(fd, srcR.Get(fd))

	return nil
}

func newMessage[T proto.Message](m T) T {
	return m.ProtoReflect().New().Interface().(T)
}

// BasicAssetView returns a basic view of an [assetpb.Asset] that only contains
// basic metadata (such as the ID).
func BasicAssetView(asset *assetpb.Asset) *assetpb.Asset {
	if asset == nil {
		return nil
	}
	switch source := asset.GetSource().(type) {
	case *assetpb.Asset_Catalog:
		return asset
	case *assetpb.Asset_Local:
		return &assetpb.Asset{
			Source: &assetpb.Asset_Local{
				Local: basicProcessedAssetView(source.Local),
			},
		}
	}
	return nil
}

func basicProcessedAssetView(processedAsset *processedassetpb.ProcessedAsset) *processedassetpb.ProcessedAsset {
	if processedAsset == nil {
		return nil
	}
	switch v := processedAsset.GetVariant().(type) {
	case *processedassetpb.ProcessedAsset_Data:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_Data{
				Data: &dataassetpb.DataAsset{
					Metadata: &metadatapb.Metadata{
						IdVersion: v.Data.GetMetadata().GetIdVersion(),
					},
				},
			},
		}
	case *processedassetpb.ProcessedAsset_HardwareDevice:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_HardwareDevice{
				HardwareDevice: &hardwaredevicemanifestpb.ProcessedHardwareDeviceManifest{
					Metadata: &hardwaredevicemanifestpb.HardwareDeviceMetadata{
						Id: v.HardwareDevice.GetMetadata().GetId(),
					},
				},
			},
		}
	case *processedassetpb.ProcessedAsset_Process:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_Process{
				Process: &processassetpb.ProcessAsset{
					Metadata: &metadatapb.Metadata{
						IdVersion: v.Process.GetMetadata().GetIdVersion(),
					},
				},
			},
		}
	case *processedassetpb.ProcessedAsset_SceneObject:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_SceneObject{
				SceneObject: &sceneobjectmanifestpb.ProcessedSceneObjectManifest{
					Metadata: &sceneobjectmanifestpb.SceneObjectMetadata{
						Id: v.SceneObject.GetMetadata().GetId(),
					},
				},
			},
		}
	case *processedassetpb.ProcessedAsset_Service:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_Service{
				Service: &servicemanifestpb.ProcessedServiceManifest{
					Metadata: &servicemanifestpb.ServiceMetadata{
						Id: v.Service.GetMetadata().GetId(),
					},
				},
			},
		}
	case *processedassetpb.ProcessedAsset_Skill:
		return &processedassetpb.ProcessedAsset{
			Variant: &processedassetpb.ProcessedAsset_Skill{
				Skill: &processedskillmanifestpb.ProcessedSkillManifest{
					Metadata: &processedskillmanifestpb.SkillMetadata{
						Id: v.Skill.GetMetadata().GetId(),
					},
				},
			},
		}
	}
	return nil
}
