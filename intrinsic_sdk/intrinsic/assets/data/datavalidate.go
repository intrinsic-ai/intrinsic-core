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

// Package datavalidate provides utils for validating Data Assets.
package datavalidate

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"os"
	"strings"

	"intrinsic/assets/data/utils"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/metadatautils"
	"intrinsic/assets/referenceddata"

	log "github.com/golang/glog"    
	"google.golang.org/grpc/codes"  
	"google.golang.org/grpc/status" 
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoregistry"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dmpb "intrinsic/assets/data/proto/v1/data_manifest_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto" 
)

type dataManifestOptions struct {
	files               *protoregistry.Files
	allowRuntimeAssetID bool
}

// DataManifestOption is an option for validating a DataManifest.
type DataManifestOption func(*dataManifestOptions)

// WithAllowDataManifestRuntimeAssetID allows the metadata to have the reserved runtime Asset ID.
func WithAllowDataManifestRuntimeAssetID() DataManifestOption {
	return func(opts *dataManifestOptions) {
		opts.allowRuntimeAssetID = true
	}
}

// WithFiles provides a Files for validating proto messages.
func WithFiles(files *protoregistry.Files) DataManifestOption {
	return func(opts *dataManifestOptions) {
		opts.files = files
	}
}

// DataManifest validates a DataManifest.
func DataManifest(ctx context.Context, m *dmpb.DataManifest, options ...DataManifestOption) error {
	opts := &dataManifestOptions{}
	for _, opt := range options {
		opt(opts)
	}
	if opts.files == nil {
		return fmt.Errorf("files option must be specified")
	}

	if m == nil {
		return fmt.Errorf("DataManifest must not be nil")
	}

	mdOpts := []metadatautils.ValidateMetadataOption{}
	if opts.allowRuntimeAssetID {
		mdOpts = append(mdOpts, metadatautils.WithAllowRuntimeAssetID())
	}
	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata(), mdOpts...); err != nil {
		return fmt.Errorf("invalid DataManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	if m.GetData() == nil {
		return fmt.Errorf("data payload must be specified for %q", id)
	}
	if name := m.GetData().MessageName(); name == "" {
		return fmt.Errorf("data payload must not be an empty Any for %q", id)
	} else if _, err := opts.files.FindDescriptorByName(name); err != nil {
		return fmt.Errorf("cannot find data payload message %q for %q: %w", name, id, err)
	}

	return nil
}

type dataAssetOptions struct {
	allowRuntimeAssetID   bool
	referencedDataOptions []ReferencedDataOption
	report                *report.Report
}

// DataAssetOption is an option for validating a DataAsset.
type DataAssetOption func(*dataAssetOptions)

// WithReferencedDataOptions appends options for validating ReferencedData within the Data Asset.
func WithReferencedDataOptions(referencedDataOptions ...ReferencedDataOption) DataAssetOption {
	return func(opts *dataAssetOptions) {
		opts.referencedDataOptions = append(opts.referencedDataOptions, referencedDataOptions...)
	}
}

// WithAllowDataAssetRuntimeAssetID allows the metadata to have the reserved runtime Asset ID.
func WithAllowDataAssetRuntimeAssetID() DataAssetOption {
	return func(opts *dataAssetOptions) {
		opts.allowRuntimeAssetID = true
	}
}

// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) DataAssetOption {
	return func(opts *dataAssetOptions) {
		opts.report = report
	}
}

