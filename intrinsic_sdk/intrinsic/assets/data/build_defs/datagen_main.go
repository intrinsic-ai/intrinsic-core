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

// package main is the entrypoint for creating Data Asset bundles.
package main

import (
	"context"
	"flag"
	"strings"

	"intrinsic/assets/data/build_defs/datagen"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"
	"intrinsic/util/proto/registryutil"

	log "github.com/golang/glog"
)

var (
	manifestPath                 = flag.String("manifest_path", "", "Path to the DataManifest textproto file.")
	referenceToPath              = intrinsicflag.MultiString("reference_to_path", nil, "Map a file reference in a manifest to a file path relative to the current working directory 'file_reference=path'. Can be repeated.")
	replaceWithExternalReference = intrinsicflag.MultiString("replace_with_external_reference", nil, "Replace all references to a file on disk with an external reference in the payload as 'path=external_ref'. Can be repeated.")
	fileDescriptorSetPaths       = intrinsicflag.MultiString("file_descriptor_set_path", nil, "Path to a binary file descriptor set proto to be used to resolve the data payload. Can be repeated.")
	outputBundlePath             = flag.String("output_bundle_path", "", "Output path for the .tar bundle.")
)

func main() {
	intrinsic.Init()

	externalReferencedFilePaths := make(map[string]string, len(*replaceWithExternalReference))
	for _, entry := range *replaceWithExternalReference {
		parts := strings.SplitN(entry, "=", 2)
		if len(parts) != 2 {
			log.Exitf("invalid --replace_with_external_reference flag %q: expected path=external_ref", entry)
		}
		externalReferencedFilePaths[parts[0]] = parts[1]
	}

	// Resolve file references to their real locations on disk
	referenceToPathMap := make(map[string]string, len(*referenceToPath))
	for _, entry := range *referenceToPath {
		parts := strings.SplitN(entry, "=", 2)
		if len(parts) != 2 {
			log.Exitf("invalid --reference_to_path flag %q: expected file_reference=path", entry)
		}
		referenceToPathMap[parts[0]] = parts[1]
	}

	fds, err := registryutil.LoadFileDescriptorSets(*fileDescriptorSetPaths)
	if err != nil {
		log.Exitf("failed to load FileDescriptorSets: %v", err)
	}

	manifest, err := datagen.ReadDataAssetManifest(*manifestPath, fds)
	if err != nil {
		log.Exitf("failed to read DataManifest: %v", err)
	}

	ctx := context.Background()
	if err := datagen.CreateDataBundle(ctx, &datagen.CreateDataBundleOptions{
		Manifest:                    manifest,
		ReferenceToPath:             referenceToPathMap,
		ExternalReferencedFilePaths: externalReferencedFilePaths,
		FileDescriptorSet:           fds,
		OutputBundlePath:            *outputBundlePath,
	}); err != nil {
		log.Exitf("failed to create Data Asset bundle: %v", err)
	}
}
