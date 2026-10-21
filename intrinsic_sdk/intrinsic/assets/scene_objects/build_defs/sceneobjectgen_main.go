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

// package main is the entrypoint for creating SceneObject Asset bundles.
package main

import (
	"context"
	"flag"

	"intrinsic/assets/scene_objects/build_defs/sceneobjectgen"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"

	log "github.com/golang/glog"
)

var (
	manifestPath           = flag.String("manifest", "", "Path to the SceneObjectManifest textproto file.")
	fileDescriptorSetPaths = intrinsicflag.MultiString("file_descriptor_set", nil, "Path to a binary file descriptor set proto associated with the manifest. Can be repeated.")
	rootSceneObjectName    = flag.String("root_scene_object_name", "", "Name of the root scene object.")
	gzfGeometryFilePaths   = intrinsicflag.MultiString("geometry_gzfile", nil, "Full path to .gzf file holding geometry. Can be repeated.")
	outputBundlePath       = flag.String("output_bundle", "", "Output path for the .tar bundle.")
)

func main() {
	intrinsic.Init()

	ctx := context.Background()
	if err := sceneobjectgen.CreateSceneObjectBundle(ctx, &sceneobjectgen.CreateSceneObjectBundleOptions{
		ManifestPath:           *manifestPath,
		FileDescriptorSetPaths: *fileDescriptorSetPaths,
		RootSceneObjectName:    *rootSceneObjectName,
		GzfGeometryFilePaths:   *gzfGeometryFilePaths,
		OutputBundlePath:       *outputBundlePath,
	}); err != nil {
		log.Exitf("failed to create SceneObject Asset bundle: %v", err)
	}
}
