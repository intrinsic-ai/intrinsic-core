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

// Package sceneobjectvalidate provides utils for validating SceneObjects.
package sceneobjectvalidate

import (
	"context"
	"fmt"
	"os"

	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/metadatautils"
	"intrinsic/assets/scene_objects/gzffile"
	"intrinsic/scene/validate/go/sceneobjectvalidation"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoregistry"

	sompb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"
	sopb "intrinsic/scene/proto/v1/scene_object_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto" 
)

type sceneObjectManifestOptions struct {
	files    *protoregistry.Files
	gzfPaths map[string]string
}

// SceneObjectManifestOption is an option for validating a SceneObjectManifest.
type SceneObjectManifestOption func(*sceneObjectManifestOptions)

// WithFiles provides a Files for validating proto messages.
func WithFiles(files *protoregistry.Files) SceneObjectManifestOption {
	return func(opts *sceneObjectManifestOptions) {
		opts.files = files
	}
}

// WithGZFPaths adds a map from GZF file paths as specified in the manifest to paths on disk.
//
// Must be specified if the manifest specifies GZF files.
func WithGZFPaths(gzfPaths map[string]string) SceneObjectManifestOption {
	return func(opts *sceneObjectManifestOptions) {
		opts.gzfPaths = gzfPaths
	}
}

// SceneObjectManifest validates a SceneObjectManifest.
func SceneObjectManifest(ctx context.Context, m *sompb.SceneObjectManifest, options ...SceneObjectManifestOption) error {
	opts := &sceneObjectManifestOptions{}
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("SceneObjectManifest must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata()); err != nil {
		return fmt.Errorf("invalid SceneObjectManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	if numGZF := len(m.GetAssets().GetGzfGeometryFilenames()); numGZF > 1 {
		return fmt.Errorf("support for multiple gzf files within a SceneObject is not yet implemented (got %d files)", numGZF)
	}
	if name := m.GetAssets().GetRootSceneObjectName(); len(name) != 0 {
		return fmt.Errorf("support for multiple gzf files within a scene_object is not yet implemented, so do not specify a root_scene_object_name (got: %q)", name)
	}
	if numGZF := len(m.GetAssets().GetGzfGeometryFilenames()); numGZF > 1 && len(m.GetAssets().GetRootSceneObjectName()) == 0 {
		return fmt.Errorf("root_scene_object_name must be specified for multiple gzf files")
	}

	// Verify that any user data in the associated SceneObjects is in the FileDescriptorSet.
	var sceneObjects []*sopb.SceneObject
	for _, gzfManifestPath := range m.GetAssets().GetGzfGeometryFilenames() {
		gzfPath, ok := opts.gzfPaths[gzfManifestPath]
		if !ok {
			return fmt.Errorf("gzf file %q specified in manifest, but no on disk path provided", gzfManifestPath)
		}
		sceneObject, err := extractSceneObjectFromGZF(gzfPath)
		if err != nil {
			return fmt.Errorf("failed to extract scene object from gzf file %q: %w", gzfPath, err)
		}
		sceneObjects = append(sceneObjects, sceneObject)
	}
	for _, sceneObject := range sceneObjects {
		for key, userData := range sceneObject.GetUserData() {
			if name := userData.MessageName(); name == "" {
				return fmt.Errorf("user data %q message must not be an empty Any for %q", key, id)
			} else if opts.files == nil {
				return fmt.Errorf("SceneObject %q has user data (%q, of type %s), but no descriptors provided", id, key, name)
			} else if _, err := opts.files.FindDescriptorByName(name); err != nil {
				return fmt.Errorf("cannot find user data message %q for %q: %w", name, id, err)
			}
		}
	}

	return nil
}

type processedSceneObjectManifestOptions struct {
	casClient caspb.ContentAddressableStorageServiceClient 
	report    *report.Report
}

// ProcessedSceneObjectManifestOption is an option for validating a ProcessedSceneObjectManifest.
type ProcessedSceneObjectManifestOption func(*processedSceneObjectManifestOptions)



// WithCASClient provides a CAS client for validating CAS references.
//
// If not provided, then CAS references are not validated.
// The context passed to the validator (e.g. ProcessedSceneObjectManifest)
// must be an outgoing context if CAS validation is enabled.
func WithCASClient(casClient caspb.ContentAddressableStorageServiceClient) ProcessedSceneObjectManifestOption {
	return func(opts *processedSceneObjectManifestOptions) {
		opts.casClient = casClient
	}
}



// WithReport sets the shared validation Report to use for collecting warnings.
func WithReport(report *report.Report) ProcessedSceneObjectManifestOption {
	return func(opts *processedSceneObjectManifestOptions) {
		opts.report = report
	}
}

// ProcessedSceneObjectManifest validates a ProcessedSceneObjectManifest.
func ProcessedSceneObjectManifest(ctx context.Context, m *sompb.ProcessedSceneObjectManifest, options ...ProcessedSceneObjectManifestOption) error {
	opts := &processedSceneObjectManifestOptions{}
	WithReport(report.New())(opts)
	for _, opt := range options {
		opt(opts)
	}

	if m == nil {
		return fmt.Errorf("ProcessedSceneObjectManifest must not be nil")
	}

	if err := metadatautils.ValidateManifestMetadata(m.GetMetadata()); err != nil {
		return fmt.Errorf("invalid ProcessedSceneObjectManifest metadata: %w", err)
	}
	id := idutils.IDFromProtoUnchecked(m.GetMetadata().GetId())

	fds := m.GetAssets().GetFileDescriptorSet()
	if fds == nil {
		return fmt.Errorf("FileDescriptorSet must not be nil for %q", id)
	}
	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return fmt.Errorf("failed to populate registry for %q: %v", id, err)
	}

	// Validate the SceneObject.
	if err := sceneobjectvalidation.ValidateSceneObject(m.GetAssets().GetSceneObjectModel()); err != nil {
		return fmt.Errorf("invalid SceneObject for %q: %w", id, err)
	}

	// Verify that any user data in the SceneObject is in the FileDescriptorSet.
	for key, userData := range m.GetAssets().GetSceneObjectModel().GetUserData() {
		if name := userData.MessageName(); name == "" {
			return fmt.Errorf("user data %q message must not be an empty Any for %q", key, id)
		} else if _, err := files.FindDescriptorByName(name); err != nil {
			return fmt.Errorf("cannot find user data message %q for %q: %w", name, id, err)
		}
	}


	// Optionally verify CAS references
	if opts.casClient != nil {
		if err := validateSceneObjectCASReferences(ctx, m.GetAssets().GetSceneObjectModel(), opts); err != nil {
			return fmt.Errorf("invalid SceneObject CAS reference for %q: %w", id, err)
		}
	}


	return nil
}

