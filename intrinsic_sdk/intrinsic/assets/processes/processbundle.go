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

// Package databundle provides utils for working with Process bundles.
package processbundle

import (
	"context"
	"fmt"
	"io"
	"os"

	"intrinsic/assets/ioutils"
	"intrinsic/assets/processes/processmanifest"
	"intrinsic/assets/processes/processvalidate"
	"intrinsic/util/archive/tartooling"
	"intrinsic/util/proto/fieldbehavior"

	"github.com/google/safearchive/tar"

	processassetpb "intrinsic/assets/processes/proto/process_asset_go_proto"
	processmanifestpb "intrinsic/assets/processes/proto/process_manifest_go_proto"
	assettypepb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
)

const (
	// LINT.IfChange(bundle_file)
	processManifestFileName = "process_manifest.binpb"
	// LINT.ThenChange(//intrinsic_sdk/intrinsic/assets/bundle.go:process_bundle_file)
)

type writeOptions struct {
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// Write writes a Process Asset .tar bundle to the given writer.
func Write(ctx context.Context, manifest *processmanifestpb.ProcessManifest, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if manifest == nil {
		return fmt.Errorf("ProcessManifest must not be nil")
	}
	if err := fieldbehavior.ClearOutputOnly(manifest); err != nil {
		return fmt.Errorf("failed to clear output-only fields: %w", err)
	}
	err := processvalidate.ProcessManifest(ctx, manifest)
	if err != nil {
		return fmt.Errorf("invalid ProcessManifest: %w", err)
	}

	tw := tar.NewWriter(w)

	if err := tartooling.AddBinaryProto(manifest, tw, processManifestFileName); err != nil {
		return fmt.Errorf("failed write ProcessManifest to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// WriteFile writes a Process Asset .tar bundle to the specified path.
func WriteFile(ctx context.Context, manifest *processmanifestpb.ProcessManifest, path string, options ...WriteOption) error {
	if path == "" {
		return fmt.Errorf("path must not be empty")
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open %q for writing: %w", path, err)
	}
	defer f.Close()

	return Write(ctx, manifest, f, options...)
}

// ManifestFromAsset extracts from the given ProcessAsset a ProcessManifest which is suitable for
// creating a corrensponding bundle from it.
func ManifestFromAsset(pa *processassetpb.ProcessAsset) (*processmanifestpb.ProcessManifest, error) {
	if pa == nil {
		return nil, fmt.Errorf("ProcessAsset must not be nil")
	}

	manifest := &processmanifestpb.ProcessManifest{
		Metadata: &processmanifestpb.ProcessMetadata{
			Id:            pa.GetMetadata().GetIdVersion().GetId(),
			DisplayName:   pa.GetMetadata().GetDisplayName(),
			Documentation: pa.GetMetadata().GetDocumentation(),
			Vendor:        pa.GetMetadata().GetVendor(),
			AssetTag:      pa.GetMetadata().GetAssetTag(),
		},
		BehaviorTree: pa.GetBehaviorTree(),
	}

	if len(pa.GetAssets()) > 0 {
		manifest.Assets = make(map[string]*processmanifestpb.ProcessManifest_Asset, len(pa.GetAssets()))
		for refID, refAsset := range pa.GetAssets() {
			switch v := refAsset.GetVariant().(type) {
			case *processassetpb.ProcessAsset_ProcessedAsset_Catalog:
				manifest.Assets[refID] = &processmanifestpb.ProcessManifest_Asset{
					Variant: &processmanifestpb.ProcessManifest_Asset_Catalog{
						Catalog: v.Catalog,
					},
				}
			default:
				return nil, fmt.Errorf("unsupported Asset variant for Asset %q: %T", refID, v)
			}
		}
	}

	// Clear the ID version from the Skill metadata in the BehaviorTree. The manifest does not contain
	// a version and the behavior tree on it should not be referencing one either for consistency.
	// This can be seen as the counterpart of [processmanifest.FillInSkillMetadataFromAssetMetadata]
	// in [Process]. The remaining fields of the Skill metadata are assumed to be valid/consistent.
	skill := manifest.GetBehaviorTree().GetDescription()
	if skill != nil {
		skill.IdVersion = ""
	}

	return manifest, nil
}

// WriteFromAsset writes a Process Asset .tar bundle to the given writer, given a ProcessAsset.
func WriteFromAsset(ctx context.Context, pa *processassetpb.ProcessAsset, w io.Writer, options ...WriteOption) error {
	manifest, err := ManifestFromAsset(pa)
	if err != nil {
		return err
	}

	return Write(ctx, manifest, w, options...)
}

// WriteFileFromAsset writes a Process Asset .tar bundle to the specified path, given a ProcessAsset.
func WriteFileFromAsset(ctx context.Context, pa *processassetpb.ProcessAsset, path string, options ...WriteOption) error {
	manifest, err := ManifestFromAsset(pa)
	if err != nil {
		return err
	}

	return WriteFile(ctx, manifest, path, options...)
}

// ProcessBundle represents a Process Asset bundle.
type ProcessBundle struct {
	Manifest *processmanifestpb.ProcessManifest
}

type readOptions struct {
}

// ReadOption is a functional option for Read.
type ReadOption func(*readOptions)

// Read reads a Process Asset bundle from a reader.
func Read(ctx context.Context, r io.Reader, options ...ReadOption) (*ProcessBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	// Read single file from the bundle.
	tr := tar.NewReader(r)
	header, err := tr.Next()
	if err != nil {
		return nil, fmt.Errorf("failed to read first entry of Process bundle: %w", err)
	}
	if header.Typeflag != tar.TypeReg {
		return nil, fmt.Errorf("unexpected entry type in Process bundle: %v", header.Typeflag)
	}
	if header.Name != processManifestFileName {
		return nil, fmt.Errorf("unexpected file in Process bundle: %v", header.Name)
	}

	manifest := &processmanifestpb.ProcessManifest{}
	if err = ioutils.ReadBinaryProto(tr, manifest); err != nil {
		return nil, fmt.Errorf("failed to read ProcessManifest proto in bundle: %w", err)
	}

	// Fail if there are other files in the bundle.
	header, err = tr.Next()
	if err != io.EOF {
		if err != nil {
			return nil, fmt.Errorf("failed to read second entry from Process bundle: %w", err)
		}
		return nil, fmt.Errorf("unexpected second entry in Process bundle: %v", header.Name)
	}

	return &ProcessBundle{
		Manifest: manifest,
	}, nil
}

// ReadFile is a helper to read a Process Asset bundle from a file path.
// It opens the file and calls Read.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*ProcessBundle, error) {
	if path == "" {
		return nil, fmt.Errorf("path must not be empty")
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q for reading: %w", path, err)
	}
	defer f.Close()
	bundle, err := Read(ctx, f, options...)
	if err != nil {
		return nil, fmt.Errorf("failed to read bundle from %q: %w", path, err)
	}
	return bundle, nil
}

type processOptions struct {
	readOptions []ReadOption
}

// ProcessOption is a functional option for Process.
type ProcessOption func(*processOptions)

// WithReadOptions provides options to pass to Read.
func WithReadOptions(options ...ReadOption) ProcessOption {
	return func(opts *processOptions) {
		opts.readOptions = options
	}
}

// Process creates a processed Process from a bundle reader.
func Process(ctx context.Context, r io.Reader, options ...ProcessOption) (*processassetpb.ProcessAsset, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}

	bundle, err := Read(ctx, r, opts.readOptions...)
	if err != nil {
		return nil, fmt.Errorf("failed to read Process bundle: %w", err)
	}
	manifest := bundle.Manifest

	asset := &processassetpb.ProcessAsset{
		Metadata: &metadatapb.Metadata{
			IdVersion: &idpb.IdVersion{
				Id: manifest.GetMetadata().GetId(),
			},
			DisplayName:   manifest.GetMetadata().GetDisplayName(),
			Documentation: manifest.GetMetadata().GetDocumentation(),
			Vendor:        manifest.GetMetadata().GetVendor(),
			AssetType:     assettypepb.AssetType_ASSET_TYPE_PROCESS,
			AssetTag:      manifest.GetMetadata().GetAssetTag(),
		},
		BehaviorTree: manifest.GetBehaviorTree(),
	}

	if len(manifest.GetAssets()) > 0 {
		asset.Assets = make(map[string]*processassetpb.ProcessAsset_ProcessedAsset, len(manifest.GetAssets()))
		for refID, refAsset := range manifest.GetAssets() {
			switch v := refAsset.GetVariant().(type) {
			case *processmanifestpb.ProcessManifest_Asset_Catalog:
				asset.Assets[refID] = &processassetpb.ProcessAsset_ProcessedAsset{
					Variant: &processassetpb.ProcessAsset_ProcessedAsset_Catalog{
						Catalog: v.Catalog,
					},
				}
			default:
				return nil, fmt.Errorf("unsupported Asset variant for Asset %q: %T", refID, v)
			}
		}
	}

	// Update the Skill metadata in the BehaviorTree to match the Process asset's metadata. In the
	// manifest the affected fields in the Skill metadata are allowed to be empty but need to be
	// filled in the processed Asset.
	processmanifest.FillInSkillMetadataFromAssetMetadata(
		asset.GetBehaviorTree(), asset.GetMetadata(),
	)

	return asset, nil
}

// ProcessFile is a helper to create a processed Process from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*processassetpb.ProcessAsset, error) {
	if path == "" {
		return nil, fmt.Errorf("path must not be empty")
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q for reading: %w", path, err)
	}
	defer f.Close()
	m, err := Process(ctx, f, options...)
	if err != nil {
		return nil, fmt.Errorf("failed to process bundle from %q: %w", path, err)
	}
	return m, nil
}
