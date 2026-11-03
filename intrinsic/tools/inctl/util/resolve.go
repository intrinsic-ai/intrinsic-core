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

// Package resolve lets users resolve paths when running with or without runfiles.
package resolve

import (
	"os"
	"path"
	"path/filepath"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/pkg/errors"
)

// Resolve tries to find a binary adjacent to the currently-running binary
// (eg when run from BinFS) or in runfiles (eg when using `bazel run`). `name` is
// an rlocation path (as for runfiles.Rlocation()). If the file is not found next
// to the current binary, it looks in the runfiles. It returns an absolute path
// to the located binary, or an error.
func Resolve(name string) (string, error) {
	return ResolveWithBinFsPath(name, filepath.Base(name))
}

// ResolveWithBinFsPath acts like Resolve, but allows customization of the expected
// path in binfs relative to the current executable.
func ResolveWithBinFsPath(name string, binfs string) (string, error) {
	executable, err := os.Executable()
	if err != nil {
		return "", errors.Wrap(err, "os.Executable")
	}
	result := path.Join(filepath.Dir(executable), binfs)
	if _, err := os.Stat(result); err == nil {
		return result, nil
	}
	result, err = runfiles.Rlocation(name)
	if err != nil {
		return "", err
	}
	return result, nil
}
