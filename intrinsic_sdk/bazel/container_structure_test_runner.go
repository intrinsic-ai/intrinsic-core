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

// Binary container_structure_test_runner validates container image layers
// against structure test assertions defined in YAML configurations.
//
// Note: This runner is intentionally designed as a lightweight, zero-unpacking
// streaming inspector focused exclusively on fileExistenceTests. It does not
// execute container runtimes (commandTests) or inspect image manifest configs (metadataTests),
// because executing commands requires a full container daemon/runtime (containerd/dockerd/runc)
// and an unpacked rootfs, which contradicts this runner's zero-unpacking streaming architecture.
package main

import (
	"archive/tar"
	"bufio"
	"compress/gzip"
	"errors"
	"flag"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/klauspost/compress/zstd"
	"gopkg.in/yaml.v3"
)

// fileExistenceTest defines assertions for a single file within the container filesystem.
type fileExistenceTest struct {
	Name         string `yaml:"name"`
	Path         string `yaml:"path"`
	Permissions  string `yaml:"permissions"`
	ExecutableBy string `yaml:"isExecutableBy"`
	ShouldExist  *bool  `yaml:"shouldExist"`
}

// structureTestConfig represents the root structure test configuration file.
// Only fileExistenceTests are executed by this lightweight runner.
// Other test types (commandTests, fileContentTests, metadataTest, licenseTests)
// require container execution or metadata inspection and are not supported.
type structureTestConfig struct {
	FileExistenceTests []fileExistenceTest `yaml:"fileExistenceTests"`
	// CommandTests are intentionally unsupported because command execution requires
	// a container runtime and an unpacked rootfs. Configurations containing commandTests
	// will fail fast.
	CommandTests     []any `yaml:"commandTests"`
	FileContentTests []any `yaml:"fileContentTests"`
	MetadataTest     any   `yaml:"metadataTest"`
	LicenseTests     []any `yaml:"licenseTests"`
}

// containerFS tracks filesystem entries and symlink aliases discovered across container layers.
type containerFS struct {
	files    map[string]os.FileMode
	symlinks map[string][]string
}

func newContainerFS() *containerFS {
	return &containerFS{
		files:    make(map[string]os.FileMode),
		symlinks: make(map[string][]string),
	}
}

// normalizePath cleans and normalizes a POSIX file path for comparison,
// stripping leading slashes and redundant path elements.
func normalizePath(rawPath string) string {
	return strings.TrimPrefix(path.Clean("/"+strings.TrimSpace(rawPath)), "/")
}

// resolveRunfile locates a file path on the filesystem or within the Bazel runfiles directory.
func resolveRunfile(filePath string) string {
	if _, err := os.Stat(filePath); err == nil {
		return filePath
	}
	for _, baseDir := range []string{os.Getenv("RUNFILES_DIR"), os.Getenv("TEST_SRCDIR")} {
		if baseDir == "" {
			continue
		}
		for _, prefix := range []string{"_main", ""} {
			candidate := filepath.Join(baseDir, prefix, filePath)
			if _, err := os.Stat(candidate); err == nil {
				return candidate
			}
		}
	}
	return filePath
}

// scanPath populates the containerFS with paths, modes, and symlinks found in the given file or directory.
// If layerPath is a directory, it walks the directory searching for tar archives.
func scanPath(layerPath string, cfs *containerFS) error {
	resolved := resolveRunfile(layerPath)
	fileInfo, err := os.Stat(resolved)
	if err != nil {
		return fmt.Errorf("stat layer %q: %w", resolved, err)
	}
	if fileInfo.IsDir() {
		return filepath.WalkDir(resolved, func(entryPath string, d fs.DirEntry, walkErr error) error {
			if walkErr != nil {
				return walkErr
			}
			if d.IsDir() {
				return nil
			}
			return scanTarArchive(entryPath, cfs)
		})
	}
	return scanTarArchive(resolved, cfs)
}

