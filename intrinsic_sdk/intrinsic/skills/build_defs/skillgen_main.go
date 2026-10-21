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

// Package main is the entrypoint for creating Skill Asset bundles.
package main

import (
	"context"
	"flag"

	"intrinsic/production/intrinsic"
	"intrinsic/skills/build_defs/skillgen"

	log "github.com/golang/glog"
)

var (
	manifestPath          = flag.String("manifest", "", "Path to the SkillManifest textproto file.")
	fileDescriptorSetPath = flag.String("file_descriptor_set", "", "Path to the binary file descriptor set.")
	imageTarPath          = flag.String("image_tar", "", "Path to the Skill image file.")
	outputBundlePath      = flag.String("output_bundle", "", "Output path for the Skill Asset bundle.")
)

func main() {
	intrinsic.Init()

	ctx := context.Background()
	if err := skillgen.CreateSkillBundle(ctx, &skillgen.CreateSkillBundleOptions{
		ManifestPath:          *manifestPath,
		FileDescriptorSetPath: *fileDescriptorSetPath,
		ImageTarPath:          *imageTarPath,
		OutputBundlePath:      *outputBundlePath,
	}); err != nil {
		log.Exitf("failed to create Skill Asset bundle: %v", err)
	}
}
