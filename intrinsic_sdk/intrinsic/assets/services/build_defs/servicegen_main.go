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

// package main is the entrypoint for creating Service Asset bundles.
package main

import (
	"context"
	"flag"

	"intrinsic/assets/services/build_defs/servicegen"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"

	log "github.com/golang/glog"
)

var (
	manifestPath           = flag.String("manifest", "", "Path to the ServiceManifest textproto file.")
	defaultConfigPath      = flag.String("default_config", "", "Optional path to default config proto.")
	fileDescriptorSetPaths = intrinsicflag.MultiString("file_descriptor_set", nil, "Path to binary file descriptor set proto associated with the manifest. Can be repeated.")
	imageTarPaths          = intrinsicflag.MultiString("image_tar", nil, "Full path to .tar archive for an image. Can be repeated.")
	outputBundlePath       = flag.String("output_bundle", "", "Output path for the .tar bundle.")
)

func main() {
	intrinsic.Init()

	ctx := context.Background()
	if err := servicegen.CreateServiceBundle(ctx, &servicegen.CreateServiceBundleOptions{
		ManifestPath:           *manifestPath,
		DefaultConfigPath:      *defaultConfigPath,
		FileDescriptorSetPaths: *fileDescriptorSetPaths,
		ImageTarPaths:          *imageTarPaths,
		OutputBundlePath:       *outputBundlePath,
	}); err != nil {
		log.Exitf("failed to create Service Asset bundle: %v", err)
	}
}
