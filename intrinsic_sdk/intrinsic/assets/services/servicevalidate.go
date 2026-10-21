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

// Package servicevalidate provides utils for validating Services.
package servicevalidate

import (
	"context"
	"errors"
	"fmt"
	"maps"
	"slices"
	"strings"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	anypb "google.golang.org/protobuf/types/known/anypb"

	"intrinsic/assets/dependencies/platform"
	deputils "intrinsic/assets/dependencies/utils"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	"intrinsic/assets/metadatautils"
	"intrinsic/util/go/validate"
	"intrinsic/util/proto/names"

	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	svpb "intrinsic/assets/services/proto/service_volume_go_proto"
	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	sspb "intrinsic/assets/services/proto/v1/service_state_go_proto"
)

var (
	// Services that are allowed to be missing from the Service's FileDescriptorSet.
	allowedMissingFDSProvidedServices = []string{

		// TODO: b/415901231 - Remove targets after follow-up PRs have been submitted on go/insrc.
		"tensorflow.serving.PredictionService",

	}

	// Services that may not be provided, along with a reason.
	disallowedProvidedServices = map[string]string{
		platform.DynamicReconfigurationV1Name: "should only be called by platform runtime",
	}

	errContainsSkillAnnotations = errors.New("config message for the Service must not contain Skill-specific dependency annotations")
)

