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

// Main implements the main CLI for generating a SkillServiceConfig file
package main

import (
	"flag"
	"fmt"

	"intrinsic/production/intrinsic"
	sscg "intrinsic/skills/build_defs/skillserviceconfiggen"
	"intrinsic/util/proto/protoio"

	log "github.com/golang/glog"

	smpb "intrinsic/skills/proto/skill_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
)

var (
	flagManifestPbbinFilename   = flag.String("manifest_pbbin_filename", "", "Filename for the binary skill manifest proto.")
	flagProtoDescriptorFilename = flag.String("proto_descriptor_filename", "", "Filename for FileDescriptorSet for skill parameter, return value and published topic protos.")
	flagOutputConfigFilename    = flag.String("output_config_filename", "", "Output filename.")
)

func checkArguments() error {
	if len(*flagManifestPbbinFilename) == 0 {
		return fmt.Errorf("--manifest_pbbin_filename is required")
	}
	if len(*flagProtoDescriptorFilename) == 0 {
		return fmt.Errorf("--proto_descriptor_filename is required")
	}
	if len(*flagProtoDescriptorFilename) == 0 {
		return fmt.Errorf("--output_config_filename is required")
	}
	return nil
}

func main() {
	intrinsic.Init()
	// Fail fast if CLI arguments are invalid.
	if err := checkArguments(); err != nil {
		log.Exitf("Invalid arguments: %v", err)
	}

	fileDescriptorSet := new(dpb.FileDescriptorSet)
	if err := protoio.ReadBinaryProto(*flagProtoDescriptorFilename, fileDescriptorSet); err != nil {
		log.Exitf("Unable to read FileDescriptorSet: %v", err)
	}
	manifest := new(smpb.SkillManifest)
	if err := protoio.ReadBinaryProto(*flagManifestPbbinFilename, manifest); err != nil {
		log.Exitf("Unable to read manifest: %v", err)
	}

	skillServiceConfig, err := sscg.ExtractSkillServiceConfigFromManifest(manifest, fileDescriptorSet)
	if err != nil {
		log.Exitf("Unable to extract SkillServiceConfig: %v", err)
	}

	if err := protoio.WriteBinaryProto(*flagOutputConfigFilename, skillServiceConfig, protoio.WithDeterministic(true)); err != nil {
		log.Exitf("Unable to generate SkillServiceConfig: %v", err)
	}
}
