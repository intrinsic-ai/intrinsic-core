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

// Package databundle provides utils for working with HardwareDevice bundles.
package hardwaredevicebundle

import (
	"context"
	"fmt"
	"io"
	"os"
	"path"
	"regexp"

	"intrinsic/assets/data/databundle"
	"intrinsic/assets/hardware_devices/hardwaredevicevalidate"
	"intrinsic/assets/idutils"
	"intrinsic/assets/imageutils"
	"intrinsic/assets/ioutils"
	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/scene_objects/sceneobjectbundle"
	"intrinsic/assets/services/servicebundle"
	"intrinsic/util/archive/tartooling"

	"github.com/google/safearchive/tar"

	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
)

const (
	// LINT.IfChange(bundle_file)
	hardwareDeviceManifestFileName = "hardware_device_manifest.binpb"
	// LINT.ThenChange(//intrinsic/assets/bundle.go:hardware_device_bundle_file)
)

var tarBundlePathRegex = regexp.MustCompile(`^assets/(?P<key>[^/]+)\.bundle\.tar$`)

type writeOptions struct {
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// Write writes a HardwareDevice Asset .tar bundle to the given writer.
//
// The bundles of local Assets are included in the HardwareDevice .tar bundle.
//
// The input manifest may be mutated by this function.
func Write(ctx context.Context, hdm *hdmpb.HardwareDeviceManifest, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if hdm == nil {
		return fmt.Errorf("HardwareDeviceManifest must not be nil")
	}
	if err := hardwaredevicevalidate.HardwareDeviceManifest(ctx, hdm); err != nil {
		return fmt.Errorf("invalid HardwareDeviceManifest: %w", err)
	}

	tw := tar.NewWriter(w)

	// Save local Assets into the bundle and update their paths in the manifest.
	for key, asset := range hdm.GetAssets() {
		switch asset.GetVariant().(type) {
		case *hdmpb.HardwareDeviceManifest_Asset_Local:
			tarPath := tarBundlePathFrom(key)
			if err := tartooling.AddFile(asset.GetLocal().GetBundlePath(), tw, tarPath); err != nil {
				return fmt.Errorf("failed to add local asset %s to bundle: %w", key, err)
			}
			asset.GetLocal().BundlePath = tarPath
		}
	}

	if err := tartooling.AddBinaryProto(hdm, tw, hardwareDeviceManifestFileName); err != nil {
		return fmt.Errorf("failed to write HardwareDeviceManifest to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// WriteFile writes a HardwareDevice Asset .tar bundle to the specified path.
func WriteFile(ctx context.Context, hdm *hdmpb.HardwareDeviceManifest, path string, options ...WriteOption) error {
	if path == "" {
		return fmt.Errorf("path must not be empty")
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open %q for writing: %w", path, err)
	}
	defer f.Close()

	return Write(ctx, hdm, f, options...)
}

// HardwareDeviceBundle represents a HardwareDevice Asset bundle.
type HardwareDeviceBundle struct {
	Manifest *hdmpb.HardwareDeviceManifest
}

type readOptions struct {
	extractLocalAssetsDir string
}

// ReadOption is a functional option for Read.
type ReadOption func(*readOptions)

// WithExtractLocalAssetsDir provides a directory to which to extract local Asset bundles.
//
// If specified, local Asset bundles will be extracted to this directory, and bundle paths updated
// in the returned manifest. The directory must already exist.
func WithExtractLocalAssetsDir(dir string) ReadOption {
	return func(opts *readOptions) {
		opts.extractLocalAssetsDir = dir
	}
}

// Read reads a HardwareDevice Asset bundle from a reader.
func Read(ctx context.Context, r io.Reader, options ...ReadOption) (*HardwareDeviceBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	// Process the .tar bundle.
	var hdm *hdmpb.HardwareDeviceManifest
	extractedBundlePaths := map[string]string{}
	manifestHandler := func(ctx context.Context, r io.Reader) error {
		hdm = &hdmpb.HardwareDeviceManifest{}
		if err := ioutils.ReadBinaryProto(r, hdm); err != nil {
			return fmt.Errorf("failed to read HardwareDeviceManifest: %w", err)
		}
		return nil
	}
	fallbackHandler := func(ctx context.Context, n string, r io.Reader) error {
		if key, ok := tryExtractAssetKey(n); ok {
			// Ignore local asset bundles if no extraction directory was provided.
			if opts.extractLocalAssetsDir == "" {
				return nil
			}
			if _, ok := extractedBundlePaths[key]; ok {
				return fmt.Errorf("duplicate local asset bundle %q", key)
			}
			extractedBundlePaths[key] = path.Join(opts.extractLocalAssetsDir, fmt.Sprintf("%s.bundle.tar", key))
			if err := writeToFile(r, extractedBundlePaths[key]); err != nil {
				return fmt.Errorf("failed to write local asset bundle %q: %w", key, err)
			}
			return nil
		}
		return fmt.Errorf("unexpected file %q", n)
	}
	if err := ioutils.WalkTarFile(ctx, tar.NewReader(r),
		ioutils.WithHandlers(map[string]ioutils.WalkTarFileHandler{
			hardwareDeviceManifestFileName: manifestHandler,
		}),
		ioutils.WithFallbackHandler(fallbackHandler),
	); err != nil {
		return nil, fmt.Errorf("failed to process tar file: %w", err)
	}

	// Replace local asset bundle paths with their extracted paths.
	if opts.extractLocalAssetsDir != "" {
		for key, asset := range hdm.GetAssets() {
			switch asset.GetVariant().(type) {
			case *hdmpb.HardwareDeviceManifest_Asset_Local:
				extractedPath, ok := extractedBundlePaths[key]
				if !ok {
					return nil, fmt.Errorf("extracted bundle path for local asset %s not found", key)
				}
				asset.GetLocal().BundlePath = extractedPath
			}
		}
	}

	return &HardwareDeviceBundle{
		Manifest: hdm,
	}, nil
}

// ReadFile is a helper to read a HardwareDevice Asset bundle from a file path.
// It opens the file and calls Read.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*HardwareDeviceBundle, error) {
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

// AssetProcessor is a function that processes a single Asset in a HardwareDeviceManifest.
type AssetProcessor func(ctx context.Context, a *hdmpb.HardwareDeviceManifest_Asset) (*hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset, error)

// PassThrough is an AssetProcessor that passes AssetCatalog references through unchanged.
//
// This processor only applies to catalog Assets.
func PassThrough(ctx context.Context, a *hdmpb.HardwareDeviceManifest_Asset) (*hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset, error) {
	switch a.GetVariant().(type) {
	case *hdmpb.HardwareDeviceManifest_Asset_Catalog:
		return &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset{
			Variant: &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Catalog{
				Catalog: a.GetCatalog(),
			},
		}, nil
	default:
		return nil, fmt.Errorf("unsupported asset type: %T", a.GetVariant())
	}
}

// LocalAssetInlinerOptions contains options for LocalAssetInliner.
type LocalAssetInlinerOptions struct {
	imageutils.ImageProcessor
	gzfprocessor.GZFProcessor
	// ReferencedDataProcessor is the referenceddata.Processor to use for Data assets (see
	// databundle.Read).
	ReferencedDataProcessor referenceddata.Processor
}

// LocalAssetInliner processes local Assets in a HardwareDevice by inlining them.
//
// Its Process method can be provided as an AssetProcessor to Process.
type LocalAssetInliner struct {
	opts LocalAssetInlinerOptions
}

// Process is an AssetProcessor that processes a local Asset bundle.
func (p *LocalAssetInliner) Process(ctx context.Context, a *hdmpb.HardwareDeviceManifest_Asset) (*hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset, error) {
	switch a.GetVariant().(type) {
	case *hdmpb.HardwareDeviceManifest_Asset_Local:
		switch at := a.GetLocal().GetAssetType(); at {
		case atpb.AssetType_ASSET_TYPE_SERVICE:
			psm, err := servicebundle.ProcessFile(ctx, a.GetLocal().GetBundlePath(),
				servicebundle.WithImageProcessor(p.opts.ImageProcessor),
			)
			if err != nil {
				return nil, fmt.Errorf("failed to process Service %s: %w", idutils.IDFromProtoUnchecked(a.GetLocal().GetId()), err)
			}
			return &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset{
				Variant: &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Service{
					Service: psm,
				},
			}, nil

		case atpb.AssetType_ASSET_TYPE_SCENE_OBJECT:
			psom, err := sceneobjectbundle.ProcessFile(ctx, a.GetLocal().GetBundlePath(),
				sceneobjectbundle.WithGZFProcessor(p.opts.GZFProcessor),
			)
			if err != nil {
				return nil, fmt.Errorf("failed to process SceneObject %s: %w", idutils.IDFromProtoUnchecked(a.GetLocal().GetId()), err)
			}
			return &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset{
				Variant: &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_SceneObject{
					SceneObject: psom,
				},
			}, nil

		case atpb.AssetType_ASSET_TYPE_DATA:
			var opts []databundle.ReadOption
			if p.opts.ReferencedDataProcessor != nil {
				opts = append(opts, databundle.WithReferencedDataProcessor(p.opts.ReferencedDataProcessor))
			}
			da, err := databundle.ProcessFile(ctx, a.GetLocal().GetBundlePath(),
				databundle.WithReadOptions(opts...),
			)
			if err != nil {
				return nil, fmt.Errorf("failed to process Data Asset %s: %w", idutils.IDFromProtoUnchecked(a.GetLocal().GetId()), err)
			}
			return &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset{
				Variant: &hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Data{
					Data: da,
				},
			}, nil
		default:
			return nil, fmt.Errorf("unsupported local Asset type: %s", at)
		}
	case *hdmpb.HardwareDeviceManifest_Asset_Catalog:
		return PassThrough(ctx, a)
	default:
		return nil, fmt.Errorf("unsupported Asset variant: %T", a.GetVariant())
	}
}

// NewLocalAssetInliner creates a LocalAssetInliner with the given options.
func NewLocalAssetInliner(opts LocalAssetInlinerOptions) *LocalAssetInliner {
	return &LocalAssetInliner{opts: opts}
}

type processOptions struct {
	processAsset AssetProcessor
	readOptions  []ReadOption
}

// ProcessOption is a functional option for Process.
type ProcessOption func(*processOptions)

// WithProcessAsset provides a function to process a single Asset in the HardwareDeviceManifest.
//
// If unspecified, a default processor will be used.
func WithProcessAsset(f AssetProcessor) ProcessOption {
	return func(opts *processOptions) {
		opts.processAsset = f
	}
}

// WithReadOptions provides options to pass to Read.
func WithReadOptions(options ...ReadOption) ProcessOption {
	return func(opts *processOptions) {
		opts.readOptions = options
	}
}

// Process creates a processed HardwareDevice from a bundle reader.
func Process(ctx context.Context, r io.Reader, options ...ProcessOption) (*hdmpb.ProcessedHardwareDeviceManifest, error) {
	opts := &processOptions{
		processAsset: PassThrough,
	}
	for _, opt := range options {
		opt(opts)
	}

	bundle, err := Read(ctx, r, opts.readOptions...)
	if err != nil {
		return nil, fmt.Errorf("failed to read HardwareDevice bundle: %w", err)
	}
	hdm := bundle.Manifest

	// Process each Asset.
	processedAssets := make(map[string]*hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset, len(hdm.GetAssets()))
	for key, asset := range hdm.GetAssets() {
		processedAsset, err := opts.processAsset(ctx, asset)
		if err != nil {
			return nil, err
		}
		switch processedAsset.GetVariant().(type) {
		case *hdmpb.ProcessedHardwareDeviceManifest_ProcessedAsset_Catalog:
			idVersion := processedAsset.GetCatalog().GetIdVersion()
			if idVersion.GetVersion() == "" {
				return nil, fmt.Errorf("catalog Asset %s does not specify a version", idutils.IDFromProtoUnchecked(idVersion.GetId()))
			}
		}
		processedAssets[key] = processedAsset
	}

	return &hdmpb.ProcessedHardwareDeviceManifest{
		Metadata: hdm.GetMetadata(),
		Assets:   processedAssets,
		Graph:    hdm.GetGraph(),
	}, nil
}

// ProcessFile is a helper to create a processed HardwareDevice from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*hdmpb.ProcessedHardwareDeviceManifest, error) {
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

// tarBundlePathFrom returns the in-tar path for a local asset bundle with the given key.
func tarBundlePathFrom(key string) string {
	return fmt.Sprintf("assets/%s.bundle.tar", key)
}

// tryExtractAssetKey returns the key of the asset bundle at the given in-tar path, or the empty
// string if the path is not a valid in-tar asset bundle path.
//
// The second return value is true if the path is a valid in-tar asset bundle path.
func tryExtractAssetKey(path string) (string, bool) {
	submatches := tarBundlePathRegex.FindStringSubmatch(path)
	if submatches == nil {
		return "", false
	}
	return submatches[1], true
}

// writeToFile reads a file from r and writes it to the specified path.
func writeToFile(r io.Reader, path string) error {
	w, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open %q for writing: %w", path, err)
	}
	defer w.Close()
	if _, err := io.Copy(w, r); err != nil {
		return fmt.Errorf("failed to copy file to %q: %w", path, err)
	}
	return nil
}
