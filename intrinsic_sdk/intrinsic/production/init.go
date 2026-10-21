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

// Package intrinsic provides initialization functionality for Golang binaries.
package intrinsic

import (
	"flag"
)

// Init is the entry point for our Golang binaries. It parses command line flags and performs
// other common initialization.

// NOTE: k8s binaries should instead use intrinsic/kubernetes/init.go which calls this
// under the hood.

func Init() {
	// Avoids logging command-line args and version information, which we get via
	// other means in our stack.
	flag.Set("silent_init", "true")
	// When manually running a binary, we want to see the logs on stderr.
	// When running in k8s, we want to see them in container logs and disable writing them to a file.
	flag.Set("logtostderr", "true")

	flag.Parse()
}
