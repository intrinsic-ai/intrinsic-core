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

// Package skillgen implements creation of a Skill Asset bundle.
package skillgen

import (
	"context"
	"fmt"

	"intrinsic/skills/skillbundle"
	"intrinsic/skills/skillfix"
	"intrinsic/skills/skillvalidate"
	"intrinsic/util/proto/protoio"

	smpb "intrinsic/skills/proto/skill_manifest_go_proto"

	"google.golang.org/protobuf/reflect/protodesc"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

// CreateSkillBundleOptions provides the data needed to create a Skill Asset bundle.
type CreateSkillBundleOptions struct {
	FileDescriptorSetPath string
	ImageTarPath          string
	ManifestPath          string
	OutputBundlePath      string
}

// CreateSkillBundle creates a Skill Asset bundle on disk.
func CreateSkillBundle(ctx context.Context, opts *CreateSkillBundleOptions) error {
	fds := &descriptorpb.FileDescriptorSet{}
	if err := protoio.ReadBinaryProto(opts.FileDescriptorSetPath, fds); err != nil {
		return fmt.Errorf("failed to read file descriptor set: %w", err)
	}
	m := &smpb.SkillManifest{}
	if err := protoio.ReadBinaryProto(opts.ManifestPath, m); err != nil {
		return fmt.Errorf("failed to read manifest: %w", err)
	}

	if err := skillfix.Manifest(m, skillfix.WithPopulateOldFields(true)); err != nil {
		return fmt.Errorf("unable to make manifest compatible with the latest version of the platform: %v", err)
	}
	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return fmt.Errorf("failed to populate the registry: %w", err)
	}
	if err := skillvalidate.SkillManifest(ctx, m, files); err != nil {
		return fmt.Errorf("invalid SkillManifest: %w", err)
	}

	if err := skillbundle.WriteFile(ctx, m, opts.OutputBundlePath,
		skillbundle.WithFileDescriptorSet(fds),
		skillbundle.WithImageTarPath(opts.ImageTarPath),
	); err != nil {
		return fmt.Errorf("failed to write Skill Asset bundle: %w", err)
	}

	return nil
}
