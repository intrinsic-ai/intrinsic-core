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

// Package databundle provides utils for working with Skill bundles.
package skillbundle

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"intrinsic/assets/imageutils"
	"intrinsic/assets/ioutils"
	"intrinsic/skills/skillvalidate"
	"intrinsic/util/archive/tartooling"

	"github.com/google/safearchive/tar"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"

	psmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"
	smpb "intrinsic/skills/proto/skill_manifest_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

const (
	// LINT.IfChange(bundle_file)
	skillManifestPathInTar = "skill_manifest.binpb"
	// LINT.ThenChange(//intrinsic/assets/bundle.go:skill_bundle_file)
)

type writeOptions struct {
	fileDescriptorSet *descriptorpb.FileDescriptorSet
	imageTarPath      string
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// WithFileDescriptorSet specifies the FileDescriptorSet to include in the bundle.
func WithFileDescriptorSet(fds *descriptorpb.FileDescriptorSet) WriteOption {
	return func(opts *writeOptions) {
		opts.fileDescriptorSet = fds
	}
}

// WithImageTarPath specifies the paths to the Skill's container image .tar file.
func WithImageTarPath(path string) WriteOption {
	return func(opts *writeOptions) {
		opts.imageTarPath = path
	}
}

// Write writes a Skill .tar bundle to the given writer.
func Write(ctx context.Context, m *smpb.SkillManifest, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("SkillManifest must not be nil")
	}

	tw := tar.NewWriter(w)

	m.Assets = &smpb.SkillAssets{}
	var fds *descriptorpb.FileDescriptorSet
	if opts.fileDescriptorSet != nil {
		fds = opts.fileDescriptorSet
		descriptorName := "descriptors-transitive-descriptor-set.proto.bin"
		m.Assets.FileDescriptorSetFilename = &descriptorName
		if err := tartooling.AddBinaryProto(opts.fileDescriptorSet, tw, descriptorName); err != nil {
			return fmt.Errorf("failed to write FileDescriptorSet to bundle: %w", err)
		}
	} else {
		fds = &descriptorpb.FileDescriptorSet{}
	}
	if opts.imageTarPath != "" {
		base := filepath.Base(opts.imageTarPath)
		m.Assets.DeploymentType = &smpb.SkillAssets_ImageFilename{
			ImageFilename: base,
		}
		if err := tartooling.AddFile(opts.imageTarPath, tw, base); err != nil {
			return fmt.Errorf("failed to write %q to bundle: %w", opts.imageTarPath, err)
		}
	}

	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return fmt.Errorf("failed to populate the registry: %w", err)
	}
	if err := skillvalidate.SkillManifest(ctx, m, files); err != nil {
		return fmt.Errorf("invalid SkillManifest: %w", err)
	}

	if err := tartooling.AddBinaryProto(m, tw, skillManifestPathInTar); err != nil {
		return fmt.Errorf("failed to write SkillManifest to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// WriteFile writes a Skill .tar bundle to the specified path.
func WriteFile(ctx context.Context, m *smpb.SkillManifest, path string, options ...WriteOption) error {
	if path == "" {
		return fmt.Errorf("path must not be empty")
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open %q for writing: %w", path, err)
	}
	defer f.Close()

	return Write(ctx, m, f, options...)
}

// SkillBundle represents a Skill Asset bundle.
type SkillBundle struct {
	Manifest *smpb.SkillManifest
	Files    map[string][]byte
}

type readOptions struct {
	readFiles bool
}

// ReadOption is a functional option for Read
type ReadOption func(*readOptions)

// WithReadFiles specifies whether to read additional files when reading the bundle.
func WithReadFiles(b bool) ReadOption {
	return func(opts *readOptions) {
		opts.readFiles = b
	}
}

// Read reads a Skill Asset bundle from a reader.
func Read(ctx context.Context, r io.Reader, options ...ReadOption) (*SkillBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	m, handlers := makeOnlySkillManifestHandlers()
	walkTarOpts := []ioutils.WalkTarFileOption{
		ioutils.WithHandlers(handlers),
	}

	var inlined map[string][]byte
	if opts.readFiles {
		var fallback ioutils.WalkTarFileFallbackHandler
		inlined, fallback = ioutils.MakeCollectInlinedFallbackHandler()
		walkTarOpts = append(walkTarOpts, ioutils.WithFallbackHandler(fallback))
	}

	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r), walkTarOpts...); err != nil {
		return nil, fmt.Errorf("failed to walk tar file: %w", err)
	}

	return &SkillBundle{
		Manifest: m,
		Files:    inlined,
	}, nil
}

// ReadFile is a helper to read a Skill Asset bundle from a file path.
// It opens the file and calls Read.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*SkillBundle, error) {
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
	imageProcessor imageutils.ImageProcessor
}

// ProcessOption is a functional option for Process.
type ProcessOption func(*processOptions)

