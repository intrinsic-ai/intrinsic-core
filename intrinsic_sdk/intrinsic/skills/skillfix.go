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

// Package skillfix contains utils that adapt a given manifest to meet the requirements of
// the latest platform version.
package skillfix

import (
	"fmt" 

	"intrinsic/util/proto/descriptorcompatibility" 

	psmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"
	smpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

// fixOpts contains options for fixing a manifest.
type fixOpts struct {
	populateOldFields   bool
	clearObsoleteFields bool
}

// FixOption is an option for fixing a manifest.
type FixOption func(*fixOpts)

// WithPopulateOldFields specifies whether to backfill old deprecated fields if empty.
func WithPopulateOldFields(populate bool) FixOption {
	return func(opts *fixOpts) {
		opts.populateOldFields = populate
	}
}

// WithClearObsoleteFields specifies whether to clear obsolete manifest fields. A field is
// considered obsolete if the platform no longer uses it.
func WithClearObsoleteFields(clear bool) FixOption {
	return func(opts *fixOpts) {
		opts.clearObsoleteFields = clear
	}
}

// Manifest updates a SkillManifest to meet the requirements of the latest platform version.
func Manifest(manifest *smpb.SkillManifest, options ...FixOption) error {
	opts := &fixOpts{}
	for _, opt := range options {
		opt(opts)
	}
	if manifest == nil {
		return nil
	}

	if manifest.GetOptions() == nil {
		manifest.Options = &smpb.Options{}
	}
	backfillSkillOptions(manifest.GetOptions())
	return nil
}

// ProcessedManifest updates a ProcessedSkillManifest to meet the requirements of the latest
// platform version.
func ProcessedManifest(manifest *psmpb.ProcessedSkillManifest, options ...FixOption) error {
	opts := &fixOpts{}
	for _, opt := range options {
		opt(opts)
	}
	if manifest == nil {
		return nil
	}

	if manifest.GetDetails() == nil {
		manifest.Details = &psmpb.SkillDetails{}
	}
	if manifest.GetDetails().GetOptions() == nil {
		manifest.Details.Options = &smpb.Options{}
	}
	backfillSkillOptions(manifest.GetDetails().GetOptions())

	if err := descriptorcompatibility.Reconcile(manifest.GetAssets().GetFileDescriptorSet()); err != nil {
		return fmt.Errorf("unable to reconcile the file descriptor set for Skill Asset: %w", err)
	}

	return nil
}

func backfillSkillOptions(options *smpb.Options) {
	if options == nil {
		return
	}

	// If SkillsServicesConfig is not present, we assume this skill provides the following skill
	// service gRPC interfaces to the platform.
	if options.GetSkillServicesConfig() == nil {
		options.SkillServicesConfig = &smpb.SkillServicesConfig{
			ServiceVersions: []smpb.SkillServicesConfig_ServiceVersion{
				smpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_PROJECTOR,
				smpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_EXECUTOR,
				smpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_SKILL_INFORMATION,
			},
		}
	}

}
