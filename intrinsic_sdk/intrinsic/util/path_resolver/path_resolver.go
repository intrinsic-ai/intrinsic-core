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

// Package pathresolver provides functionality to access runfiles
package pathresolver

import (
	"fmt"
	"io/fs"
	"os"
	"path"
	"path/filepath"

	"github.com/bazelbuild/rules_go/go/runfiles"
)

const (
	repoName       = "google3" 
	bzlmodMainName = "_main"   
)


// getCandidatePrefixes returns the minimal list of runfiles directory prefixes to try.
func getCandidatePrefixes() []string {
	prefixes := make([]string, 0, len(migrationLocations)+3)
	for _, loc := range migrationLocations {
		prefixes = append(prefixes, path.Join(bzlmodMainName, loc))
	}
	prefixes = append(prefixes,
		bzlmodMainName, // when paths are specified as `google3/intrinsic/...` or `incode/...`
		"intrinsic_apis+",
		"intrinsic_apis",
	)
	return prefixes
}



// ResolveRunfilesFsRoot gets an fs.FS for runfiles root.
func ResolveRunfilesFsRoot() (fs.FS, error) {
	return runfiles.New()
}

// Env returns additional environmental variables to pass to subprocesses.
// Each element is of the form “key=value”. Pass these variables to
// Bazel-built binaries so they can find their runfiles as well.
func Env() ([]string, error) {
	r, err := runfiles.New()
	if err != nil {
		return nil, err
	}
	return r.Env(), nil
}

// ResolveRunfilesFs gets an fs.FS for runfiles relative to the repo root.
func ResolveRunfilesFs() (fs.FS, error) {
	root, err := ResolveRunfilesFsRoot()

	if err != nil {
		return nil, fmt.Errorf("unable to get runfiles root: %v", err)
	}

	var lastErr error
	var firstFs fs.FS
	for i, prefix := range getCandidatePrefixes() {
		baseDirFs, err := fs.Sub(root, prefix)
		if i == 0 && err == nil {
			firstFs = baseDirFs
		}
		if err == nil {
			// Guarantee the directory is valid in the runfiles tree
			if _, statErr := fs.Stat(baseDirFs, "."); statErr == nil {
				return baseDirFs, nil
			}
		}
		lastErr = err
	}

	if firstFs != nil {
		return firstFs, nil
	}

	return nil, fmt.Errorf("unable to get runfiles sub (tried %v): %v", getCandidatePrefixes(), lastErr)









	//
}

// ResolveRunfilesPath gets the runfiles location of a file.
// Use the typical runfiles path without the repository name.
//
// Example:
//
//	ResolveRunfilesPath("intrinsic/skills/build_defs/tests/no_op_skill_py_manifest.pbbin")
//
// TODO(b/329281292) Deprecate in favor of using bazel runfiles libraries directly.
func ResolveRunfilesPath(p string) (string, error) {
	r, err := runfiles.New()
	if err != nil {
		return "", err
	}


	var lastErr error
	var firstResolved string
	for i, prefix := range getCandidatePrefixes() {
		runfilesPath := path.Join(prefix, p)
		resolved, err := r.Rlocation(runfilesPath)

		if i == 0 && err == nil && resolved != "" {
			firstResolved = resolved
		}

		// Ensure the physical file exists before returning
		if err == nil && resolved != "" {
			if _, statErr := os.Stat(resolved); statErr == nil {
				return resolved, nil
			}
		}
		lastErr = err
	}

	if firstResolved != "" {
		return firstResolved, nil
	}

	return "", fmt.Errorf("unable to resolve path %q in runfiles: %v", p, lastErr)


}

// ResolveRunfilesOrLocalPath gets the runfiles or local location of a file.
//
// This is useful when packaging inside a container, where the runfiles directory
// is not available.
//
// TODO(b/329281292) Deprecate in favor of using bazel runfiles libraries directly.
func ResolveRunfilesOrLocalPath(p string) (string, error) {
	resolvedPath, errRunfile := ResolveRunfilesPath(p)
	if errRunfile == nil {
		return resolvedPath, nil
	}

	// TODO(b/379017440) Allow using runfiles libraries instead.
	// Fallbacks for local execution/container packaging outside Bazel.
	resolvedRepos := []string{
		repoName + "+",
		"google3", 
		".",       // Flat container layout
	}

	for _, resolvedRepo := range resolvedRepos {
		resolvedPath = filepath.Join(".", resolvedRepo, p)
		if _, err := os.Stat(resolvedPath); err == nil {
			return resolvedPath, nil
		}
	}
	return "", fmt.Errorf("unable to resolve path %q:\n  Not available as runfile: %v\n  Not available as local file", p, errRunfile)
}
