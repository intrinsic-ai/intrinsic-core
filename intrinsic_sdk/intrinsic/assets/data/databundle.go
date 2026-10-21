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

// Package databundle provides utils for working with Data Asset bundles.
package databundle

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"intrinsic/assets/data/datavalidate"
	"intrinsic/assets/data/utils"
	"intrinsic/assets/ioutils"
	"intrinsic/assets/referenceddata"
	"intrinsic/util/archive/tartooling"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/google/safearchive/tar"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	// LINT.IfChange(bundle_file)
	dataAssetFileName = "data_asset.binpb"
	// LINT.ThenChange(//intrinsic_sdk/intrinsic/assets/bundle.go:data_bundle_file)
	dataFileBaseDir = "data_files"
)

type writeOptions struct {
	externalReferencedFilePaths map[string]string
}

// WriteOption is a functional option for Write.
type WriteOption func(*writeOptions)

// WithExternalReferencedFilePaths provides a a map specifying the referenced files to exclude from
// the .tar bundle and the paths to which to remap those references in the payload.
//
// Keys are paths to referenced files to exclude.
// Values are remapped paths for references in the output payload.
//
// Excluded files are left out of the .tar bundle and are kept as external references in the payload
// along with digests to ensure the data are not modified after bundle creation.
func WithExternalReferencedFilePaths(paths map[string]string) WriteOption {
	return func(opts *writeOptions) {
		opts.externalReferencedFilePaths = paths
	}
}

// Write writes a Data Asset .tar bundle to the given writer.
func Write(ctx context.Context, da *dapb.DataAsset, w io.Writer, options ...WriteOption) error {
	opts := &writeOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if da == nil {
		return fmt.Errorf("DataAsset must not be nil")
	}
	if da.GetMetadata().GetAssetType() == atpb.AssetType_ASSET_TYPE_UNSPECIFIED {
		da.Metadata.AssetType = atpb.AssetType_ASSET_TYPE_DATA
	}
	if err := datavalidate.DataAsset(ctx, da, datavalidate.WithAllowDataAssetRuntimeAssetID()); err != nil {
		return fmt.Errorf("invalid DataAsset: %w", err)
	}

	tw := tar.NewWriter(w)

	payload, err := utils.ExtractPayload(da)
	if err != nil {
		return fmt.Errorf("failed to extract data payload: %w", err)
	}

	// Records all file path references that were found.
	foundReferencedFilePaths := map[string]struct{}{}

	// Walk through the data payload. For each ReferencedData:
	// - Validate it;
	// - If it is a file reference that is not excluded, copy it to the tar bundle;
	// - If it is a file reference that is excluded, remap it and ensure it has a digest.
	tarPaths := map[string]struct{}{} // Keeps track of used tar paths.
	payloadOut, err := referenceddata.WalkUnique(payload, func(ref *referenceddata.ReferencedData) error {
		if err := datavalidate.ReferencedData(ctx, ref); err != nil {
			return fmt.Errorf("invalid ReferencedData: %w", err)
		}

		// Process file references. We either add the file to the tar bundle or remap it and ensure the
		// reference has a digest to guard against later modification to the file.
		if ref.Type() == referenceddata.FileReferenceType {
			foundReferencedFilePaths[ref.Reference()] = struct{}{}

			if remappedPath, ok := opts.externalReferencedFilePaths[ref.Reference()]; !ok { // Add to the tar bundle.
				inBundlePath := toUniqueTarPath(ref.Reference(), dataFileBaseDir, tarPaths)
				if err := tartooling.AddFile(ref.Reference(), tw, inBundlePath); err != nil {
					return fmt.Errorf("failed to add data file to bundle: %w", err)
				}
				ref.SetReference(inBundlePath)
			} else { // Keep the file external.

				// Ensure its reference has a digest.
				if ref.Digest() == "" {
					file, err := os.Open(ref.Reference())
					if err != nil {
						return fmt.Errorf("failed to open referenced file %q: %w", ref.Reference(), err)
					}
					defer file.Close()

					digest, err := utils.Digest(file)
					if err != nil {
						return fmt.Errorf("failed to compute digest of referenced file %q: %w", ref.Reference(), err)
					}
					ref.SetDigest(digest)
				}


				ref.SetReference(remappedPath)
			}
		}

		return nil
	})
	if err != nil {
		return fmt.Errorf("failed to walk referenced data: %w", err)
	}

	// Verify that all external referenced file paths are actually referenced by the data payload.
	for path := range opts.externalReferencedFilePaths {
		if _, ok := foundReferencedFilePaths[path]; !ok {
			return fmt.Errorf("external referenced file path %q is not referenced in the data payload. referenced: %v", path, foundReferencedFilePaths)
		}
	}

	payloadOutAny, err := anypb.New(payloadOut)
	if err != nil {
		return fmt.Errorf("failed to create Any proto for data payload: %w", err)
	}

	// Construct and add the bundle version of the Data Asset.
	daOut := &dapb.DataAsset{
		Data:              payloadOutAny,
		FileDescriptorSet: da.GetFileDescriptorSet(),
		Metadata:          da.GetMetadata(),
	}
	if err := tartooling.AddBinaryProto(daOut, tw, dataAssetFileName); err != nil {
		return fmt.Errorf("failed to write DataAsset to bundle: %w", err)
	}

	if err := tw.Close(); err != nil {
		return fmt.Errorf("failed to close tar writer: %w", err)
	}

	return nil
}

