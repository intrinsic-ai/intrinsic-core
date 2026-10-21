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

// main validates a skill manifest text proto and builds the binary.
package main

import (
	"flag"
	"fmt"
	"strings"

	"intrinsic/production/intrinsic"
	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"

	log "github.com/golang/glog"

	smpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

var (
	flagManifest                                 = flag.String("manifest", "", "Path to a SkillManifest pbtxt file.")
	flagOutput                                   = flag.String("output", "", "Output path.")
	flagFileDescriptorSetOut                     = flag.String("file_descriptor_set_out", "", "Output path for the file descriptor set.")
	flagFileDescriptorSets                       = flag.String("file_descriptor_sets", "", "Comma separated paths to binary file descriptor set protos.")
	flagIncompatibleDisallowManifestDependencies = flag.Bool("incompatible_disallow_manifest_dependencies", false, "Whether to prevent the skill from declaring dependencies in the manifest.")
)

func createSkillManifest() error {
	var fds []string
	if *flagFileDescriptorSets != "" {
		fds = strings.Split(*flagFileDescriptorSets, ",")
	}
	set, err := registryutil.LoadFileDescriptorSets(fds)
	if err != nil {
		return fmt.Errorf("unable to build FileDescriptorSet: %v", err)
	}

	types, err := registryutil.NewTypesFromFileDescriptorSet(set)
	if err != nil {
		return fmt.Errorf("failed to populate the types registry: %w", err)
	}

	m := new(smpb.SkillManifest)
	if err := protoio.ReadTextProto(*flagManifest, m, protoio.WithResolver(types)); err != nil {
		return fmt.Errorf("failed to read manifest: %v", err)
	}
	if *flagIncompatibleDisallowManifestDependencies && len(m.GetDependencies().GetRequiredEquipment()) > 0 {
		return fmt.Errorf("dependencies declared in the manifest's dependencies field but --incompatible_disallow_manifest_dependencies is true")
	}
	if err := protoio.WriteBinaryProto(*flagOutput, m, protoio.WithDeterministic(true)); err != nil {
		return fmt.Errorf("could not write skill manifest proto: %v", err)
	}

	if err := protoio.WriteBinaryProto(*flagFileDescriptorSetOut, set, protoio.WithDeterministic(true)); err != nil {
		return fmt.Errorf("could not write file descriptor set proto: %v", err)
	}
	return nil
}

func main() {
	intrinsic.Init()
	if err := createSkillManifest(); err != nil {
		log.Exitf("Failed to create skill manifest: %v", err)
	}
}
