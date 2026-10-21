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

// Package metadatautils provides utilities for Asset metadata.
package metadatautils

import (
	"slices"

	"intrinsic/assets/dependencies/platform"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	"intrinsic/assets/tagutils"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	datamanifestpb "intrinsic/assets/data/proto/v1/data_manifest_go_proto"
	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	pmpb "intrinsic/assets/processes/proto/process_manifest_go_proto"
	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	documentationpb "intrinsic/assets/proto/documentation_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	vendorpb "intrinsic/assets/proto/vendor_go_proto"
	sceneobjectmanifestpb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"
	servicemanifestpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	psmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"
	skillmanifestpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

const (
	// !!!!!!!!!!!!!!!!!! DO NOT MODIFY THESE LIMITS WITHOUT APPROVAL FROM ASSETS. !!!!!!!!!!!!!!!!!!

	// MaxDisplayNameLength is the maximum allowed character length for Asset display names.
	MaxDisplayNameLength = 80
	// MaxDescriptionLength is the maximum allowed character length for Asset descriptions.
	//
	// This limit set based on the longest existing asset description at the time of writing.
	MaxDescriptionLength = 2400
	// MaxMetadataSize is the maximum size of a Metadata message, in bytes.
	//
	// This limit exists to ensure optimal performance of Metadata consumers such as the frontend.
	// The frontend's interaction with Asset metadata assumes relatively small message sizes;
	// arbitrarily large metadata could severely degrade its performance. Setting this limit here
	// guards against code changes that inadvertently push metadata past this limit (e.g., by adding
	// large proto dependencies to an Asset's FileDescriptorSet).
	MaxMetadataSize = 1024 * 1024 // 1 MiB
	// MaxRelNotesLength is the maximum allowed character length for Asset release notes.
	// The character limits for release notes were set arbitrarily based on the length of
	// description.
	MaxRelNotesLength = 2400
	// MaxVersionLength is the maximum allowed character length for Asset versions.
	//
	// Semver does not have character limits for versions, and leaves it to best judgment:
	// https://semver.org/#does-semver-have-a-size-limit-on-the-version-string. We choose an arbitrary
	// limit here that considers potential lengths of prerelease label and build metadata.
	MaxVersionLength = 128
)

// NameCharLength sets character limits for Asset names as a function of their type.
//
// Skill IDs are used as the DNS names in k8s and are formatted as "<package_name>.<name>".
// https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#rfc-1035-label-names
// restricts label names to 63 characters. Keeping this as the maximum allowed upper limit, a
// conservative limit is chosen for skill names to account for package names, other prefixes, and
// special symbols that maybe be added when converting IDs to k8s labels.
//
// Note: Skill IDs are used for DNS names, but we check Asset names for character limits. We cannot
// limit the ID character limit explicitly, because IDs could have nested packages. We also cannot
// restrict character limits for packages for the same reason.
var NameCharLength = map[atypepb.AssetType]int{
	// keep-sorted start
	atypepb.AssetType_ASSET_TYPE_DATA:            80,
	atypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE: 80,
	atypepb.AssetType_ASSET_TYPE_PROCESS:         80,
	atypepb.AssetType_ASSET_TYPE_SCENE_OBJECT:    80,
	atypepb.AssetType_ASSET_TYPE_SERVICE:         80,
	atypepb.AssetType_ASSET_TYPE_SKILL:           45,
	// keep-sorted end
}

// ManifestMetadata is an interface for an Asset manifest proto that specifies metadata.
type ManifestMetadata interface {
	proto.Message

	GetDisplayName() string
	GetDocumentation() *documentationpb.Documentation
	GetId() *idpb.Id
	GetVendor() *vendorpb.Vendor
}

type metadataWithTag interface {
	GetAssetTag() atagpb.AssetTag
}

type metadataWithTags interface {
	GetAssetTags() []atagpb.AssetTag
}

type validateMetadataOptions struct {
	allowRuntimeAssetID        *bool
	requiredAssetType          atypepb.AssetType
	specifiesFileDescriptorSet *bool
	specifiesProvides          *bool
	specifiesReleaseNotes      *bool
	specifiesUpdateTime        *bool
	specifiesVersion           *bool
}

// ValidateMetadataOption is an option for ValidateMetadata.
type ValidateMetadataOption func(*validateMetadataOptions)

// WithAllowRuntimeAssetID allows the metadata to have the reserved runtime Asset ID.
func WithAllowRuntimeAssetID() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		opts.allowRuntimeAssetID = proto.Bool(true)
	}
}