// WriteFile writes a Data Asset .tar bundle to the specified path.
func WriteFile(ctx context.Context, da *dapb.DataAsset, path string, options ...WriteOption) error {
	if path == "" {
		return fmt.Errorf("path must not be empty")
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open %q for writing: %w", path, err)
	}
	defer f.Close()

	return Write(ctx, da, f, options...)
}

// DataBundle represents a Data Asset bundle.
type DataBundle struct {
	Data *dapb.DataAsset
}

// ExternalFilePathProposal is a function that proposes an absolute path for a relative file reference.
type ExternalFilePathProposal func(reference string) (string, error)

type readOptions struct {
	referencedDataProcessor referenceddata.Processor
	pathProposals           []ExternalFilePathProposal
}

// ReadOption is a functional option for Read.
type ReadOption func(*readOptions)

// WithReferencedDataProcessor specifies a referenceddata.Processor to call for each unique
// ReferencedData value in the Data Asset as it is read.
//
// (Note that all inlined ReferencedData are considered unique.)
//
// If a non-nil ReferencedData is returned by the processor, the return value replaces all of the
// matching ReferencedData values in the Data Asset.
func WithReferencedDataProcessor(f referenceddata.Processor) ReadOption {
	return func(opts *readOptions) {
		opts.referencedDataProcessor = f
	}
}

// WithPathProposal specifies an ExternalFilePathProposal to use when resolving relative file references.
func WithPathProposal(p ExternalFilePathProposal) ReadOption {
	return func(opts *readOptions) {
		opts.pathProposals = append(opts.pathProposals, p)
	}
}

// Read reads a Data Asset bundle from a reader.
//
// Relative file references in the Data Asset payload cannot be resolved if they are relative to the bundle's directory,
// unless the bundle path is passed via context (which ReadFile does).
func Read(ctx context.Context, reader io.Reader, options ...ReadOption) (*DataBundle, error) {
	opts := &readOptions{}
	for _, opt := range options {
		opt(opts)
	}

	tr := tar.NewReader(reader)

	// Read all files from the bundle, processing ReferencedData for in-tar data files as we go.
	var da *dapb.DataAsset
	processedReferences := map[string]*referenceddata.ReferencedData{}
	var unknownFilePaths []string
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("failed to process tar file: %w", err)
		}
		if hdr.Typeflag != tar.TypeReg {
			continue
		}

		tarPath := hdr.Name
		switch tarPath {
		case dataAssetFileName:
			da = &dapb.DataAsset{}
			if err := ioutils.ReadBinaryProto(tr, da); err != nil {
				return nil, fmt.Errorf("failed to read DataAsset: %w", err)
			}
			da.GetMetadata().AssetType = atpb.AssetType_ASSET_TYPE_DATA
		default:
			if strings.HasPrefix(tarPath, dataFileBaseDir) { // Found a referenced in-tar data file.
				// Process the reference for the in-tar file now, since we won't be able to pass the reader
				// to the processor below when we walk the payload.
				// Note that we don't validate the referenced data in this case.
				ref := referenceddata.FromProto(&rdpb.ReferencedData{
					Data: &rdpb.ReferencedData_Reference{
						Reference: tarPath,
					},
				})
				if opts.referencedDataProcessor != nil {
					if err := referenceddata.Process(ctx, ref, opts.referencedDataProcessor,
						referenceddata.WithReader(tr, hdr.Size),
					); err != nil {
						return nil, fmt.Errorf("failed to process in-tar reference %q: %w", tarPath, err)
					}
				}
				processedReferences[tarPath] = ref
			} else { // Unknown file. This will be an error below.
				unknownFilePaths = append(unknownFilePaths, tarPath)
			}
		}
	}

	if len(unknownFilePaths) > 0 {
		return nil, fmt.Errorf("unknown files: %v", unknownFilePaths)
	}
	if da == nil {
		return nil, fmt.Errorf("DataAsset not found in tar file")
	}

	// Process the payload's ReferencedData values.
	payload, err := utils.ExtractPayload(da)
	if err != nil {
		return nil, fmt.Errorf("failed to extract data payload: %w", err)
	}

	payloadOut, err := referenceddata.WalkUnique(payload, func(ref *referenceddata.ReferencedData) error {
		// Check whether we already processed the reference during our pass through the .tar bundle.
		if pRef, ok := processedReferences[ref.Reference()]; ok {
			ref.Merge(pRef)
			return nil
		}

		// Find the reference (see function comment about relative paths).
		if err := findReference(ref, opts.pathProposals); err != nil {
			return fmt.Errorf("failed to find reference: %w", err)
		}

		// Validate the ReferencedData (e.g., verify its digest).
		if err := datavalidate.ReferencedData(ctx, ref); err != nil {
			return fmt.Errorf("invalid ReferencedData: %w", err)
		}

		// Optionally process the ReferencedData.
		if opts.referencedDataProcessor != nil {
			if err := referenceddata.Process(ctx, ref, opts.referencedDataProcessor); err != nil {
				return fmt.Errorf("failed to process reference: %w", err)
			}
		}

		return nil
	})
	if err != nil {
		return nil, fmt.Errorf("failed to walk payload: %w", err)
	}

	if payloadOutAny, err := anypb.New(payloadOut); err != nil {
		return nil, fmt.Errorf("failed to create Any proto for data payload: %w", err)
	} else {
		da.Data = payloadOutAny
	}

	return &DataBundle{
		Data: da,
	}, nil
}

