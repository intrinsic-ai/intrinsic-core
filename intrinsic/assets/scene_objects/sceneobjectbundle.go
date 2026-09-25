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

// Package sceneobjectbundle provides utils for working with SceneObject bundles.
package sceneobjectbundle

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"intrinsic/assets/ioutils"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/scene_objects/sceneobjectvalidate"
	"intrinsic/util/archive/tartooling"
	"intrinsic/util/go/pointer"

	"github.com/google/safearchive/tar"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoregistry"

	sompb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

const (
	// LINT.IfChange(bundle_file)
	sceneObjectManifestPathInTar = "scene_object_manifest.binarypb"
	// LINT.ThenChange(//intrinsic/assets/bundle.go:scene_object_bundle_file)
)

type writeOptions struct {
	fileDescriptorSet    *descriptorpb.FileDescriptorSet
	gzfGeometryFilePaths []string
	rootSceneObjectName  string
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// WithFileDescriptorSet specifies the FileDescriptorSet to include in the bundle.
func WithFileDescriptorSet(fds *descriptorpb.FileDescriptorSet) WriteOption {
	return func(opts *writeOptions) {
		opts.fileDescriptorSet = fds
	}
}

// WithGZFGeometryFilePaths specifies the GZF geometry files to include in the bundle.
func WithGZFGeometryFilePaths(paths []string) WriteOption {
	return func(opts *writeOptions) {
		opts.gzfGeometryFilePaths = paths
	}
}

// WithRootSceneObjectName specifies the name of the SceneObject's root object.
func WithRootSceneObjectName(name string) WriteOption {
	return func(opts *writeOptions) {
		opts.rootSceneObjectName = name
	}
}

// Write writes a SceneObject .tar bundle to a writer.
func Write(ctx context.Context, m *sompb.SceneObjectManifest, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("SceneObjectManifest must not be nil")
	}

	tw := tar.NewWriter(w)

	if m.GetAssets() != nil {
		return fmt.Errorf("manifest.assets must be nil")
	}

	m.Assets = &sompb.SceneObjectAssets{}
	m.Assets.RootSceneObjectName = opts.rootSceneObjectName
	if opts.fileDescriptorSet != nil {
		descriptorName := "file_descriptor_set.binpb"
		m.Assets.FileDescriptorSetFilename = pointer.To(descriptorName)
		if err := tartooling.AddBinaryProto(opts.fileDescriptorSet, tw, descriptorName); err != nil {
			return fmt.Errorf("failed to write FileDescriptorSet to bundle: %w", err)
		}
	}
	gzfPaths := map[string]string{}
	for _, path := range opts.gzfGeometryFilePaths {
		base := filepath.Base(path)
		gzfPaths[base] = path
		m.Assets.GzfGeometryFilenames = append(m.Assets.GzfGeometryFilenames, base)
		if err := tartooling.AddFile(path, tw, base); err != nil {
			return fmt.Errorf("failed to write %q to bundle: %w", path, err)
		}
	}

	var files *protoregistry.Files
	if opts.fileDescriptorSet != nil {
		var err error
		files, err = protodesc.NewFiles(opts.fileDescriptorSet)
		if err != nil {
			return fmt.Errorf("failed to populate registry: %w", err)
		}
	}

	if err := sceneobjectvalidate.SceneObjectManifest(ctx, m,
		sceneobjectvalidate.WithFiles(files),
		sceneobjectvalidate.WithGZFPaths(gzfPaths),
	); err != nil {
		return fmt.Errorf("invalid SceneObjectManifest: %w", err)
	}

	// Now we can write the manifest, since Assets have been completed.
	if err := tartooling.AddBinaryProto(m, tw, sceneObjectManifestPathInTar); err != nil {
		return fmt.Errorf("failed to write SceneObjectManifest to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// SceneObjectBundle represents a SceneObject Asset bundle.
type SceneObjectBundle struct {
	Manifest *sompb.SceneObjectManifest
	Files    map[string][]byte
}

type readOptions struct {
	readFiles bool
}

// ReadOption is a functional option for Read.
type ReadOption func(*readOptions)

// WithReadSceneObjectFiles specifies whether to read additional files when reading the bundle.
func WithReadSceneObjectFiles(b bool) ReadOption {
	return func(opts *readOptions) {
		opts.readFiles = b
	}
}

// Read reads a SceneObject Asset bundle from a reader.
func Read(ctx context.Context, r io.Reader, options ...ReadOption) (*SceneObjectBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	m, handlers := makeOnlySceneObjectManifestHandlers()
	walkTarOpts := []ioutils.WalkTarFileOption{
		ioutils.WithHandlers(handlers),
	}

	var inlined map[string][]byte
	if opts.readFiles {
		var fallback ioutils.WalkTarFileFallbackHandler
		inlined, fallback = ioutils.MakeCollectInlinedFallbackHandler()
		walkTarOpts = append(walkTarOpts, ioutils.WithFallbackHandler(fallback))
	}
	if err := ioutils.WalkTarFile(ctx, r, walkTarOpts...); err != nil {
		return nil, fmt.Errorf("failed to walk tar file: %w", err)
	}

	return &SceneObjectBundle{
		Manifest: m,
		Files:    inlined,
	}, nil
}

// ReadFile is a helper to read a SceneObject Asset bundle from a file path.
// It opens the file and calls Read.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*SceneObjectBundle, error) {
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
	gzfProcessor gzfprocessor.GZFProcessor
}

// ProcessOption is a functional option for Process.
type ProcessOption func(*processOptions)

// WithGZFProcessor specifies the GZFProcessor to use.
func WithGZFProcessor(p gzfprocessor.GZFProcessor) ProcessOption {
	return func(opts *processOptions) {
		opts.gzfProcessor = p
	}
}

// Process creates a processed SceneObject from a bundle reader.
func Process(ctx context.Context, r io.Reader, options ...ProcessOption) (*sompb.ProcessedSceneObjectManifest, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}
	if opts.gzfProcessor == nil {
		return nil, fmt.Errorf("gzfProcessor must not be nil")
	}

	var rs io.ReadSeeker
	if seeker, ok := r.(io.ReadSeeker); ok {
		rs = seeker
	} else {
		data, err := io.ReadAll(r)
		if err != nil {
			return nil, fmt.Errorf("failed to read bundle: %w", err)
		}
		rs = bytes.NewReader(data)
	}

	// Read the manifest and then reset the file once we have the information about the bundle we're
	// going to process.
	manifest, handlers := makeOnlySceneObjectManifestHandlers()
	if err := ioutils.WalkTarFile(ctx, rs, ioutils.WithHandlers(handlers)); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to read manifest: %w", err)
	}
	if _, err := rs.Seek(0, io.SeekStart); err != nil {
		return nil, fmt.Errorf("failed to seek: %w", err)
	}

	// Initialize handlers for when we walk through the file again now that we know what we're looking
	// for, but error on unexpected files this time.
	processedAssets, handlers, err := makeSceneObjectAssetHandlers(manifest, opts)
	if err != nil {
		return nil, fmt.Errorf("failed to make handlers: %w", err)
	}
	if err := ioutils.WalkTarFile(ctx, rs,
		ioutils.WithHandlers(handlers),
		ioutils.WithFallbackHandler(ioutils.AlwaysErrorAsUnexpected),
	); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to process assets: %w", err)
	}

	return &sompb.ProcessedSceneObjectManifest{
		Metadata: manifest.GetMetadata(),
		Assets:   processedAssets,
	}, nil
}