// WithAssetType requires metadata to have the specified Asset type.
func WithAssetType(at atypepb.AssetType) ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		opts.requiredAssetType = at
	}
}

// WithCatalogOptions adds options for validating metadata for use in the AssetCatalog.
func WithCatalogOptions() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		opts.allowRuntimeAssetID = proto.Bool(true)
		opts.specifiesUpdateTime = proto.Bool(true)
		opts.specifiesVersion = proto.Bool(true)
	}
}

// WithInAssetOptions adds options for validating metadata that are represented within an Asset
// definition (e.g., Data Assets and Processes).
func WithInAssetOptions() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		WithNoOutputOnlyFields()(opts)
		opts.specifiesReleaseNotes = proto.Bool(false)
		opts.specifiesUpdateTime = proto.Bool(false)
		opts.specifiesVersion = proto.Bool(false)
	}
}

// WithInAssetOptionsIgnoreVersion adds options for validating metadata that are represented within
// an Asset definition (e.g., Data Assets and Processes).
//
// It ignores the presence of a version, to support transitioning from metadata that previously
// required a version.
func WithInAssetOptionsIgnoreVersion() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		WithNoOutputOnlyFields()(opts)
		opts.specifiesReleaseNotes = proto.Bool(false)
		opts.specifiesUpdateTime = proto.Bool(false)
	}
}

// WithNoOutputOnlyFields requires metadata to have no output-only fields.
func WithNoOutputOnlyFields() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		opts.specifiesFileDescriptorSet = proto.Bool(false)
		opts.specifiesProvides = proto.Bool(false)
	}
}

// WithRuntimeOptions adds options for validating runtime Asset representations.
func WithRuntimeOptions() ValidateMetadataOption {
	return func(opts *validateMetadataOptions) {
		opts.allowRuntimeAssetID = proto.Bool(false)
		opts.specifiesFileDescriptorSet = proto.Bool(true)
		opts.specifiesProvides = proto.Bool(true)
	}
}