// ReadFile is a helper to read a Data Asset bundle from a file path.
// It opens the file and calls Read.
//
// By default this will look for relative file references in the following places:
//   - relative to the bundle's directory (for manually constructed bundles that refer in a
//     straightforward way to data at a location relative to the bundle);
//   - relative to the bazel .runfiles directory generated along with the bundle (for bundles that
//     are generated by the intrinsic_data build rule and then later passed to a tool such as
//     `inctl asset install`);
//   - an rlocation path for a runfile of the binary that is calling this function (for bazel
//     targets that take in the bundle generated by an intrinsic_data target as a dependency [e.g.,
//     unit tests]).
//
// Additional pathways can be inspected by using WithPathProposal.
func ReadFile(ctx context.Context, path string, options ...ReadOption) (*DataBundle, error) {
	if path == "" {
		return nil, fmt.Errorf("path must not be empty")
	}
	f, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("failed to open %q for reading: %w", path, err)
	}
	defer f.Close()

	bundleDir, bundleName := filepath.Split(path)
	return Read(ctx, f, append(options,
		WithPathProposal(func(ref string) (string, error) {
			return filepath.Join(bundleDir, ref), nil
		}),
		WithPathProposal(func(ref string) (string, error) {
			return filepath.Join(bundleDir, fmt.Sprintf("%s.runfiles", bundleName), ref), nil
		}),
		WithPathProposal(func(ref string) (string, error) {
			return runfiles.Rlocation(ref)
		}),
	)...)
}

type processOptions struct {
	readOptions []ReadOption
}

// ProcessOption is a functional option for Process.
type ProcessOption func(*processOptions)

// WithReadOptions specifies the ReadOptions to use when reading the Data Asset.
func WithReadOptions(options ...ReadOption) ProcessOption {
	return func(opts *processOptions) {
		opts.readOptions = options
	}
}

// Process creates a processed Data Asset from a bundle reader.
func Process(ctx context.Context, r io.Reader, options ...ProcessOption) (*dapb.DataAsset, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}

	bundle, err := Read(ctx, r, opts.readOptions...)
	if err != nil {
		return nil, fmt.Errorf("failed to read Data bundle: %w", err)
	}

	return bundle.Data, nil
}

// ProcessFile is a helper to create a processed Data Asset from a bundle file path.
// It opens the file and calls Process.
func ProcessFile(ctx context.Context, path string, options ...ProcessOption) (*dapb.DataAsset, error) {
	opts := &processOptions{}
	for _, opt := range options {
		opt(opts)
	}

	bundle, err := ReadFile(ctx, path, opts.readOptions...)
	if err != nil {
		return nil, fmt.Errorf("failed to read Data bundle: %w", err)
	}

	return bundle.Data, nil
}

// toUniqueTarPath generates a unique path in the tar bundle to which to save a data file.
//
// `tarPaths` keeps track of tar paths that have already been used.
func toUniqueTarPath(path string, tarDir string, tarPaths map[string]struct{}) string {
	fileName := filepath.Base(path)
	fileExt := filepath.Ext(fileName)
	filePre := strings.TrimSuffix(fileName, fileExt)
	suffix := ""
	for i := 1; ; i++ {
		tarPath := filepath.Join(tarDir, fmt.Sprintf("%s%s%s", filePre, suffix, fileExt))
		if _, ok := tarPaths[tarPath]; !ok {
			tarPaths[tarPath] = struct{}{}
			return tarPath
		}
		suffix = fmt.Sprintf("%03d", i)
	}
}

func findReference(ref *referenceddata.ReferencedData, proposals []ExternalFilePathProposal) error {
	if ref.Type() == referenceddata.FileReferenceType && !filepath.IsAbs(ref.Reference()) {
		var candidatePaths []string
		for _, proposal := range proposals {
			candidatePath, err := proposal(ref.Reference())
			if err != nil {
				continue
			}
			if _, err := os.Stat(candidatePath); err == nil {
				ref.SetReference(candidatePath)
				return nil
			}
			candidatePaths = append(candidatePaths, candidatePath)
		}
		return fmt.Errorf("no valid base directory found for referenced file %q (tried: %v)", ref.Reference(), candidatePaths)
	}

	return nil
}