// ProcessFile is a helper to create a processed SceneObject from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*sompb.ProcessedSceneObjectManifest, error) {
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

// makeOnlySceneObjectManifestHandlers returns a map of handlers that only pull out the
// SceneObjectManifest from the tar file into the returned proto.
//
// Can be used with a fallback handler.
func makeOnlySceneObjectManifestHandlers() (*sompb.SceneObjectManifest, map[string]ioutils.WalkTarFileHandler) {
	manifest := new(sompb.SceneObjectManifest)
	handlers := map[string]ioutils.WalkTarFileHandler{
		sceneObjectManifestPathInTar: ioutils.MakeBinaryProtoHandler(manifest),
	}
	return manifest, handlers
}

// makeSceneObjectAssetHandlers returns handlers for all assets listed in the SceneObjectManifest.
//
// This will be:
// * A handler that ignores the manifest;
// * A handler that wraps opts.gzfProcessor to be called on every gzf file;
// * And optionally a binary proto handler for the file descriptor set file.
func makeSceneObjectAssetHandlers(manifest *sompb.SceneObjectManifest, opts *processOptions) (*sompb.ProcessedSceneObjectAssets, map[string]ioutils.WalkTarFileHandler, error) {
	handlers := map[string]ioutils.WalkTarFileHandler{
		sceneObjectManifestPathInTar: ioutils.IgnoreHandler, // already read this.
	}

	if manifest == nil {
		return nil, nil, fmt.Errorf("manifest must not be nil")
	}
	if manifest.GetAssets() == nil {
		return nil, nil, fmt.Errorf("manifest.assets must not be nil")
	}

	numGzfFiles := len(manifest.GetAssets().GetGzfGeometryFilenames())
	if numGzfFiles > 1 {
		return nil, handlers, fmt.Errorf("too many gzf files in SceneObject (got %d, expected 1)", numGzfFiles)
	}

	processedAssets := &sompb.ProcessedSceneObjectAssets{
		FileDescriptorSet: &descriptorpb.FileDescriptorSet{},
	}
	if p := manifest.GetAssets().FileDescriptorSetFilename; p != nil {
		handlers[*p] = ioutils.MakeBinaryProtoHandler(processedAssets.FileDescriptorSet)
	}
	for _, p := range manifest.GetAssets().GetGzfGeometryFilenames() {
		handlers[p] = func(ctx context.Context, r io.Reader) error {
			so, err := opts.gzfProcessor(ctx, r)
			if err != nil {
				return fmt.Errorf("failed to process object: %v", err)
			}
			if processedAssets.GetSceneObjectModel() != nil {
				return fmt.Errorf("SceneObject model is unexpectedly not nil")
			}
			processedAssets.SceneObjectModel = so
			return nil
		}
	}
	return processedAssets, handlers, nil
}

// WriteFile is a helper to write a SceneObject .tar bundle to a file path.
// It opens the file and calls Write.
func WriteFile(ctx context.Context, m *sompb.SceneObjectManifest, path string, options ...WriteOption) error {
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