// ValidateMetadata validates Asset metadata.
func ValidateMetadata(m *metadatapb.Metadata, options ...ValidateMetadataOption) error {
	opts := &validateMetadataOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if m.GetIdVersion().GetVersion() == "" {
		if err := idutils.ValidateIDProto(m.GetIdVersion().GetId()); err != nil {
			return status.Errorf(codes.InvalidArgument, "invalid id: %v", err)
		}
	} else if err := idutils.ValidateIDVersionProto(m.GetIdVersion()); err != nil {
		return status.Errorf(codes.InvalidArgument, "invalid id version: %v", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetIdVersion().GetId())

	// Verify that the Asset ID is not reserved, except when opts.allowRuntimeAssetID permits the
	// use of platform.RuntimeAssetID.
	if opts.allowRuntimeAssetID == nil || !*opts.allowRuntimeAssetID || id != platform.RuntimeAssetID {
		if err := platform.ValidateIDNotReserved(m.GetIdVersion().GetId()); err != nil {
			return err
		}
	}

	if err := validateFieldPresence(id, "file descriptor set", m.GetFileDescriptorSet(), opts.specifiesFileDescriptorSet); err != nil {
		return err
	}
	if err := validateArrayPresence(id, "provides", m.GetProvides(), opts.specifiesProvides); err != nil {
		return err
	}
	if err := validateFieldPresence(id, "release notes", m.GetReleaseNotes(), opts.specifiesReleaseNotes); err != nil {
		return err
	}
	if err := validateFieldPresence(id, "update time", m.GetUpdateTime(), opts.specifiesUpdateTime); err != nil {
		return err
	}
	if err := validateFieldPresence(id, "version", m.GetIdVersion().GetVersion(), opts.specifiesVersion); err != nil {
		return err
	}

	if err := validateAssetTag(id, m.GetAssetTag(), m.GetAssetType()); err != nil {
		return err
	}
	if err := validateAssetType(id, m.GetAssetType(), opts.requiredAssetType); err != nil {
		return err
	}
	if err := validateDescription(id, m.GetDocumentation().GetDescription()); err != nil {
		return err
	}
	if err := validateDisplayName(id, m.GetDisplayName()); err != nil {
		return err
	}
	if err := validateName(id, m.GetIdVersion().GetId().GetName(), m.GetAssetType()); err != nil {
		return err
	}
	if err := validateRelNotes(id, m.GetReleaseNotes()); err != nil {
		return err
	}
	if err := validateVendor(id, m.GetVendor().GetDisplayName()); err != nil {
		return err
	}
	if err := validateVersion(id, m.GetIdVersion().GetVersion()); err != nil {
		return err
	}

	if sz := proto.Size(m); sz > MaxMetadataSize {
		return status.Errorf(codes.ResourceExhausted, "metadata size too large for %q (%d bytes > max %d bytes). Try reducing size of release notes, documentation, or file descriptor set.)", id, sz, MaxMetadataSize)
	}

	// Validate provides interfaces.
	for _, pi := range m.GetProvides() {
		if err := interfaceutils.ValidateInterfaceName(pi.GetUri()); err != nil {
			return status.Errorf(codes.InvalidArgument, "invalid provides interface %q for %q: %v", pi.GetUri(), id, err)
		}
	}

	return nil
}

// ValidateManifestMetadata validates Asset manifest metadata.
func ValidateManifestMetadata(m ManifestMetadata, options ...ValidateMetadataOption) error {
	metadata, err := metadataFromManifestMetadata(m)
	if err != nil {
		return err
	}

	return ValidateMetadata(metadata, options...)
}

// ToInputMetadata returns a clone of the input Metadata with output-only fields stripped.
func ToInputMetadata(m *metadatapb.Metadata) *metadatapb.Metadata {
	mOut := proto.Clone(m).(*metadatapb.Metadata)
	StripNonInputMetadata(mOut)

	return mOut
}

// StripNonInputMetadata strips the output-only fields from the specified metadata.
func StripNonInputMetadata(m *metadatapb.Metadata) {
	m.FileDescriptorSet = nil
	m.Provides = nil
}

// ToInAssetMetadata returns a clone of the input Metadata that is suitable for representation
// within an Asset definition (e.g., see Data and Process Assets).
func ToInAssetMetadata(m *metadatapb.Metadata) *metadatapb.Metadata {
	mOut := proto.Clone(m).(*metadatapb.Metadata)
	StripNonInAssetMetadata(mOut)

	return mOut
}

// StripNonInAssetMetadata strips from the input Metadata fields that don't apply when represented
// within an Asset definition.
func StripNonInAssetMetadata(m *metadatapb.Metadata) {
	StripNonInputMetadata(m)
	m.ReleaseNotes = ""
	m.UpdateTime = nil
	if m.GetIdVersion() != nil {
		m.GetIdVersion().Version = ""
	}
}

func validateAssetTag(id string, atag atagpb.AssetTag, at atypepb.AssetType) error {
	if applicableTags, err := tagutils.AssetTagsForType(at); err != nil {
		return status.Errorf(codes.Internal, "cannot get tags for Asset type %q: %v", at.String(), err)
	} else if !slices.Contains(applicableTags, atag) {
		return status.Errorf(codes.InvalidArgument, "invalid tag for %q: tag %q does not apply to Asset type %q", id, atag.String(), at.String())
	}

	return nil
}

func validateAssetType(id string, at atypepb.AssetType, required atypepb.AssetType) error {
	if at == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
		return status.Errorf(codes.InvalidArgument, "no Asset type specified for %q", id)
	}
	if required != atypepb.AssetType_ASSET_TYPE_UNSPECIFIED && at != required {
		return status.Errorf(codes.InvalidArgument, "invalid Asset type for %q (need %v, got %v)", id, required.String(), at.String())
	}

	return nil
}

func validateDescription(id string, description string) error {
	return validateStringLength(id, "description", description, MaxDescriptionLength)
}

func validateDisplayName(id string, displayName string) error {
	if err := validateStringRequired(id, "display name", displayName); err != nil {
		return err
	}
	return validateStringLength(id, "display name", displayName, MaxDisplayNameLength)
}

