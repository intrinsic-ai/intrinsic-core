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

// Package servicebundle provides utils for working with Service bundles.
package servicebundle

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"intrinsic/assets/imageutils"
	"intrinsic/assets/ioutils"
	"intrinsic/assets/services/servicevalidate"
	"intrinsic/util/archive/tartooling"
	"intrinsic/util/proto/registryutil"

	"github.com/google/safearchive/tar"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"

	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	// LINT.IfChange(bundle_file)
	serviceManifestPathInTar = "service_manifest.binarypb"
	// LINT.ThenChange(//intrinsic/assets/bundle.go:service_bundle_file)
)

type writeOptions struct {
	defaultConfig     *anypb.Any
	fileDescriptorSet *descriptorpb.FileDescriptorSet
	imageTarPaths     []string
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// WithDefaultConfig specified the default configuration of the Service.
func WithDefaultConfig(defaultConfig *anypb.Any) WriteOption {
	return func(opts *writeOptions) {
		opts.defaultConfig = defaultConfig
	}
}

// WithFileDescriptorSet specifies the FileDescriptorSet to include in the bundle.
func WithFileDescriptorSet(fds *descriptorpb.FileDescriptorSet) WriteOption {
	return func(opts *writeOptions) {
		opts.fileDescriptorSet = fds
	}
}

// WithImageTarPaths provides the paths to the Service's container image .tar files.
func WithImageTarPaths(imageTarPaths []string) WriteOption {
	return func(opts *writeOptions) {
		opts.imageTarPaths = imageTarPaths
	}
}

// Write writes a Service .tar bundle to the given writer.
func Write(ctx context.Context, m *smpb.ServiceManifest, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("ServiceManifest must not be nil")
	}

	tw := tar.NewWriter(w)

	m.Assets = new(smpb.ServiceAssets)
	if opts.fileDescriptorSet != nil {
		descriptorName := "descriptors-transitive-descriptor-set.proto.bin"
		m.Assets.ParameterDescriptorFilename = &descriptorName
		if err := tartooling.AddBinaryProto(opts.fileDescriptorSet, tw, descriptorName); err != nil {
			return fmt.Errorf("failed to write FileDescriptorSet to bundle: %w", err)
		}
	}
	if opts.defaultConfig != nil {
		configName := "default_config.binarypb"
		m.Assets.DefaultConfigurationFilename = &configName
		if err := tartooling.AddBinaryProto(opts.defaultConfig, tw, configName); err != nil {
			return fmt.Errorf("failed to write default config to bundle: %w", err)
		}
	}
	for _, path := range opts.imageTarPaths {
		base := filepath.Base(path)
		m.Assets.ImageFilenames = append(m.Assets.ImageFilenames, base)
		if err := tartooling.AddFile(path, tw, base); err != nil {
			return fmt.Errorf("failed to write %q to bundle: %w", path, err)
		}
	}

	var files *protoregistry.Files
	if opts.fileDescriptorSet != nil {
		var err error
		files, err = protodesc.NewFiles(opts.fileDescriptorSet)
		if err != nil {
			return fmt.Errorf("failed to create proto files: %w", err)
		}
	}
	if err := servicevalidate.ServiceManifest(ctx, m, files, opts.defaultConfig); err != nil {
		return fmt.Errorf("invalid ServiceManifest: %w", err)
	}

	if err := tartooling.AddBinaryProto(m, tw, serviceManifestPathInTar); err != nil {
		return fmt.Errorf("failed to write ServiceManifest to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// WriteFile writes a Service .tar bundle to the specified path.
func WriteFile(ctx context.Context, m *smpb.ServiceManifest, path string, options ...WriteOption) error {
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

// ServiceBundle represents a Service Asset bundle.
type ServiceBundle struct {
	Manifest *smpb.ServiceManifest
	Files    map[string][]byte
}

type readOptions struct {
	readFiles bool
}

// ReadOption is a function option for Read.
type ReadOption func(*readOptions)

// WithReadFiles specifies whether to read additional files when reading the bundle.
func WithReadFiles(b bool) ReadOption {
	return func(opts *readOptions) {
		opts.readFiles = b
	}
}

// Read reads a Service Asset bundle from a reader.
func Read(ctx context.Context, r io.Reader, options ...ReadOption) (*ServiceBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	m, handlers := makeOnlyServiceManifestHandlers()
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

	return &ServiceBundle{
		Manifest: m,
		Files:    inlined,
	}, nil
}

// ReadFile is a helper to read a Service Asset bundle from a file path.
// It opens the file and calls Read.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*ServiceBundle, error) {
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

// Process creates a processed Service from a bundle reader.
func Process(ctx context.Context, r io.ReadSeeker, options ...ProcessOption) (*smpb.ProcessedServiceManifest, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}

	// Read the manifest and then reset the file once we have the information about the bundle we're
	// going to process.
	manifest, handlers := makeOnlyServiceManifestHandlers()
	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r), ioutils.WithHandlers(handlers)); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to read manifest: %w", err)
	}
	if _, err := r.Seek(0, io.SeekStart); err != nil {
		return nil, fmt.Errorf("failed to seek: %w", err)
	}

	// Initialize handlers for when we walk through the file again now that we know what we're looking
	// for, but error on unexpected files this time.
	processedAssets, handlers := makeServiceAssetHandlers(manifest, opts)
	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r),
		ioutils.WithHandlers(handlers),
		ioutils.WithFallbackHandler(ioutils.AlwaysErrorAsUnexpected),
	); err != nil {
		return nil, fmt.Errorf("failed to walk tar file to process assets: %w", err)
	}

	m := &smpb.ProcessedServiceManifest{
		Metadata:   manifest.GetMetadata(),
		ServiceDef: manifest.GetServiceDef(),
		Assets:     processedAssets,
	}

	if m.GetServiceDef().GetConfigMessageFullName() != "" {
		// Generate an empty default config if none was provided.
		if m.GetAssets().GetDefaultConfiguration() == nil {
			types, err := registryutil.NewTypesFromFileDescriptorSet(m.GetAssets().GetFileDescriptorSet())
			if err != nil {
				return nil, fmt.Errorf("failed to populate the registry: %v", err)
			}
			msgType, err := types.FindMessageByName(protoreflect.FullName(m.GetServiceDef().GetConfigMessageFullName()))
			if err != nil {
				return nil, fmt.Errorf("failed to find config message %q: %v", m.GetServiceDef().GetConfigMessageFullName(), err)
			}
			defaultConfig, err := anypb.New(msgType.New().Interface())
			if err != nil {
				return nil, fmt.Errorf("failed to create default config: %v", err)
			}
			m.GetAssets().DefaultConfiguration = defaultConfig
		}
	} else if m.GetAssets().GetDefaultConfiguration() != nil {
		// Derive config message name from the default config, if specified.
		m.GetServiceDef().ConfigMessageFullName = string(m.GetAssets().GetDefaultConfiguration().MessageName())
	}

	return m, nil
}

