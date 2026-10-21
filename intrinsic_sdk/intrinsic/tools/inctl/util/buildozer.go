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

// package buildozer contains utilities for using buildozer
package buildozer

import (
	"bytes"
	"fmt"
	"strings"

	"github.com/bazelbuild/buildtools/edit"
)

// ExecuteBuildozerCommands runs the given package-level buildozer commands
// (which have the form "buildozer ... //my_package:__pkg__"). Modify or
// fork this function if you need to run non-package-level buildozer commands.
func ExecuteBuildozerCommands(cmds []string, bazelWorkspaceDir string, bazelPackage []string) error {
	opts := edit.NewOpts()
	opts.RootDir = bazelWorkspaceDir
	packageLabel := fmt.Sprintf("//%s:__pkg__", strings.Join(bazelPackage, "/"))

	for _, cmd := range cmds {
		// Capture and suppress output (buildozer uses stdout/stderr by default)
		// and only print it in case of an error (see below).
		var out, err bytes.Buffer
		opts.OutWriter = &out
		opts.ErrWriter = &err

		args := []string{cmd, packageLabel}
		result := edit.Buildozer(opts, args)

		// Buildozer return codes:
		// 0: success
		// 3: no error, but no files were modified
		if result != 0 && result != 3 {
			return fmt.Errorf("command %q returned with error code %d:\n%s",
				strings.Join(args, " "), result, err.String())
		}
	}

	return nil
}