// ServiceManifest validates a ServiceManifest.
func ServiceManifest(ctx context.Context, m *smpb.ServiceManifest, files *protoregistry.Files, defaultConfig *anypb.Any) error {
	if m == nil {
		return fmt.Errorf("ServiceManifest must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata()); err != nil {
		return fmt.Errorf("invalid ServiceManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	expectedImagePaths, err := validateServiceDef(m.GetServiceDef(), files)
	if err != nil {
		return fmt.Errorf("invalid service_def for Service %q: %w", id, err)
	}

	imagePaths := m.GetAssets().GetImageFilenames()
	for p := range expectedImagePaths {
		if !slices.Contains(imagePaths, p) {
			return fmt.Errorf("image %q in the manifest for Service %q is not listed in its assets", p, id)
		}
	}
	for _, p := range imagePaths {
		if _, ok := expectedImagePaths[p]; !ok {
			return fmt.Errorf("image %q in the assets for Service %q is not used in the manifest", p, id)
		}
	}

	if err := validateServiceConfig(
		m.GetServiceDef().GetConfigMessageFullName(),
		defaultConfig,
		m.GetAssets().GetDefaultConfigurationFilename() != "",
		files,
	); err != nil {
		return fmt.Errorf("invalid service config for Service %q: %w", id, err)
	}
	for _, iface := range platform.ProvidedByServiceManifest(m) {
		if err := validatePlatformProvideInFiles(iface, files); err != nil {
			return fmt.Errorf("invalid platform provided interfaces for Service %q: %w", id, err)
		}
	}

	return nil
}

type processedServiceManifestOptions struct {
	highlyPrivilegedFieldsContext  context.Context                
	highlyPrivilegedFieldsOptions  []HighlyPrivilegedFieldsOption 
	report                         *report.Report
	requiredRegistry               string
	skipPlatformServicesCheckInFDS bool
}

// ProcessedServiceManifestOption is an option for validating a ProcessedServiceManifest.
type ProcessedServiceManifestOption func(*processedServiceManifestOptions)



// WithHighlyPrivilegedFieldsOptions appends options to use for validating highly-privileged fields
// in the manifest.
//
// Highly-privileged fields are only validated if options are provided.
func WithHighlyPrivilegedFieldsOptions(ctx context.Context, options ...HighlyPrivilegedFieldsOption) ProcessedServiceManifestOption {
	return func(opts *processedServiceManifestOptions) {
		opts.highlyPrivilegedFieldsContext = ctx
		opts.highlyPrivilegedFieldsOptions = append(opts.highlyPrivilegedFieldsOptions, options...)
	}
}



// WithRequiredRegistry specifies the registry that must have been used for all images.
func WithRequiredRegistry(registry string) ProcessedServiceManifestOption {
	return func(opts *processedServiceManifestOptions) {
		opts.requiredRegistry = registry
	}
}

// WithSkipPlatformServicesCheckInFDS specifies whether to skip the check that platform-provided
// services are present in the file descriptor set.
func WithSkipPlatformServicesCheckInFDS(skip bool) ProcessedServiceManifestOption {
	return func(opts *processedServiceManifestOptions) {
		opts.skipPlatformServicesCheckInFDS = skip
	}
}

// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) ProcessedServiceManifestOption {
	return func(opts *processedServiceManifestOptions) {
		opts.report = report
	}
}

// ProcessedServiceManifest validates a ProcessedServiceManifest.
func ProcessedServiceManifest(ctx context.Context, m *smpb.ProcessedServiceManifest, options ...ProcessedServiceManifestOption) error {
	opts := &processedServiceManifestOptions{}
	WithReport(report.New())(opts)
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("ProcessedServiceManifest must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata()); err != nil {
		return fmt.Errorf("invalid ProcessedServiceManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	var files *protoregistry.Files
	if fds := m.GetAssets().GetFileDescriptorSet(); fds != nil {
		var err error
		files, err = protodesc.NewFiles(fds)
		if err != nil {
			return fmt.Errorf("failed to populate the registry: %w", err)
		}
	}

	expectedImagePaths, err := validateServiceDef(m.GetServiceDef(), files)
	if err != nil {
		return fmt.Errorf("invalid service_def for Service %q: %w", id, err)
	}

	imagePaths := slices.Collect(maps.Keys(m.GetAssets().GetImages()))
	for p := range expectedImagePaths {
		if !slices.Contains(imagePaths, p) {
			return fmt.Errorf("image %q in the manifest for Service %q is not listed in its assets", p, id)
		}
	}
	for _, p := range imagePaths {
		if _, ok := expectedImagePaths[p]; !ok {
			return fmt.Errorf("image %q in the assets for Service %q is not used in the manifest", p, id)
		}
	}
	if opts.requiredRegistry != "" {
		for k, image := range m.GetAssets().GetImages() {
			if image.GetRegistry() != opts.requiredRegistry {
				return fmt.Errorf("unexpected registry specified for image %s (expected %q, got %q)", k, opts.requiredRegistry, image.GetRegistry())
			}
		}
	}

	defaultConfig := m.GetAssets().GetDefaultConfiguration()
	if err := validateServiceConfig(m.GetServiceDef().GetConfigMessageFullName(), defaultConfig, defaultConfig != nil, files); err != nil {
		return fmt.Errorf("invalid service config for Service %q: %w", id, err)
	}

	if !opts.skipPlatformServicesCheckInFDS {
		for _, iface := range platform.ProvidedByProcessedServiceManifest(m) {
			if err := validatePlatformProvideInFiles(iface, files); err != nil {
				return fmt.Errorf("invalid platform provided interfaces for Service %q: %w", id, err)
			}
		}
	}



	if len(opts.highlyPrivilegedFieldsOptions) > 0 {
		if err := HighlyPrivilegedFields(opts.highlyPrivilegedFieldsContext, m, opts.highlyPrivilegedFieldsOptions...); err != nil {
			return fmt.Errorf("privileged field validation failed: %w", err)
		}
	}



	return nil
}

// Volume validates a Volume.
func Volume(volume *svpb.Volume) error {
	if err := validate.DNSLabel(volume.GetName()); err != nil {
		return fmt.Errorf("invalid volume name %q: %w", volume.GetName(), err)
	}

	switch volume.GetSource().(type) {
	case *svpb.Volume_HostPath:
		if err := validate.UserString(volume.GetHostPath().GetPath()); err != nil {
			return fmt.Errorf("invalid host path %q: %w", volume.GetHostPath().GetPath(), err)
		}
		switch volume.GetHostPath().GetType() {
		case svpb.HostPathVolumeSourceType_HOST_PATH_VOLUME_SOURCE_TYPE_UNSPECIFIED,
			svpb.HostPathVolumeSourceType_HOST_PATH_VOLUME_SOURCE_TYPE_CHAR_DEVICE:
		default:
			return fmt.Errorf("unsupported host path type %v for volume %q", volume.GetHostPath().GetType(), volume.GetName())
		}
	case *svpb.Volume_EmptyDir:
	case nil:
		return fmt.Errorf("volume %q did not specify a source", volume.GetName())
	default:
		return fmt.Errorf("unsupported volume source type %T for volume %q", volume.GetSource(), volume.GetName())
	}

	return nil
}

// VolumeMount validates a VolumeMount.
func VolumeMount(mount *svpb.VolumeMount) error {
	if err := validate.DNSLabel(mount.GetName()); err != nil {
		return fmt.Errorf("invalid volume name %q: %w", mount.GetName(), err)
	}
	if err := validate.UserString(mount.GetMountPath()); err != nil {
		return fmt.Errorf("invalid mount path %q: %w", mount.GetMountPath(), err)
	}
	return nil
}



// VerifyUserOrgMember is a function that verifies that a user is a member of the claimed org.
//
// Both the user and org are specified via context metadata.
type VerifyUserOrgMember func(ctx context.Context) error

type highlyPrivilegedFieldsOptions struct {
	VerifyUserOrgMember
}

// HighlyPrivilegedFieldsOption is an option for validating highly privileged fields.
type HighlyPrivilegedFieldsOption func(*highlyPrivilegedFieldsOptions)

// WithVerifyUserOrgMember provides a VerifyUserOrgMember function.
func WithVerifyUserOrgMember(f VerifyUserOrgMember) HighlyPrivilegedFieldsOption {
	return func(opts *highlyPrivilegedFieldsOptions) {
		opts.VerifyUserOrgMember = f
	}
}

// HighlyPrivilegedFields verifies that either no highly privileged fields are specified or that the
// user in the given context is allowed to specify them. Defaults to a no-op implementation for intrinsic-core.
var HighlyPrivilegedFields = func(ctx context.Context, manifest *smpb.ProcessedServiceManifest, options ...HighlyPrivilegedFieldsOption) error {
	return nil
}



func validateServiceDef(sd *smpb.ServiceDef, files *protoregistry.Files) (map[string]struct{}, error) {
	// Collect the Service's pod specs (verifying that at least a sim spec is specified).
	servicePodSpecs := map[string]*smpb.ServicePodSpec{}
	if sd != nil {
		if sd.GetRealSpec() != nil {
			servicePodSpecs["real"] = sd.GetRealSpec()
		}

		if sd.GetSimSpec() == nil {
			return nil, fmt.Errorf("a sim_spec must be specified if a service_def is provided")
		}
		servicePodSpecs["sim"] = sd.GetSimSpec()

		// Validate the Service's proto prefixes.
		for _, prefix := range sd.GetServiceProtoPrefixes() {
			if err := names.ValidateProtoPrefix(prefix); err != nil {
				return nil, fmt.Errorf("service proto prefix %q is not valid: %w", prefix, err)
			}

			strippedPrefix := strings.TrimSuffix(strings.TrimPrefix(prefix, "/"), "/")
			if reason, ok := disallowedProvidedServices[strippedPrefix]; ok {
				return nil, fmt.Errorf("disallowed service proto prefix %q specified (%s)", prefix, reason)
			}
			if !slices.Contains(allowedMissingFDSProvidedServices, strippedPrefix) {
				if files == nil {
					return nil, fmt.Errorf("service proto prefix %q specified, but no descriptors provided", prefix)
				}
				if _, err := files.FindDescriptorByName(protoreflect.FullName(strippedPrefix)); err == protoregistry.NotFound {
					return nil, fmt.Errorf("could not find service proto prefix %q in provided descriptors: %w", prefix, err)
				} else if err != nil {
					return nil, fmt.Errorf("checking against the file descriptor set failed unexpectedly: %w", err)
				}
			}
		}
		if config := sd.GetServiceInspectionConfig(); config != nil {
			if config.GetDataProtoMessageFullName() == "" {
				return nil, fmt.Errorf("inspection config is present but data_proto_message_full_name is empty")
			}
			// Validate the inspection proto message to be in the FileDescriptorSet.
			if files == nil {
				return nil, fmt.Errorf("inspection data proto message %q specified, but no descriptors provided", config.GetDataProtoMessageFullName())
			}
			if _, err := files.FindDescriptorByName(protoreflect.FullName(config.GetDataProtoMessageFullName())); err != nil {
				return nil, fmt.Errorf("could not find inspection data proto message %q in provided descriptors: %w", config.GetDataProtoMessageFullName(), err)
			}
		}

		if drc := sd.GetDynamicReconfigurationConfig(); drc != nil {
			// If DynamicReconfigurationConfig is present then at least one service version must be
			// specified.
			if len(drc.GetServiceVersions()) == 0 {
				return nil, fmt.Errorf("dynamic reconfiguration config is present but no service versions are specified")
			}
			if slices.Contains(drc.GetServiceVersions(), drpb.DynamicReconfigurationConfig_UNSPECIFIED) {
				return nil, fmt.Errorf("dynamic reconfiguration config contains UNSPECIFIED service version")
			}
			// If deprecated supports dynamic reconfiguration is true then the service must implement
			// intrinsic_proto.services.v1.DynamicReconfiguration.
			if sd.GetSupportsDynamicReconfiguration() && !slices.Contains(drc.GetServiceVersions(), drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION) {
				return nil, fmt.Errorf("deprecated supports_dynamic_reconfiguration is true but DynamicReconfigurationConfig does not contain corresponding service version")
			}
		} else if sd.GetSupportsDynamicReconfiguration() {
			return nil, fmt.Errorf("deprecated supports_dynamic_reconfiguration is true but DynamicReconfigurationConfig is not present")
		}

		if ss := sd.GetServiceStateConfig(); ss != nil {
			// If ServiceStateConfig is present then at least one service version must be specified.
			if len(ss.GetServiceVersions()) == 0 {
				return nil, fmt.Errorf("service state config is present but no service versions are specified")
			}
			if slices.Contains(ss.GetServiceVersions(), sspb.ServiceStateConfig_UNSPECIFIED) {
				return nil, fmt.Errorf("service state config contains UNSPECIFIED service version")
			}
			// If deprecated supports service state is true then the service must implement
			// intrinsic_proto.services.v1.ServiceState.
			if sd.GetSupportsServiceState() && !slices.Contains(ss.GetServiceVersions(), sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE) {
				return nil, fmt.Errorf("deprecated supports_service_state is true but ServiceStateConfig does not contain corresponding service version")
			}
		} else if sd.GetSupportsServiceState() {
			return nil, fmt.Errorf("deprecated supports_service_state is true but ServiceStateConfig is not present")
		}
	}

	// Validate the Service's volumes.
	for podType, spec := range servicePodSpecs {
		if err := validateServicePodSpecVolumes(spec); err != nil {
			return nil, fmt.Errorf("invalid volumes in the %s spec: %w", podType, err)
		}
	}

	// Collect the Service's image paths for future validation.
	expectedImagePaths := map[string]struct{}{}
	for _, spec := range servicePodSpecs {
		if name := spec.GetImage().GetArchiveFilename(); name != "" {
			expectedImagePaths[name] = struct{}{}
		}
		for _, container := range spec.GetExtraImages() {
			expectedImagePaths[container.GetArchiveFilename()] = struct{}{}
		}

		for _, container := range spec.GetInitContainers() {
			expectedImagePaths[container.GetArchiveFilename()] = struct{}{}
		}

	}

	return expectedImagePaths, nil
}

func validateServicePodSpecVolumes(spec *smpb.ServicePodSpec) error {
	// Validate the defined volumes.
	volumeNames := map[string]struct{}{}
	for _, volume := range spec.GetSettings().GetVolumes() {
		if err := Volume(volume); err != nil {
			return err
		}

		name := volume.GetName()
		if _, ok := volumeNames[name]; ok {
			return fmt.Errorf("volume %q is specified multiple times", name)
		}
		volumeNames[name] = struct{}{}
	}

	// Validate the volume mounts.
	for _, mount := range spec.GetImage().GetSettings().GetVolumeMounts() {
		if err := VolumeMount(mount); err != nil {
			return err
		}

		if _, ok := volumeNames[mount.GetName()]; !ok {
			return fmt.Errorf("volume mount references non-existent volume %q", mount.GetName())
		}
	}

	return nil
}

func validateServiceConfig(configMessageFullName string, defaultConfig *anypb.Any, defaultConfigNeeded bool, files *protoregistry.Files) error {
	defaultConfigProvided := defaultConfig != nil
	if defaultConfigNeeded && !defaultConfigProvided {
		return fmt.Errorf("default config is needed but not provided")
	}
	if !defaultConfigNeeded && defaultConfigProvided {
		return fmt.Errorf("no default config is needed, but one was provided")
	}
	if configMessageFullName != "" {
		if defaultConfigNeeded {
			// Verify that the default config is of the specified config message type.
			if string(defaultConfig.MessageName()) != configMessageFullName {
				return fmt.Errorf("default config is of type %q, but manifest specifies config type %q", defaultConfig.MessageName(), configMessageFullName)
			}
		}
	} else if defaultConfigNeeded {
		defaultConfigMessageName := string(defaultConfig.MessageName())
		if defaultConfigMessageName == "" {
			return fmt.Errorf("default config cannot be an empty Any message; omit it instead")
		}
		configMessageFullName = defaultConfigMessageName
	}

	// Verify that the Service's config message, if any, is in the file descriptor set.
	if configMessageFullName != "" {
		if files == nil {
			return fmt.Errorf("config message specified (%q), but no descriptors provided", configMessageFullName)
		}
		d, err := files.FindDescriptorByName(protoreflect.FullName(configMessageFullName))
		if err != nil {
			return fmt.Errorf("could not find config message %q in provided descriptors: %w", configMessageFullName, err)
		}
		if md, ok := d.(protoreflect.MessageDescriptor); !ok {
			return fmt.Errorf("config message %q is not a message", configMessageFullName)
		} else if hasSkillAnnotations := deputils.HasResolvedDependency(md, deputils.WithSkillAnnotations()); hasSkillAnnotations {
			return errContainsSkillAnnotations
		}
	}

	return nil
}

func validatePlatformProvideInFiles(iface *metadatapb.Interface, files *protoregistry.Files) error {
	if strings.HasPrefix(iface.GetUri(), interfaceutils.GRPCURIPrefix) {
		serviceName := strings.TrimPrefix(iface.GetUri(), interfaceutils.GRPCURIPrefix)
		if files == nil {
			return fmt.Errorf("platform provided interface specified (%q), but no descriptors provided", iface.GetUri())
		}
		if _, err := files.FindDescriptorByName(protoreflect.FullName(serviceName)); err != nil {
			return fmt.Errorf("could not find service %q in provided descriptors: %w", serviceName, err)
		}
	}
	return nil
}
