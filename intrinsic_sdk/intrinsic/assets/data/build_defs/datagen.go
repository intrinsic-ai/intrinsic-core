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

// Package datagen implements creation of the Data Asset bundle.
package datagen

import (
	"context"
	"fmt"
	"path/filepath"

	"intrinsic/assets/data/databundle"
	"intrinsic/assets/data/datavalidate"
	"intrinsic/assets/data/utils"
	"intrinsic/assets/referenceddata"
	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dmpb "intrinsic/assets/data/proto/v1/data_manifest_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	mpb "intrinsic/assets/proto/metadata_go_proto"

	"google.golang.org/protobuf/reflect/protodesc"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

// CreateDataBundleOptions provides the data needed to create a Data Asset bundle.
type CreateDataBundleOptions struct {
	// Manifest is the DataManifest message for the bundle.
	Manifest *dmpb.DataManifest
	// ReferenceToPath is a map of file references as specified in a manifest to file paths
	// relative to the current working directory. Data Asset manifests specify file references
	// as paths relative to the manifest. However, the intrinsic_data Bazel rule invokes the
	// datagen tool from a Bazel action. Bazel will run the tool in that action's execroot,
	// and it has its own conventions about where in the execroot it places files. This mapping
	// allows people to specify paths in the manifest that make sense in the source tree, while
	// letting intrinsic_data tell datagen where Bazel actually put the files when the Bazel
	// action runs this tool.
	ReferenceToPath map[string]string
	// ExternalReferencedFilePaths is a map specifying files on disk to exclude from
	// the .tar bundle, and external references to replace those files with.
	//
	// Keys are paths to files relative to the current working directory.
	// Values are external references to put in the Data Asset instead of those files.
	//
	// Files excluded here are left out of the .tar bundle and are kept as external references in the
	// payload along with digests to ensure the data are not modified after bundle creation.
	ExternalReferencedFilePaths map[string]string
	// FileDescriptorSet is the binary file descriptor set proto to be used to resolve
	// the data payload message.
	FileDescriptorSet *descriptorpb.FileDescriptorSet
	// OutputBundlePath is the output path for the tar bundle.
	OutputBundlePath string
}

// CreateDataBundle creates a Data Asset bundle on disk.
func CreateDataBundle(ctx context.Context, opts *CreateDataBundleOptions) error {
	if opts.Manifest == nil {
		return fmt.Errorf("opts.Manifest is required")
	}
	if opts.FileDescriptorSet == nil {
		return fmt.Errorf("opts.FileDescriptorSet is required")
	}

	files, err := protodesc.NewFiles(opts.FileDescriptorSet)
	if err != nil {
		return fmt.Errorf("failed to populate registry: %w", err)
	}
	if err := datavalidate.DataManifest(ctx, opts.Manifest,
		datavalidate.WithFiles(files),
		datavalidate.WithAllowDataManifestRuntimeAssetID(),
	); err != nil {
		return fmt.Errorf("invalid DataManifest: %w", err)
	}

	da := &dapb.DataAsset{
		Data:              opts.Manifest.GetData(),
		FileDescriptorSet: opts.FileDescriptorSet,
		Metadata: &mpb.Metadata{
			AssetType:   atypepb.AssetType_ASSET_TYPE_DATA,
			DisplayName: opts.Manifest.GetMetadata().GetDisplayName(),
			IdVersion: &idpb.IdVersion{
				Id: opts.Manifest.GetMetadata().GetId(),
			},
			Documentation: opts.Manifest.GetMetadata().GetDocumentation(),
			Vendor:        opts.Manifest.GetMetadata().GetVendor(),
		},
	}

	if payload, err := utils.ExtractPayload(da); err != nil {
		return fmt.Errorf("failed to extract data payload: %w", err)
	} else if payloadOut, err := referenceddata.WalkUnique(payload, func(ref *referenceddata.ReferencedData) error {
		if ref.Type() == referenceddata.FileReferenceType && !filepath.IsAbs(ref.Reference()) {
			// Translate file references to their actual locations on disk
			execPath, ok := opts.ReferenceToPath[ref.Reference()]
			if !ok {
				return fmt.Errorf("referenced file %q not found in ReferenceToPath; check that the file is included in the data attribute of intrinsic_data", ref.Reference())
			}
			ref.SetReference(execPath)
		}
		return nil
	}); err != nil {
		return fmt.Errorf("failed to walk data payload when creating Data Asset bundle at %q: %w", opts.OutputBundlePath, err)
	} else if payloadOutAny, err := anypb.New(payloadOut); err != nil {
		return fmt.Errorf("failed to create Any proto for data payload: %w", err)
	} else {
		da.Data = payloadOutAny
	}

	if err := databundle.WriteFile(ctx, da, opts.OutputBundlePath,
		databundle.WithExternalReferencedFilePaths(opts.ExternalReferencedFilePaths),
	); err != nil {
		return fmt.Errorf("failed to write Data Asset bundle: %w", err)
	}

	return nil
}

// ReadDataAssetManifest reads a DataManifest textproto from disk and resolves its dynamic types
// using the provided FileDescriptorSet.
func ReadDataAssetManifest(manifestPath string, fds *descriptorpb.FileDescriptorSet) (*dmpb.DataManifest, error) {
	types, err := registryutil.NewTypesFromFileDescriptorSet(fds)
	if err != nil {
		return nil, fmt.Errorf("failed to populate registry types: %w", err)
	}
	m := &dmpb.DataManifest{}
	if err := protoio.ReadTextProto(manifestPath, m, protoio.WithResolver(types)); err != nil {
		return nil, fmt.Errorf("failed to read manifest %q: %w", manifestPath, err)
	}
	return m, nil
}