// scanTarArchive inspects a tar (or gzipped/zstd-compressed tar) file and records all entry paths, modes, and symlinks.
func scanTarArchive(archivePath string, cfs *containerFS) error {
	file, err := os.Open(archivePath)
	if err != nil {
		return fmt.Errorf("open archive %q: %w", archivePath, err)
	}
	defer file.Close()

	var reader io.Reader = file
	var magic [4]byte
	n, readErr := file.Read(magic[:])
	if readErr != nil && !errors.Is(readErr, io.EOF) {
		return fmt.Errorf("read magic header from %q: %w", archivePath, readErr)
	}

	if _, err := file.Seek(0, io.SeekStart); err != nil {
		return fmt.Errorf("seek archive %q: %w", archivePath, err)
	}

	isGzip := n >= 2 && magic[0] == 0x1f && magic[1] == 0x8b
	isZstd := n >= 4 && magic[0] == 0x28 && magic[1] == 0xb5 && magic[2] == 0x2f && magic[3] == 0xfd
	if isGzip {
		gzipReader, err := gzip.NewReader(file)
		if err != nil {
			return fmt.Errorf("create gzip reader for %q: %w", archivePath, err)
		}
		defer gzipReader.Close()
		reader = gzipReader
	} else if isZstd {
		zstdReader, err := zstd.NewReader(file)
		if err != nil {
			return fmt.Errorf("create zstd reader for %q: %w", archivePath, err)
		}
		defer zstdReader.Close()
		reader = zstdReader
	}

	tarReader := tar.NewReader(bufio.NewReaderSize(reader, 64*1024))
	entriesRead := 0
	for {
		header, err := tarReader.Next()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			if entriesRead > 0 || isGzip || isZstd {
				return fmt.Errorf("read tar entry from %q: %w", archivePath, err)
			}
			// Non-tar auxiliary files (e.g. index.json, oci-layout in OCI directories)
			// are safely ignored on the first header if uncompressed.
			return nil
		}
		entriesRead++

		name := normalizePath(header.Name)
		if name == "" {
			continue
		}

		cfs.files[name] = header.FileInfo().Mode()

		if header.Typeflag == tar.TypeSymlink || header.Typeflag == tar.TypeLink {
			rawTarget := strings.TrimSpace(header.Linkname)
			if rawTarget != "" {
				var targets []string
				if strings.HasPrefix(rawTarget, "/") {
					targets = append(targets, normalizePath(rawTarget))
				} else {
					dir := path.Dir(name)
					relTarget := normalizePath(path.Join(dir, rawTarget))
					rootTarget := normalizePath(rawTarget)
					if relTarget != "" {
						targets = append(targets, relTarget)
					}
					// Fallback heuristic: Some container layer builders create relative symlinks
					// that are intended to resolve relative to the layer/image root (or prefix namespace)
					// rather than relative to the symlink's parent directory.
					if rootTarget != relTarget && rootTarget != "" {
						targets = append(targets, rootTarget)
					}
				}
				cfs.symlinks[name] = append(cfs.symlinks[name], targets...)
			}
		}
	}
	return nil
}

// findFile recursively resolves targetPath against files, direct symlinks, and directory prefix symlinks.
func findFile(curr string, cfs *containerFS, visited map[string]bool) (os.FileMode, bool) {
	curr = normalizePath(curr)
	if curr == "" {
		return 0, false
	}
	if visited[curr] {
		return 0, false
	}
	visited[curr] = true
	defer func() {
		delete(visited, curr)
	}()

	// 1. If curr is a recorded symlink, follow its targets:
	if targets, isSymlink := cfs.symlinks[curr]; isSymlink {
		for _, target := range targets {
			if targetMode, found := findFile(target, cfs, visited); found {
				return targetMode, true
			}
		}
		// Recorded symlink whose targets cannot be resolved.
		return 0, false
	}

	// 2. If curr exists directly as a regular file or directory:
	if mode, exists := cfs.files[curr]; exists {
		return mode, true
	}

	// 3. Prefix symlink expansion: check if any ancestor directory of curr is a symlink.
	for prefix := path.Dir(curr); prefix != "." && prefix != "/"; prefix = path.Dir(prefix) {
		if targets, isSymlink := cfs.symlinks[prefix]; isSymlink {
			suffix := curr[len(prefix)+1:]
			for _, target := range targets {
				expanded := normalizePath(path.Join(target, suffix))
				if targetMode, found := findFile(expanded, cfs, visited); found {
					return targetMode, true
				}
			}
		}
	}

	return 0, false
}

