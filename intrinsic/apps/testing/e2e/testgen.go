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

// Package main generates a test script from a template.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"intrinsic/production/intrinsic"

	log "github.com/golang/glog"

	_ "embed"
)

//go:embed test_template_sh.tmpl
var tmpl string

var (
	testConfigFile = flag.String("test_config", "", "JSON encoded test configuration.")
	outputFile     = flag.String("output_file", "", "Path to the output file.")
)

type testConfigItemType struct {
	Type   string   `json:"type"`
	Files  []string `json:"files"`
	Params []string `json:"params"`
}

type testConfigType struct {
	Solution      string                `json:"solution"`
	OperationMode string                `json:"operation_mode"`
	RunAsService  bool                  `json:"run_as_service"`
	ServiceBundle string                `json:"service_bundle"`
	ServiceId     string                `json:"service_id"`
	ServiceName   string                `json:"service_name"`
	Tests         []*testConfigItemType `json:"tests"`
}

type templateData = map[string]any

func generate(testConfig *testConfigType) error {
	output, err := os.Create(*outputFile)
	if err != nil {
		return fmt.Errorf("error creating output file: %w", err)
	}
	defer output.Close()

	var testFunctions []templateData
	for _, test := range testConfig.Tests {
		for i, testFile := range test.Files {
			testFunction := templateData{
				"TestType":   test.Type,
				"TestName":   fmt.Sprintf("%02d_%s", i, strings.ReplaceAll(filepath.Base(testFile), ".", "_")),
				"TestFile":   testFile,
				"TestParams": test.Params,
			}
			testFunctions = append(testFunctions, testFunction)
		}
	}

	tmpl, err := template.New("test_template_sh.tmpl").Parse(tmpl)
	if err != nil {
		return fmt.Errorf("error parsing template file: %w", err)
	}
	if err := tmpl.Execute(output, templateData{
		"Solution":      testConfig.Solution,
		"OperationMode": testConfig.OperationMode,
		"RunAsService":  testConfig.RunAsService,
		"ServiceBundle": testConfig.ServiceBundle,
		"ServiceId":     testConfig.ServiceId,
		"ServiceName":   testConfig.ServiceName,
		"TestFunctions": testFunctions,
	}); err != nil {
		return fmt.Errorf("error executing header template: %w", err)
	}

	return nil
}

func main() {
	intrinsic.Init()

	if *testConfigFile == "" {
		log.Fatal("Error: --test_config is required")
	}
	testConfigFile, err := os.Open(*testConfigFile)
	if err != nil {
		log.Fatalf("Error reading test config file: %v\n", err)
	}
	testConfigDecoder := json.NewDecoder(testConfigFile)
	testConfig := testConfigType{}
	if err := testConfigDecoder.Decode(&testConfig); err != nil {
		log.Fatalf("Error parsing test config: %v\n", err)
	}
	if len(testConfig.Tests) == 0 {
		log.Fatal("Error: You must specify at least one test.")
	}

	if *outputFile == "" {
		log.Fatal("Error: --output_file is required")
	}

	if err := generate(&testConfig); err != nil {
		log.Fatalf("Error generating test: %v\n", err)
	}
}
