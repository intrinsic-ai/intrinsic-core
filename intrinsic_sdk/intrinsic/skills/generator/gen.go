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

// Package gen provides code generation utilities for generating skills
package gen

import (
	"bufio"
	"embed"
	"fmt"
	"os"
	"strings"
	"text/template"

	manifestpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

type templateCCParameters struct {
	CCHeaderPaths     []string
	CreateSkillMethod string
}

type templatePyParameters struct {
	PythonModules       []string
	ParameterDescriptor string
	CreateSkillMethod   string
}

//go:embed skill_service_main_tmpl.cc
//go:embed skill_service_main.py.tmpl
var embeddedTemplate embed.FS

func writeCCTemplateOutput(parameters templateCCParameters, out string) error {
	template, err := template.New("skill_service_main_tmpl.cc").
		Funcs(template.FuncMap{"join": strings.Join}).
		ParseFS(embeddedTemplate, "skill_service_main_tmpl.cc")
	if err != nil {
		return fmt.Errorf("cannot parse template: %v", err)
	}

	f, err := os.Create(out)
	if err != nil {
		return fmt.Errorf("cannot create file %q: %v", out, err)
	}
	defer f.Close()

	w := bufio.NewWriter(f)
	if err := template.Execute(w, parameters); err != nil {
		return fmt.Errorf("cannot populate: %v", err)
	}
	return w.Flush()
}

func writePyTemplateOutput(parameters templatePyParameters, out string) error {
	template, err := template.New("skill_service_main.py.tmpl").
		ParseFS(embeddedTemplate, "skill_service_main.py.tmpl")
	if err != nil {
		return fmt.Errorf("cannot parse template: %v", err)
	}

	f, err := os.Create(out)
	if err != nil {
		return fmt.Errorf("cannot create file %q: %v", out, err)
	}
	defer f.Close()

	w := bufio.NewWriter(f)
	if err := template.Execute(w, parameters); err != nil {
		return fmt.Errorf("cannot populate: %v", err)
	}
	return w.Flush()
}

func lastString(parts []string) string {
	if len(parts) == 0 {
		return ""
	}

	if len(parts) == 1 {
		return parts[0]
	}

	return parts[len(parts)-1]
}

func pyDescriptorFrom(protoModule, protoMessageFullName string) string {
	if protoMessageFullName == "" {
		return "None"
	}
	return protoModule + "." + lastString(strings.Split(protoMessageFullName, ".")) + ".DESCRIPTOR"
}

// WriteSkillServiceCC writes a skill service main to the file at out.
//
// The manifestPath must refer to a file that contains an intrinsic_proto.skills.SkillManifest
// proto binary. ccHeaderPaths are the path(s) to the proto header file(s) for the skill's
// protobuf schema(s). out is the file path to write the generated service main to.
//
// The template is specified at skill_service_main_tmpl.cc
func WriteSkillServiceCC(manifest *manifestpb.SkillManifest, ccHeaderPaths []string, out string) error {
	return writeCCTemplateOutput(
		templateCCParameters{
			CCHeaderPaths:     ccHeaderPaths,
			CreateSkillMethod: manifest.GetOptions().GetCcConfig().GetCreateSkill(),
		},
		out,
	)
}

// WriteSkillServicePy writes a skill service main to the file at out.
//
// The manifestPath must refer to a file that contains an intrinsic_proto.skills.SkillManifest
// proto binary. pyModules are the module(s) to to import for the skill, these must at least include
// the module where the skill create method is declared and the proto schema modules for the skill.
// out is the file path to write the generated service main to.
//
// The template is specified at skill_service_main_tmpl.py
func WriteSkillServicePy(manifest *manifestpb.SkillManifest, out string) error {
	return writePyTemplateOutput(
		templatePyParameters{
			PythonModules: []string{
				manifest.GetOptions().GetPythonConfig().GetSkillModule(),
				manifest.GetOptions().GetPythonConfig().GetProtoModule(),
			},
			ParameterDescriptor: pyDescriptorFrom(
				manifest.GetOptions().GetPythonConfig().GetProtoModule(),
				manifest.GetParameter().GetMessageFullName()),
			CreateSkillMethod: manifest.GetOptions().GetPythonConfig().GetCreateSkill(),
		},
		out,
	)
}
