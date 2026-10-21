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

// Package sceneobjectfix contains utils that adapt SceneObjects to meet the requirements of the
// latest platform version.
package sceneobjectfix

import (
	"fmt" 

	"intrinsic/util/proto/descriptorcompatibility" 

	sompb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

// Manifest updates a SceneObjectManifest to meet the requirements of the latest platform version.
func Manifest(manifest *sompb.SceneObjectManifest) error {
	return nil
}

// ProcessedManifest updates a ProcessedSceneObjectManifest to meet the requirements of the latest
// platform version.
func ProcessedManifest(manifest *sompb.ProcessedSceneObjectManifest) error {
	// Backfill missing FileDescriptorSet when user data is not specified.
	if len(manifest.GetAssets().GetSceneObjectModel().GetUserData()) == 0 && manifest.GetAssets().GetFileDescriptorSet() == nil {
		if manifest.GetAssets() == nil {
			manifest.Assets = &sompb.ProcessedSceneObjectAssets{}
		}
		manifest.Assets.FileDescriptorSet = &descriptorpb.FileDescriptorSet{}
	}

	if err := descriptorcompatibility.Reconcile(manifest.GetAssets().GetFileDescriptorSet()); err != nil {
		return fmt.Errorf("unable to reconcile the file descriptor set for SceneObject Asset: %w", err)
	}

	return nil
}