func validateName(id string, name string, at atypepb.AssetType) error {
	maxLength, exists := NameCharLength[at]
	if !exists {
		return status.Errorf(codes.Internal, "unsupported Asset type: %v", at)
	}
	if err := validateStringRequired(id, "name", name); err != nil {
		return err
	}
	return validateStringLength(id, "name", name, maxLength)
}

func validateRelNotes(id string, relnotes string) error {
	return validateStringLength(id, "release notes", relnotes, MaxRelNotesLength)
}

func validateVendor(id string, vendor string) error {
	return validateStringRequired(id, "vendor", vendor)
}

func validateVersion(id string, version string) error {
	return validateStringLength(id, "version", version, MaxVersionLength)
}

func validateStringRequired(id string, name string, value string) error {
	if value == "" {
		return status.Errorf(codes.InvalidArgument, "no %s specified for %q", name, id)
	}

	return nil
}

func validateStringLength(id string, name string, value string, maxLength int) error {
	if valueLength := len(value); valueLength > maxLength {
		return status.Errorf(codes.ResourceExhausted, "%s too long for %q (length %d > max %d)", name, id, valueLength, maxLength)
	}
	return nil
}

func validateFieldPresence[T comparable](id string, name string, x T, specifies *bool) error {
	var z T
	if specifies != nil {
		if *specifies {
			if x == z {
				return status.Errorf(codes.InvalidArgument, "required %s not specified for %q", name, id)
			}
		} else if x != z {
			return status.Errorf(codes.InvalidArgument, "disallowed %s specified for %q", name, id)
		}
	}
	return nil
}

func validateArrayPresence[T any](id string, name string, x []T, specifies *bool) error {
	if specifies != nil && !*specifies && len(x) > 0 {
		return status.Errorf(codes.InvalidArgument, "disallowed %s specified for %q", name, id)
	}
	return nil
}

func metadataFromManifestMetadata(m ManifestMetadata) (*metadatapb.Metadata, error) {
	var at atypepb.AssetType
	switch mt := m.(type) {
	case *datamanifestpb.DataManifest_Metadata:
		at = atypepb.AssetType_ASSET_TYPE_DATA
	case *hdmpb.HardwareDeviceMetadata:
		at = atypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE
	case *pmpb.ProcessMetadata:
		at = atypepb.AssetType_ASSET_TYPE_PROCESS
	case *sceneobjectmanifestpb.SceneObjectMetadata:
		at = atypepb.AssetType_ASSET_TYPE_SCENE_OBJECT
	case *servicemanifestpb.ServiceMetadata:
		at = atypepb.AssetType_ASSET_TYPE_SERVICE
	case *skillmanifestpb.SkillManifest, *psmpb.SkillMetadata:
		at = atypepb.AssetType_ASSET_TYPE_SKILL
	default:
		return nil, status.Errorf(codes.Internal, "unsupported manifest type: %T", mt)
	}

	tag, err := tagFromManifestMetadata(m)
	if err != nil {
		return nil, err
	}

	return &metadatapb.Metadata{
		AssetTag:      tag,
		AssetType:     at,
		DisplayName:   m.GetDisplayName(),
		Documentation: m.GetDocumentation(),
		IdVersion: &idpb.IdVersion{
			Id: m.GetId(), // ID only, since manifests do not specify versions.
		},
		Vendor: m.GetVendor(),
	}, nil
}

func tagFromManifestMetadata(m any) (atagpb.AssetTag, error) {
	// Some metadata has a `tag` field.
	if mWithTag, ok := m.(metadataWithTag); ok {
		return mWithTag.GetAssetTag(), nil
	}

	// Some metadata has a `tags` field.
	if mWithTags, ok := m.(metadataWithTags); ok {
		if len(mWithTags.GetAssetTags()) > 0 {
			return mWithTags.GetAssetTags()[0], nil
		}

		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED, nil
	}

	// Some metadata cannot represent tags.
	if !metadataHasTag(m) {
		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED, nil
	}

	return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED, status.Errorf(codes.Internal, "cannot get tag from unsupported manifest type for %T", m)
}

func metadataHasTag(m any) bool {
	switch m.(type) {
	case *datamanifestpb.DataManifest_Metadata, *skillmanifestpb.SkillManifest, *psmpb.SkillMetadata:
		return false
	}

	return true
}
