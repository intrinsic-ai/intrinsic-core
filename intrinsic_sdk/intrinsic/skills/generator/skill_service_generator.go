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

// Package main provides a skill service generator command line tool.
package main

import (
	_ "embed"
	"errors"
	"flag"
	"fmt"
	"strings"

	"intrinsic/production/intrinsic"
	gen "intrinsic/skills/generator/gen"
	"intrinsic/skills/skillmanifest"
	"intrinsic/util/proto/descriptor"
	"intrinsic/util/proto/protoio"

	log "github.com/golang/glog"
	"google.golang.org/protobuf/proto"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"

	manifestpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

type stringArray []string

func (i *stringArray) String() string {
	return fmt.Sprintf("%v", *i)
}

func (i *stringArray) Set(value string) error {
	if len(*i) > 0 {
		return errors.New("flag already set")
	}

	if value == "" {
		return errors.New("empty value provided for flag")
	}

	for _, s := range strings.Split(value, ",") {
		*i = append(*i, s)
	}
	return nil
}

func (i *stringArray) Get() any {
	return []string(*i)
}

var (
	manifestPath         = flag.String("manifest", "", "The path to the protobin file containing the intrinsic_proto.skills.SkillManifest.")
	fileDescriptorSet    = flag.String("file_descriptor_set", "", "The path to the protobin file containing the file descriptor set.")
	out                  = flag.String("out", "", "The path for the generated skill service file.")
	manifestOut          = flag.String("manifest_out", "", "The path to write the augmented skill manifest protobin.")
	fileDescriptorSetOut = flag.String("file_descriptor_set_out", "", "The path to write the augmented file descriptor set protobin.")
	lang                 = flag.String("lang", "", "The language the skill is implemented in; should be one of: {cpp, python}.")
	ccHeaderPaths        = func() *stringArray {
		p := new(stringArray)
		flag.Var(p, "cc_headers", "The comma-separated list of paths to the cpp proto header files for the skill's cpp deps.")
		return p
	}()
)

const fdsProvidedToPlatformPath = "skill_services_provided_to_platform_transitive_set_sci.proto.bin"

//go:embed skill_services_provided_to_platform_transitive_set_sci.proto.bin
var providedToPlatformFDSBytes []byte

var serviceVersionsProvidedToPlatform = []manifestpb.SkillServicesConfig_ServiceVersion{
	manifestpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_PROJECTOR,
	manifestpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_EXECUTOR,
	manifestpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_SKILL_INFORMATION,
}

// populateServiceVersions adds the services the skill provides to the platform.
func populateServiceVersions(m *manifestpb.SkillManifest) error {
	// We do not add anything if the manifest already contains servive versions.
	if len(m.GetOptions().GetSkillServicesConfig().GetServiceVersions()) != 0 {
		return nil
	}
	if m.GetOptions() == nil {
		m.Options = &manifestpb.Options{}
	}
	if m.GetOptions().GetSkillServicesConfig() == nil {
		m.Options.SkillServicesConfig = &manifestpb.SkillServicesConfig{}
	}
	config := m.GetOptions().GetSkillServicesConfig()
	for _, sv := range serviceVersionsProvidedToPlatform {
		config.ServiceVersions = append(config.ServiceVersions, sv)
	}
	return nil
}

func main() {
	intrinsic.Init()

	manifest := &manifestpb.SkillManifest{}
	if err := protoio.ReadBinaryProto(*manifestPath, manifest); err != nil {
		log.Exitf("cannot read manifest: %v", err)
	}
	fds := &descriptorpb.FileDescriptorSet{}
	if err := protoio.ReadBinaryProto(*fileDescriptorSet, fds); err != nil {
		log.Exitf("failed to read file descriptor set: %v", err)
	}

	providedToPlatformFDS := &descriptorpb.FileDescriptorSet{}
	if err := proto.Unmarshal(providedToPlatformFDSBytes, providedToPlatformFDS); err != nil {
		log.Exitf("failed to unmarshal provided to platform file descriptor set: %v", err)
	}
	augmentedFDS, err := descriptor.MergeFileDescriptorSets([]*descriptorpb.FileDescriptorSet{fds, providedToPlatformFDS})
	if err != nil {
		log.Exitf("failed to merge file descriptor sets: %v", err)
	}
	fds = augmentedFDS

	populateServiceVersions(manifest)
	if err := skillmanifest.PruneSourceCodeInfo(manifest, fds); err != nil {
		log.Exitf("failed to prune source code info: %v", err)
	}

	if err := protoio.WriteBinaryProto(*manifestOut, manifest); err != nil {
		log.Exitf("failed to write augmented skill manifest: %v", err)
	}
	if err := protoio.WriteBinaryProto(*fileDescriptorSetOut, fds); err != nil {
		log.Exitf("failed to write augmented file descriptor set: %v", err)
	}

	switch *lang {
	case "cpp":
		if err := gen.WriteSkillServiceCC(manifest, *ccHeaderPaths, *out); err != nil {
			log.Exitf("Cannot write cc skill service file: %v.", err)
		}
		return
	case "python":
		if err := gen.WriteSkillServicePy(manifest, *out); err != nil {
			log.Exitf("Cannot write py skill service file: %v.", err)
		}
		return
	default:
		log.Exitf("Invalid language selection for skill. lang=%s; should be one of {cpp, python}", *lang)
	}
}
