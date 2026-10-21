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

// Package processvalidate provides utils for validating Processes.
package processvalidate

import (
	"context"
	"fmt"

	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/metadatautils"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	papb "intrinsic/assets/processes/proto/process_asset_go_proto"
	pmpb "intrinsic/assets/processes/proto/process_manifest_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	docpb "intrinsic/assets/proto/documentation_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
)

var (
	// ErrMissingBehaviorTree is returned when a Process is missing a behavior tree.
	ErrMissingBehaviorTree = status.Errorf(codes.InvalidArgument, "'behavior_tree' must be specified")

	// ErrBehaviorTreeNameInconsistent is returned when a Process has an inconsistent behavior tree
	// name.
	ErrBehaviorTreeNameInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.name' must match 'metadata.display_name'")

	// ErrSkillProtoMissing is returned when the behavior tree of a Process does not have a Skill
	// proto.
	ErrSkillProtoMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description' must be set")

	// ErrBehaviorTreeDescriptionMissing is returned when the behavior tree of a Process has a Skill
	// proto but the behavior tree description is missing.
	ErrBehaviorTreeDescriptionMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.behavior_tree_description' must be set")

	// ErrSkillNameMissing is returned when the behavior tree of a Process has a Skill proto but the
	// Skill name is missing.
	ErrSkillNameMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.skill_name' is missing but must be equal to 'metadata.id_version.id.name'")

	// ErrSkillNameInconsistent is returned when the behavior tree of a Process has a Skill proto but
	// the Skill name does not match the Asset name.
	ErrSkillNameInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.skill_name' must be equal to 'metadata.id_version.id.name'")

	// ErrSkillPackageNameMissing is returned when the behavior tree of a Process has a Skill proto
	// but the Skill package name is missing.
	ErrSkillPackageNameMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.package_name' is missing but must be equal to 'metadata.id_version.id.package'")

	// ErrSkillPackageNameInconsistent is returned when the behavior tree of a Process has a Skill
	// proto but the Skill package name does not match the Asset package name.
	ErrSkillPackageNameInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.package_name' must be equal to 'metadata.id_version.id.package'")

	// ErrSkillIDMissing is returned when the behavior tree of a Process has a Skill proto but the
	// Skill ID is missing.
	ErrSkillIDMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.id' is missing but must be equal to 'metadata.id_version.id'")

	// ErrSkillIDInconsistent is returned when the behavior tree of a Process has a Skill proto but
	// the Skill ID does not match the Asset ID.
	ErrSkillIDInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.id' must be equal to 'metadata.id_version.id'")

	// ErrSkillIDVersionInconsistent is returned when the behavior tree of a Process has a Skill proto
	// but the skill ID version does not match the Asset ID version.
	ErrSkillIDVersionInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.id_version' must be equal to 'metadata.id_version'")

	// ErrSkillDescriptionMissing is returned when the behavior tree of a Process has a Skill proto
	// but the Skill description is missing.
	ErrSkillDescriptionMissing = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.description' is missing but must be equal to 'metadata.documentation.description'")

	// ErrSkillDescriptionInconsistent is returned when the behavior tree of a Process has a Skill
	// proto but the Skill description does not match the Asset documentation.
	ErrSkillDescriptionInconsistent = status.Errorf(
		codes.InvalidArgument,
		"'behavior_tree.description.description' must be equal to 'metadata.documentation.description'")
)

// ProcessManifest validates a ProcessManifest.
func ProcessManifest(ctx context.Context, m *pmpb.ProcessManifest) error {
	if m == nil {
		return fmt.Errorf("ProcessManifest must not be nil")
	}

	metadata := m.GetMetadata()
	if err := metadatautils.ValidateManifestMetadata(metadata); err != nil {
		return fmt.Errorf("invalid ProcessManifest metadata: %w", err)
	}

	if err := validateBehaviorTree(m.GetBehaviorTree(), validateBehaviorTreeOptions{
		assetID:            metadata.GetId(),
		assetDisplayName:   metadata.GetDisplayName(),
		assetDocumentation: metadata.GetDocumentation(),
		// In the manifest the Skill proto in the BehaviorTree is allowed to be missing or can be filled
		// partially. However, metadata fields which are filled in the Skill proto must be consistent
		// with the Asset metadata.
		requireFilledSkillMetadata: false,
	}); err != nil {
		return err
	}

	return nil
}

type processAssetOptions struct {
	ignoreVersion bool
	report        *report.Report
}

// ProcessAssetOption is an option for validating a ProcessAsset.
type ProcessAssetOption func(*processAssetOptions)

// WithIgnoreVersion specifies whether to ignore whether or not a version is specified in the
// Process' metadata.
func WithIgnoreVersion(ignore bool) ProcessAssetOption {
	return func(opts *processAssetOptions) {
		opts.ignoreVersion = ignore
	}
}

// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) ProcessAssetOption {
	return func(opts *processAssetOptions) {
		opts.report = report
	}
}

// ProcessAsset validates a ProcessAsset.
func ProcessAsset(ctx context.Context, pa *papb.ProcessAsset, options ...ProcessAssetOption) error {
	opts := &processAssetOptions{
		report: report.New(),
	}
	for _, opt := range options {
		opt(opts)
	}

	if pa == nil {
		return fmt.Errorf("ProcessAsset must not be nil")
	}

	inAssetOption := metadatautils.WithInAssetOptions()
	if opts.ignoreVersion {
		inAssetOption = metadatautils.WithInAssetOptionsIgnoreVersion()
	}

	metadata := pa.GetMetadata()
	if err := metadatautils.ValidateMetadata(metadata,
		metadatautils.WithAssetType(atypepb.AssetType_ASSET_TYPE_PROCESS),
		inAssetOption,
	); err != nil {
		return fmt.Errorf("invalid ProcessAsset metadata: %w", err)
	}

	if err := validateBehaviorTree(pa.GetBehaviorTree(), validateBehaviorTreeOptions{
		assetID:            metadata.GetIdVersion().GetId(),
		assetDisplayName:   metadata.GetDisplayName(),
		assetDocumentation: metadata.GetDocumentation(),
		// In the processed Asset the Skill proto in the BehaviorTree must be set and must be filled
		// consistently with the asset metadata.
		requireFilledSkillMetadata: true,
	}); err != nil {
		return err
	}

	return nil
}

type validateBehaviorTreeOptions struct {
	assetID                    *idpb.Id
	assetDisplayName           string
	assetDocumentation         *docpb.Documentation
	requireFilledSkillMetadata bool
}

// FillBackwardsCompatibleVersion ensures that a ProcessAsset represents the version info required
// by older validation code.
//
// TODO(b/449824543): Remove this once older validation is no longer supported.
func FillBackwardsCompatibleVersion(pa *papb.ProcessAsset, version string) {
	pa.GetMetadata().GetIdVersion().Version = version

	if pa.GetBehaviorTree() != nil && pa.GetBehaviorTree().GetDescription() != nil {
		pa.GetBehaviorTree().GetDescription().IdVersion = idutils.IDVersionFromProtoUnchecked(pa.GetMetadata().GetIdVersion())
	}
}

// validateBehaviorTree validates the given behavior tree for a Process.
//
// In particular, checks the consistency of the behavior tree's name and skill proto (if present)
// with the Asset metadata.
func validateBehaviorTree(bt *btpb.BehaviorTree, options validateBehaviorTreeOptions) error {
	if bt == nil {
		return ErrMissingBehaviorTree
	}

	if bt.GetName() != options.assetDisplayName {
		return ErrBehaviorTreeNameInconsistent
	}

	skill := bt.GetDescription()

	if skill == nil {
		if options.requireFilledSkillMetadata {
			return ErrSkillProtoMissing
		}
		return nil
	}

	if options.requireFilledSkillMetadata && skill.BehaviorTreeDescription == nil {
		return ErrBehaviorTreeDescriptionMissing
	}

	// These metadata fields are redundant with the Asset's metadata. They should match the Asset's
	// metadata or can be empty (if `requireFilledSkillMetadata` is false).
	if skill.SkillName != "" {
		if skill.SkillName != options.assetID.GetName() {
			return ErrSkillNameInconsistent
		}
	} else if options.requireFilledSkillMetadata {
		return ErrSkillNameMissing
	}

	if skill.PackageName != "" {
		if skill.PackageName != options.assetID.GetPackage() {
			return ErrSkillPackageNameInconsistent
		}
	} else if options.requireFilledSkillMetadata {
		return ErrSkillPackageNameMissing
	}

	if skill.Id != "" {
		if skill.Id != idutils.IDFromProtoUnchecked(options.assetID) {
			return ErrSkillIDInconsistent
		}
	} else if options.requireFilledSkillMetadata {
		return ErrSkillIDMissing
	}

	if skill.IdVersion != "" {
		ivp, err := idutils.NewIDVersionParts(skill.IdVersion)
		if err != nil {
			return err
		}
		if ivp.ID() != idutils.IDFromProtoUnchecked(options.assetID) {
			return ErrSkillIDVersionInconsistent
		}
	}

	if skill.Description != "" {
		if skill.Description != options.assetDocumentation.GetDescription() {
			return ErrSkillDescriptionInconsistent
		}
	} else if options.requireFilledSkillMetadata && options.assetDocumentation.GetDescription() != "" {
		return ErrSkillDescriptionMissing
	}

	return nil
}