// unixPermissions extracts the 12-bit Unix permission bits (standard 9 bits + setuid, setgid, sticky).
func unixPermissions(mode os.FileMode) uint32 {
	perm := uint32(mode.Perm())
	if mode&os.ModeSetuid != 0 {
		perm |= 04000
	}
	if mode&os.ModeSetgid != 0 {
		perm |= 02000
	}
	if mode&os.ModeSticky != 0 {
		perm |= 01000
	}
	return perm
}

// checkFileExistence validates a single file assertion against the scanned container filesystem.
// It returns an error if the assertion fails, or nil if it passes.
func checkFileExistence(test fileExistenceTest, cfs *containerFS) error {
	targetPath := normalizePath(test.Path)
	shouldExist := test.ShouldExist == nil || *test.ShouldExist
	visited := make(map[string]bool)
	mode, exists := findFile(targetPath, cfs, visited)

	if shouldExist && !exists {
		if targets, isSymlink := cfs.symlinks[targetPath]; isSymlink {
			return fmt.Errorf("path %q was resolved as a symlink pointing to %v, but target destination was not found in any scanned layer", targetPath, targets)
		}
		return fmt.Errorf("path %q was not found in any layer", targetPath)
	}
	if !shouldExist && exists {
		return fmt.Errorf("path %q exists but shouldExist is false", targetPath)
	}
	if !exists {
		return nil
	}

	if test.ExecutableBy != "" {
		var mask os.FileMode
		switch strings.ToLower(strings.TrimSpace(test.ExecutableBy)) {
		case "owner":
			mask = 0o100
		case "group":
			mask = 0o010
		case "other":
			mask = 0o001
		case "any":
			mask = 0o111
		default:
			return fmt.Errorf("path %q: unsupported isExecutableBy value %q", targetPath, test.ExecutableBy)
		}
		if mode.Perm()&mask == 0 {
			return fmt.Errorf("path %q mode 0%o is not executable by %s", targetPath, mode.Perm(), test.ExecutableBy)
		}
	}

	if test.Permissions != "" {
		permStr := strings.TrimSpace(test.Permissions)
		symbolic := mode.Perm().String()
		symbolic9 := strings.TrimPrefix(symbolic, "-")
		fullSymbolic := mode.String()

		if expectedOctal, err := strconv.ParseUint(permStr, 8, 32); err == nil {
			actualPerm := uint32(mode.Perm())
			if expectedOctal > 0777 {
				actualPerm = unixPermissions(mode)
			}
			if uint32(expectedOctal) != actualPerm {
				return fmt.Errorf("path %q permission mismatch: expected 0%o, got 0%o (%s)", targetPath, expectedOctal, actualPerm, symbolic)
			}
		} else if permStr != symbolic && permStr != symbolic9 && permStr != fullSymbolic && strings.TrimPrefix(permStr, "d") != symbolic9 {
			return fmt.Errorf("path %q permission mismatch: expected %s, got %s (0%o)", targetPath, test.Permissions, symbolic, mode.Perm())
		}
	}

	return nil
}