// ProcessFile is a helper to create a processed Service from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*smpb.ProcessedServiceManifest, error) {
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

// makeOnlyServiceManifestHandlers returns a map of handlers that only pull out the ServiceManifest
// from the tar file into the returned proto.
//
// Can be used with a fallback handler.
func makeOnlyServiceManifestHandlers() (*smpb.ServiceManifest, map[string]ioutils.WalkTarFileHandler) {
	manifest := new(smpb.ServiceManifest)
	handlers := map[string]ioutils.WalkTarFileHandler{
		serviceManifestPathInTar: ioutils.MakeBinaryProtoHandler(manifest),
	}
	return manifest, handlers
}

// makeServiceAssetHandlers returns handlers for all assets listed in the ServiceManifest.
//
// This will be at most:
// * A handler that ignores the manifest
// * A binary proto handler for the default configuration file
// * A binary proto handler for the file descriptor set file
// * A handler that wraps opts.imageProcessor to be called on every image
func makeServiceAssetHandlers(manifest *smpb.ServiceManifest, opts *processOptions) (*smpb.ProcessedServiceAssets, map[string]ioutils.WalkTarFileHandler) {
	handlers := map[string]ioutils.WalkTarFileHandler{
		serviceManifestPathInTar: ioutils.IgnoreHandler, // already read this.
	}
	// Don't generate an empty assets message if there wasn't one to begin with. This is a slightly
	// odd state, but Process is not doing validation of the manifest. This also protects against nil
	// access of manifest.GetAssets().{MemberVariable}, which is required for checking the "optional"
	// piece of "optional string" fields in this version of the golang proto API.
	if manifest.GetAssets() == nil {
		return nil, handlers
	}

	processedAssets := &smpb.ProcessedServiceAssets{
		FileDescriptorSet: &descriptorpb.FileDescriptorSet{},
	}
	if p := manifest.GetAssets().DefaultConfigurationFilename; p != nil {
		processedAssets.DefaultConfiguration = new(anypb.Any)
		handlers[*p] = ioutils.MakeBinaryProtoHandler(processedAssets.DefaultConfiguration)
	}
	if p := manifest.GetAssets().ParameterDescriptorFilename; p != nil {
		processedAssets.FileDescriptorSet = new(descriptorpb.FileDescriptorSet)
		handlers[*p] = ioutils.MakeBinaryProtoHandler(processedAssets.FileDescriptorSet)
	}
	for _, p := range manifest.GetAssets().GetImageFilenames() {
		if opts.imageProcessor == nil {
			handlers[p] = ioutils.IgnoreHandler
		} else {
			handlers[p] = func(ctx context.Context, r io.Reader) error {
				img, err := opts.imageProcessor(ctx, manifest.GetMetadata().GetId(), p, r)
				if err != nil {
					return fmt.Errorf("failed to process image: %v", err)
				}
				if processedAssets.Images == nil {
					processedAssets.Images = make(map[string]*ipb.Image)
				}
				processedAssets.Images[p] = img
				return nil
			}
		}
	}
	return processedAssets, handlers
}