func extractSceneObjectFromGZF(gzfPath string) (*sopb.SceneObject, error) {
	tempDir, err := os.MkdirTemp("", "scene_object_")
	if err != nil {
		return nil, fmt.Errorf("failed to create temporary directory: %w", err)
	}
	defer os.RemoveAll(tempDir)

	return gzffile.ExtractSceneObject(gzfPath, tempDir)
}



func validateSceneObjectCASReferences(ctx context.Context, so *sopb.SceneObject, opts *processedSceneObjectManifestOptions) error {
	for ie, entity := range so.GetEntities() {
		if entity.GetLink() != nil {
			for name, geometrySet := range entity.GetLink().GetGeometryComponent().GetNamedGeometries() {
				for ig, geometry := range geometrySet.GetGeometries() {
					if geometry.GetGeometryStorageRefs().GetGeometryRef() != "" {
						if err := verifyCASReference(ctx, geometry.GetGeometryStorageRefs().GetGeometryRef(), opts); err != nil {
							return fmt.Errorf("entity[%d] (%q), geometry %s[%d] geometry ref failed validation: %w", ie, entity.GetName(), name, ig, err)
						}
					}
					if geometry.GetGeometryStorageRefs().GetRenderableRef() != "" {
						if err := verifyCASReference(ctx, geometry.GetGeometryStorageRefs().GetRenderableRef(), opts); err != nil {
							return fmt.Errorf("entity[%d] (%q), geometry %s[%d] renderable ref failed validation: %w", ie, entity.GetName(), name, ig, err)
						}
					}
				}
			}
		}
	}
	return nil
}

func verifyCASReference(ctx context.Context, ref string, opts *processedSceneObjectManifestOptions) error {
	client := opts.casClient
	if client == nil {
		return fmt.Errorf("unexpectedly missing CAS client")
	}
	if _, err := client.Stat(ctx, &caspb.StatRequest{ObjectId: ref}); err != nil {
		return fmt.Errorf("failed to validate CAS reference %q: %w", ref, err)
	}
	return nil
}


