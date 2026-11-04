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

// Command checkimportpathmatchesgopackage checks that option go_package in a proto file matches go_proto_library's importpath attribute.
package main

import (
	"bufio"
	"fmt"
	"os"
	"regexp"
)

// This regex finds the go_package option in a .proto file.
// It captures the package path inside the quotes.
// e.g., option go_package = "example.com/project/protos/foo";
var goPackageRegex = regexp.MustCompile(`^\s*option\s+go_package\s*=\s*"([^"]+)"`)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintf(os.Stderr, "Usage: %s <expected_import_path> <proto_file_path>\n", os.Args[0])
		os.Exit(2)
	}

	expectedImportPath := os.Args[1]
	protoFilePath := os.Args[2]

	file, err := os.Open(protoFilePath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error opening proto file %q: %v\n", protoFilePath, err)
		os.Exit(2)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		matches := goPackageRegex.FindStringSubmatch(line)
		if len(matches) > 1 {
			// The captured group is at index 1.
			goPackagePath := matches[1]
			if goPackagePath == expectedImportPath {
				fmt.Printf("SUCCESS: importpath %q matches go_package in %s\n", expectedImportPath, protoFilePath)
				os.Exit(0)
			} else {
				fmt.Fprintf(os.Stderr, "ERROR: option go_package and go_proto_library's importpath attribute differ in %s\n", protoFilePath)
				fmt.Fprintf(os.Stderr, "  go_proto_library importpath: %q\n", expectedImportPath)
				fmt.Fprintf(os.Stderr, "  .proto file go_package:      %q\n", goPackagePath)
				fmt.Fprintf(os.Stderr, "Please set option go_package in your proto value to %q\n See go/intrinsic-use-option-go-package for more info.", expectedImportPath)
				os.Exit(1)
			}
		}
	}

	if err := scanner.Err(); err != nil {
		fmt.Fprintf(os.Stderr, "Error reading proto file %q: %v\n", protoFilePath, err)
		os.Exit(2)
	}

	fmt.Fprintf(os.Stderr, "ERROR: Could not find 'option go_package' in %s\nPlease add the following to the proto file\noption go_package = \"%s\";", protoFilePath, expectedImportPath)
	os.Exit(1)
}
