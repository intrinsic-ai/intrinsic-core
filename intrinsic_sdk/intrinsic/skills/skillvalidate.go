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

// Package skillvalidate provides utils for validating Skills.
package skillvalidate

import (
	"context"
	"errors" 
	"fmt"
	"slices"
	"strings"

	"intrinsic/assets/dependencies/platform"
	"intrinsic/assets/dependencies/utils" 
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"
	"intrinsic/assets/metadatautils"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"

	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	psmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"
	smpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

var errFallbackManifestDependencyKeyEqualsRequiredEquipment = errors.New("cannot declare dependency with a fallback_manifest_dependency_key whose value equals the key of any manifest-declared required equipment") 

// SkillManifest validates a SkillManifest.
func SkillManifest(ctx context.Context, m *smpb.SkillManifest, files *protoregistry.Files) error {
	if m == nil {
		return fmt.Errorf("SkillManifest must not be nil")
	}
	if files == nil {
		return fmt.Errorf("files registry must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m); err != nil {
		return fmt.Errorf("invalid SkillManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetId())

	sd := &psmpb.SkillDetails{
		Options:       m.GetOptions(),
		Dependencies:  m.GetDependencies(),
		Parameter:     m.GetParameter(),
		ExecuteResult: m.GetReturnType(),
	}
	if err := validateSkillDetails(sd, files); err != nil {
		return fmt.Errorf("invalid Skill details for %q: %w", id, err)
	}

	if err := validateSkillServicesConfig(m.GetOptions().GetSkillServicesConfig()); err != nil {
		return fmt.Errorf("invalid Skill details for %q: %w", id, err)
	}
	for _, iface := range platform.ProvidedBySkillManifest(m) {
		if err := validatePlatformProvideInFiles(iface, files); err != nil {
			return fmt.Errorf("invalid platform provided interfaces for Skill %q: %w", id, err)
		}
	}

	return nil
}

type processedSkillManifestOptions struct {
	report                            *report.Report
	requiredPlatformSkillDependencies []string
	requiredRegistry                  string
}

// ProcessedSkillManifestOption is an option for validating a ProcessedSkillManifest.
type ProcessedSkillManifestOption func(*processedSkillManifestOptions)

// WithRequiredRegistry specifies the registry that must have been used for all images.
func WithRequiredRegistry(registry string) ProcessedSkillManifestOption {
	return func(opts *processedSkillManifestOptions) {
		opts.requiredRegistry = registry
	}
}

// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) ProcessedSkillManifestOption {
	return func(opts *processedSkillManifestOptions) {
		opts.report = report
	}
}

// WithRequiredProvidedToPlatformInterfaces specifies the protocol-prefixed interfaces a Skill must
// implement to be compatible with the current version of the platform. For example, if called with
// 'grpc://intrinsic_proto.skills.Executor', the Skill validator will generate an error if the Skill
// does not provide the Executor gRPC service to the platform.
func WithRequiredProvidedToPlatformInterfaces(required ...string) ProcessedSkillManifestOption {
	return func(opts *processedSkillManifestOptions) {
		opts.requiredPlatformSkillDependencies = append(opts.requiredPlatformSkillDependencies, required...)
	}
}

// ProcessedSkillManifest validates a ProcessedSkillManifest.
func ProcessedSkillManifest(ctx context.Context, m *psmpb.ProcessedSkillManifest, options ...ProcessedSkillManifestOption) error {
	opts := &processedSkillManifestOptions{}
	WithReport(report.New())(opts)
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("ProcessedSkillManifest must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata()); err != nil {
		return fmt.Errorf("invalid ProcessedSkillManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	if m.GetAssets() == nil || m.GetAssets().GetFileDescriptorSet() == nil {
		return fmt.Errorf("ProcessedSkillManifest file descriptor set must not be nil")
	}
	files, err := protodesc.NewFiles(m.GetAssets().GetFileDescriptorSet())
	if err != nil {
		return fmt.Errorf("failed to populate the registry: %w", err)
	}

	if err := validateSkillDetails(m.GetDetails(), files); err != nil {
		return fmt.Errorf("invalid Skill details for %q: %w", id, err)
	}

	if err := validateSkillServicesConfig(m.GetDetails().GetOptions().GetSkillServicesConfig()); err != nil {
		return fmt.Errorf("invalid Skill details for %q: %w", id, err)
	}

	if opts.requiredRegistry != "" {
		switch d := m.GetAssets().GetDeploymentType().(type) {
		case *psmpb.ProcessedSkillAssets_Image:
			if d.Image.GetRegistry() != opts.requiredRegistry {
				return fmt.Errorf("unexpected registry specified (expected %q, got %q)", opts.requiredRegistry, d.Image.GetRegistry())
			}
		}
	}

	if len(opts.requiredPlatformSkillDependencies) > 0 {
		if err := validatePlatformSkillDependencies(m, opts.requiredPlatformSkillDependencies); err != nil {
			return fmt.Errorf("invalid platform skill dependencies: %w", err)
		}
	}

	return nil
}

func validateSkillDetails(sd *psmpb.SkillDetails, files *protoregistry.Files) error {

	if name := sd.GetParameter().GetMessageFullName(); name != "" {
		d, err := files.FindDescriptorByName(protoreflect.FullName(name))
		if err != nil {
			return fmt.Errorf("cannot find parameter message %q: %w", name, err)
		}
		if _, ok := d.(protoreflect.MessageDescriptor); !ok {
			return fmt.Errorf("message %q is not a message", name)
		}


		md := d.(protoreflect.MessageDescriptor) // existence already checked above
		fallbackKeys := utils.FallbackManifestKeys(md)
		if len(fallbackKeys) > 0 {
			requiredEquipment := sd.GetDependencies().GetRequiredEquipment()
			for _, key := range fallbackKeys {
				if _, ok := requiredEquipment[key]; ok {
					return errFallbackManifestDependencyKeyEqualsRequiredEquipment
				}
			}
		}

	}
	if name := sd.GetExecuteResult().GetMessageFullName(); name != "" {
		if _, err := files.FindDescriptorByName(protoreflect.FullName(name)); err != nil {
			return fmt.Errorf("cannot find return type message %q: %w", name, err)
		}
	}

	return nil
}

func validatePlatformSkillDependencies(manifest *psmpb.ProcessedSkillManifest, required []string) error {
	interfaces := platform.ProvidedByProcessedSkillManifest(manifest)

	// provided is a map where keys are required interfaces and values indicate whether the Skill
	// provides them.
	provided := make(map[string]bool, len(required))
	for _, r := range required {
		provided[r] = false
	}
	for _, iface := range interfaces {
		ifaceURI := iface.GetUri()
		if _, ok := provided[ifaceURI]; ok {
			provided[ifaceURI] = true
		}
	}
	for uri, found := range provided {
		if !found {
			return fmt.Errorf("this platform version requires that each Skill provide %q", uri)
		}
	}
	return nil
}

func validateSkillServicesConfig(ssc *smpb.SkillServicesConfig) error {
	if ssc == nil {
		return fmt.Errorf("skill services config must be specified")
	}
	if len(ssc.GetServiceVersions()) == 0 {
		return fmt.Errorf("skill services config is present but no service versions are specified")
	}
	if slices.Contains(ssc.GetServiceVersions(), smpb.SkillServicesConfig_UNSPECIFIED) {
		return fmt.Errorf("skill services config contains UNSPECIFIED service version")
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