// WithImageProcessor specifies the ImageProcessor to use.
func WithImageProcessor(p imageutils.ImageProcessor) ProcessOption {
	return func(opts *processOptions) {
		opts.imageProcessor = p
	}
}

// Process creates a processed Skill from a bundle reader.
func Process(ctx context.Context, r io.ReadSeeker, options ...ProcessOption) (*psmpb.ProcessedSkillManifest, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}

	// Read the manifest and then reset the file once we have the information about the bundle we're
	// going to process.
	manifest, handlers := makeOnlySkillManifestHandlers()
	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r), ioutils.WithHandlers(handlers)); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to read manifest: %w", err)
	}
	if _, err := r.Seek(0, io.SeekStart); err != nil {
		return nil, fmt.Errorf("failed to seek: %w", err)
	}

	// Initialize handlers for when we walk through the file again now that we know what we're looking
	// for, but error on unexpected files this time.
	processedAssets, handlers := makeSkillAssetHandlers(manifest, opts)
	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r),
		ioutils.WithHandlers(handlers),
		ioutils.WithFallbackHandler(ioutils.AlwaysErrorAsUnexpected),
	); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to process assets: %w", err)
	}

	psm := &psmpb.ProcessedSkillManifest{
		Assets: processedAssets,
	}
	m := &psmpb.SkillMetadata{
		Id:            manifest.GetId(),
		Vendor:        manifest.GetVendor(),
		Documentation: manifest.GetDocumentation(),
		DisplayName:   manifest.GetDisplayName(),
	}
	// We do this to avoid adding empty sub messages.
	if !proto.Equal(m, &psmpb.SkillMetadata{}) {
		psm.Metadata = m
	}
	d := &psmpb.SkillDetails{
		Options:       manifest.GetOptions(),
		Dependencies:  manifest.GetDependencies(),
		Parameter:     manifest.GetParameter(),
		ExecuteResult: manifest.GetReturnType(),
		StatusInfo:    manifest.GetStatusInfo(),
	}
	// We do this to avoid adding empty sub messages.
	if !proto.Equal(d, &psmpb.SkillDetails{}) {
		psm.Details = d
	}

	return psm, nil
}

// ProcessFile is a helper to create a processed Skill from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*psmpb.ProcessedSkillManifest, error) {
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

// makeOnlySkillManifestHandlers returns a map of handlers that only pull out the SkillManifest from
// the tar file into the returned proto.
//
// Can be used with a fallback handler.
func makeOnlySkillManifestHandlers() (*smpb.SkillManifest, map[string]ioutils.WalkTarFileHandler) {
	manifest := new(smpb.SkillManifest)
	handlers := map[string]ioutils.WalkTarFileHandler{
		skillManifestPathInTar: ioutils.MakeBinaryProtoHandler(manifest),
	}
	return manifest, handlers
}

// makeSkillAssetHandlers returns handlers for all assets listed in the SkillManifest.
//
// This will be at most:
// * A handler that ignores the manifest
// * A binary proto handler for the file descriptor set file
// * A handler that wraps opts.ImageProcessor to be called on every image
// * A binary proto handler for the parameterized behavior tree file
func makeSkillAssetHandlers(manifest *smpb.SkillManifest, opts *processOptions) (*psmpb.ProcessedSkillAssets, map[string]ioutils.WalkTarFileHandler) {
	handlers := map[string]ioutils.WalkTarFileHandler{
		skillManifestPathInTar: ioutils.IgnoreHandler, // already read this.
	}
	// Don't generate an empty assets message if there wasn't one to begin with. This is a slightly
	// odd state, but Process is not doing validation of the manifest. This also protects against nil
	// access of manifest.GetAssets().{MemberVariable}, which is required for checking the "optional"
	// piece of "optional string" fields in this version of the golang proto API.
	if manifest.GetAssets() == nil {
		return nil, handlers
	}

	processedAssets := &psmpb.ProcessedSkillAssets{}
	if p := manifest.GetAssets().FileDescriptorSetFilename; p != nil {
		processedAssets.FileDescriptorSet = new(descriptorpb.FileDescriptorSet)
		handlers[*p] = ioutils.MakeBinaryProtoHandler(processedAssets.FileDescriptorSet)
	}
	switch manifest.GetAssets().GetDeploymentType().(type) {
	case *smpb.SkillAssets_ImageFilename:
		p := manifest.GetAssets().GetImageFilename()
		handlers[p] = func(ctx context.Context, r io.Reader) error {
			img, err := opts.imageProcessor(ctx, manifest.GetId(), p, r)
			if err != nil {
				return fmt.Errorf("failed to process image: %v", err)
			}
			processedAssets.DeploymentType = &psmpb.ProcessedSkillAssets_Image{
				Image: img,
			}
			return nil
		}
	}
	return processedAssets, handlers
}