// run executes the structure test runner with the given CLI arguments and I/O streams.
// It executes fileExistenceTests across all specified configs and layer archives.
// Unsupported test types (such as commandTests or metadataTest) fail execution fast.
func run(args []string, stdout, stderr io.Writer) error {
	flagSet := flag.NewFlagSet("container_structure_test_runner", flag.ContinueOnError)
	flagSet.SetOutput(stderr)

	configsFlag := flagSet.String("configs", "", "comma-separated test config paths")
	layersFlag := flagSet.String("layers", "", "comma-separated layer paths")
	targetFlag := flagSet.String("target", "", "target label")

	if err := flagSet.Parse(args); err != nil {
		return err
	}

	cfs := newContainerFS()
	var failureMessages []string
	var scannedLayers []string

	for _, layer := range strings.Split(*layersFlag, ",") {
		layer = strings.TrimSpace(layer)
		if layer == "" {
			continue
		}
		scannedLayers = append(scannedLayers, layer)
		if err := scanPath(layer, cfs); err != nil {
			fmt.Fprintf(stderr, "Error scanning layer %q: %v\n", layer, err)
			return fmt.Errorf("scan layer %q: %w", layer, err)
		}
	}

	totalTests := 0
	for _, configPath := range strings.Split(*configsFlag, ",") {
		configPath = strings.TrimSpace(configPath)
		if configPath == "" {
			continue
		}

		resolvedConfig := resolveRunfile(configPath)
		data, err := os.ReadFile(resolvedConfig)
		if err != nil {
			fmt.Fprintf(stderr, "Error reading config %q: %v\n", configPath, err)
			return fmt.Errorf("read config %q: %w", configPath, err)
		}

		var config structureTestConfig
		if err := yaml.Unmarshal(data, &config); err != nil {
			fmt.Fprintf(stderr, "Error parsing config %q: %v\n", configPath, err)
			return fmt.Errorf("parse config %q: %w", configPath, err)
		}

		if len(config.CommandTests) > 0 || config.MetadataTest != nil || len(config.FileContentTests) > 0 || len(config.LicenseTests) > 0 {
			return fmt.Errorf("config %q contains unsupported test types (commandTests, metadataTest, fileContentTests, licenseTests); the lightweight streaming runner only supports fileExistenceTests", configPath)
		}

		for _, test := range config.FileExistenceTests {
			totalTests++
			if err := checkFileExistence(test, cfs); err != nil {
				testName := test.Name
				if testName == "" {
					testName = test.Path
				}
				failureMessages = append(failureMessages, fmt.Sprintf("FAIL [%s]: %v", testName, err))
			}
		}
	}

	if totalTests == 0 && len(failureMessages) == 0 {
		failureMessages = append(failureMessages, "no fileExistenceTests found across provided config(s)")
	}

	if len(failureMessages) > 0 {
		fmt.Fprintf(stderr, "\nStructure tests failed for %s:\n", *targetFlag)
		for _, failure := range failureMessages {
			fmt.Fprintf(stderr, "  * %s\n", failure)
		}
		if len(scannedLayers) > 0 {
			fmt.Fprintf(stderr, "\nScanned layer(s) (%d):\n", len(scannedLayers))
			for _, layer := range scannedLayers {
				fmt.Fprintf(stderr, "  - %s\n", layer)
			}
		}
		fmt.Fprintln(stderr, "\n💡 Hint: This test ran with layer streaming (only a subset of image layers was inspected).")
		fmt.Fprintln(stderr, "  * If the missing file or symlink target is provided by third-party packages (e.g. pip wheels) or base image:")
		fmt.Fprintln(stderr, "    Add the layer to test_layers, e.g. test_layers = [\":<name>_packages_layer\", ...]")
		fmt.Fprintln(stderr, "  * To disable container structure tests for this target, pass test_layers = []")
		return errors.New("container structure tests failed")
	}

	fmt.Fprintf(stdout, "PASS: %d container structure test(s) passed for %s\n", totalTests, *targetFlag)
	return nil
}

func main() {
	if err := run(os.Args[1:], os.Stdout, os.Stderr); err != nil {
		os.Exit(1)
	}
}