// DataAsset validates a DataAsset.
func DataAsset(ctx context.Context, da *dapb.DataAsset, options ...DataAssetOption) error {
	opts := &dataAssetOptions{}
	WithReport(report.New())(opts)
	for _, opt := range options {
		opt(opts)
	}

	if da == nil {
		return fmt.Errorf("DataAsset must not be nil")
	}

	m := da.GetMetadata()
	mdOpts := []metadatautils.ValidateMetadataOption{
		metadatautils.WithAssetType(atpb.AssetType_ASSET_TYPE_DATA),
		metadatautils.WithInAssetOptions(),
	}
	if opts.allowRuntimeAssetID {
		mdOpts = append(mdOpts, metadatautils.WithAllowRuntimeAssetID())
	}
	if err := metadatautils.ValidateMetadata(m, mdOpts...); err != nil {
		return fmt.Errorf("invalid DataAsset metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(da.GetMetadata().GetIdVersion().GetId())

	if da.GetData() == nil {
		return fmt.Errorf("data payload must be specified for %q", id)
	}

	fds := da.GetFileDescriptorSet()
	if fds == nil {
		return fmt.Errorf("FileDescriptorSet must not be nil for %q", id)
	}
	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return fmt.Errorf("failed to populate registry for %q: %v", id, err)
	}
	if name := da.GetData().MessageName(); name == "" {
		return fmt.Errorf("data payload must not be an empty Any for %q", id)
	} else if _, err := files.FindDescriptorByName(name); err != nil {
		return fmt.Errorf("cannot find data payload message %q for %q: %w", name, id, err)
	}

	// Validate the payload's referenced data.
	if payload, err := utils.ExtractPayload(da); err != nil {
		return err
	} else if _, err := referenceddata.WalkUnique(payload, func(ref *referenceddata.ReferencedData) error {
		if err := ReferencedData(ctx, ref, opts.referencedDataOptions...); err != nil {
			return fmt.Errorf("invalid ReferencedData for %q: %w", id, err)
		}
		return nil
	}); err != nil {
		return err
	}

	return nil
}

type referencedDataOptions struct {
	casClient                  caspb.ContentAddressableStorageServiceClient 
	disallowFileReferences     bool
	ignoreMissingCASReferences bool 
}

// ReferencedDataOption is an option for validating a ReferencedData.
type ReferencedDataOption func(*referencedDataOptions)



// WithCASClient provides a CAS client for validating CAS ReferencedData.
//
// If not provided, then CAS references are not validated.
// The context passed to the validator (e.g. DataAsset) must be an outgoing
// context if CAS validation is enabled.
func WithCASClient(casClient caspb.ContentAddressableStorageServiceClient) ReferencedDataOption {
	return func(opts *referencedDataOptions) {
		opts.casClient = casClient
	}
}

// WithIgnoreMissingCASReferences specifies whether to ignore missing CAS references.
func WithIgnoreMissingCASReferences(b bool) ReferencedDataOption {
	return func(opts *referencedDataOptions) {
		opts.ignoreMissingCASReferences = b
	}
}



// WithDisallowFileReferences specifies whether file references are disallowed in the
// ReferencedData.
func WithDisallowFileReferences(disallowFileReferences bool) ReferencedDataOption {
	return func(opts *referencedDataOptions) {
		opts.disallowFileReferences = disallowFileReferences
	}
}

// ReferencedData validates a ReferencedData.
//
// Validation includes:
// - If specified, compare the digest against the referenced data.
// - If the type is a file reference and file references are disallowed, return an error.

//   - If the type is a CAS reference and a CAS client is provided, validate that the referenced
//     object exists.
//

func ReferencedData(ctx context.Context, ref *referenceddata.ReferencedData, options ...ReferencedDataOption) error {
	opts := &referencedDataOptions{}
	for _, opt := range options {
		opt(opts)
	}

	// Type-specific validation.
	switch ref.Type() {
	case referenceddata.FileReferenceType:
		if opts.disallowFileReferences {
			return fmt.Errorf("file references are not allowed (got: %q)", ref.Reference())
		}
	case referenceddata.CASReferenceType:

		if opts.casClient != nil {
			if _, err := opts.casClient.Stat(ctx, &caspb.StatRequest{ObjectId: ref.Reference()}); err != nil {
				if c := status.Code(err); c == codes.NotFound && opts.ignoreMissingCASReferences {
					log.WarningContextf(ctx, "ignoring missing CAS reference %q", ref.Reference())
					return nil
				}
				return fmt.Errorf("failed to validate CAS reference %q: %w", ref.Reference(), err)
			}
		}

	case referenceddata.InlinedReferenceType:
		// Nothing to do.
	default:
		return fmt.Errorf("unknown reference type: %d", ref.Type())
	}

	// Validate the digest against the referenced data.
	if ref.Digest() != "" {
		// Get a reader for the data.
		var reader io.Reader
		switch ref.Type() {
		case referenceddata.FileReferenceType:
			file, err := os.Open(ref.Reference())
			if err != nil {
				return fmt.Errorf("failed to open referenced file %q: %w", ref.Reference(), err)
			}
			defer file.Close()
			reader = file
		case referenceddata.CASReferenceType:
			if ref.Digest() != "" {
				casID := strings.TrimPrefix(ref.Reference(), "intcas://")
				parsed, err := utils.ParseDigest(ref.Digest())
				if err != nil {
					return fmt.Errorf("failed to parse digest %q: %w", ref.Digest(), err)
				}
				if casID != parsed.Hash {
					return fmt.Errorf("CAS reference hash %q does not match digest hash %q", casID, parsed.Hash)
				}
			}
			return nil
		case referenceddata.InlinedReferenceType:
			reader = bytes.NewReader(ref.Inlined())
		default:
			return fmt.Errorf("unknown reference type: %d", ref.Type())
		}

		// Test the digest.
		if parsed, err := utils.ParseDigest(ref.Digest()); err != nil {
			return fmt.Errorf("failed to parse digest %q: %w", ref.Digest(), err)
		} else if gotDigest, err := utils.Digest(reader, utils.WithAlgorithm(parsed.Algorithm)); err != nil {
			return fmt.Errorf("failed to compute digest: %w", err)
		} else if gotDigest != parsed.Digest {
			return fmt.Errorf("digest mismatch: got %q, want %q", gotDigest, parsed.Digest)
		}
	}

	return nil
}
