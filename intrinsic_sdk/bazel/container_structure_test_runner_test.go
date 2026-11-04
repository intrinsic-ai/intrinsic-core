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

package main

import (
	"archive/tar"
	"bytes"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestFindFileDirect(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["app/bin/server"] = 0o755

	visited := make(map[string]bool)
	mode, found := findFile("/app/bin/server", cfs, visited)
	if !found {
		t.Fatalf("expected /app/bin/server to be found")
	}
	if mode != 0o755 {
		t.Fatalf("expected mode 0755, got 0%o", mode)
	}
}

func TestFindFileDirectSymlink(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["app/bin/target_binary"] = 0o755
	cfs.symlinks["app/bin/link_binary"] = []string{"app/bin/target_binary"}

	visited := make(map[string]bool)
	mode, found := findFile("app/bin/link_binary", cfs, visited)
	if !found {
		t.Fatalf("expected app/bin/link_binary to be resolved")
	}
	if mode != 0o755 {
		t.Fatalf("expected mode 0755, got 0%o", mode)
	}
}

func TestFindFileDirectoryPrefixSymlink(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["opt/intrinsic/world/service/updater/world_updater_main"] = 0o755
	cfs.symlinks["intrinsic"] = []string{"opt/intrinsic"}

	visited := make(map[string]bool)
	mode, found := findFile("/intrinsic/world/service/updater/world_updater_main", cfs, visited)
	if !found {
		t.Fatalf("expected /intrinsic/world/service/updater/world_updater_main to be resolved via directory symlink")
	}
	if mode != 0o755 {
		t.Fatalf("expected mode 0755, got 0%o", mode)
	}
}

func TestFindFileNestedSkillsDirectorySymlink(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["::skills::/opt/intrinsic/world/skills/remove_object/_remove_object_skill_binary"] = 0o755
	cfs.symlinks["::skills::/intrinsic"] = []string{
		"::skills::/opt/intrinsic",
		"opt/intrinsic",
	}

	visited := make(map[string]bool)
	mode, found := findFile("/::skills::/intrinsic/world/skills/remove_object/_remove_object_skill_binary", cfs, visited)
	if !found {
		t.Fatalf("expected /::skills::/intrinsic/... to be resolved via nested directory symlink")
	}
	if mode != 0o755 {
		t.Fatalf("expected mode 0755, got 0%o", mode)
	}
}

func TestFindFileChainedSymlinks(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["usr/lib/app/server"] = 0o755
	cfs.symlinks["bin/app"] = []string{"opt/bin/server"}
	cfs.symlinks["opt/bin"] = []string{"usr/lib/app"}

	visited := make(map[string]bool)
	mode, found := findFile("bin/app", cfs, visited)
	if !found {
		t.Fatalf("expected chained symlink bin/app to resolve to usr/lib/app/server")
	}
	if mode != 0o755 {
		t.Fatalf("expected mode 0755, got 0%o", mode)
	}
}

func TestFindFileSymlinkLoop(t *testing.T) {
	cfs := newContainerFS()
	cfs.symlinks["loop_a"] = []string{"loop_b"}
	cfs.symlinks["loop_b"] = []string{"loop_a"}

	visited := make(map[string]bool)
	_, found := findFile("loop_a", cfs, visited)
	if found {
		t.Fatalf("expected loop_a to fail resolution safely without infinite recursion")
	}
}

func TestFindFileBrokenSymlink(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["broken_link"] = 0o755
	cfs.symlinks["broken_link"] = []string{"nonexistent_target"}

	visited := make(map[string]bool)
	_, found := findFile("broken_link", cfs, visited)
	if found {
		t.Fatalf("expected broken_link to return found=false")
	}
}

func TestFindFileBacktrackingSharedIntermediate(t *testing.T) {
	cfs := newContainerFS()
	// Two symlink prefixes that both reference 'common/dir'
	cfs.symlinks["alias_bad"] = []string{"common/dir"}
	cfs.symlinks["alias_good"] = []string{"common/dir"}
	cfs.files["common/dir/valid_file"] = 0o644

	// First search via alias_bad/other_file fails, then search via alias_good/valid_file succeeds
	visited := make(map[string]bool)
	_, foundBad := findFile("alias_bad/other_file", cfs, visited)
	if foundBad {
		t.Fatalf("expected alias_bad/other_file to not be found")
	}

	mode, foundGood := findFile("alias_good/valid_file", cfs, visited)
	if !foundGood {
		t.Fatalf("expected alias_good/valid_file to be found after backtracking")
	}
	if mode != 0o644 {
		t.Fatalf("expected mode 0644, got 0%o", mode)
	}
}

func TestCheckFileExistence(t *testing.T) {
	cfs := newContainerFS()
	cfs.files["opt/intrinsic/app/main"] = 0o755
	cfs.symlinks["intrinsic"] = []string{"opt/intrinsic"}

	trueVal := true
	falseVal := false

	// Test 1: existing via symlink with executable check
	err := checkFileExistence(fileExistenceTest{
		Path:         "/intrinsic/app/main",
		ShouldExist:  &trueVal,
		ExecutableBy: "owner",
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for existing file: %v", err)
	}

	// Test 2: non-existent file shouldExist = false
	err = checkFileExistence(fileExistenceTest{
		Path:        "/intrinsic/app/missing",
		ShouldExist: &falseVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for missing file with shouldExist=false: %v", err)
	}

	// Test 3: non-existent file shouldExist = true
	err = checkFileExistence(fileExistenceTest{
		Path:        "/intrinsic/app/missing",
		ShouldExist: &trueVal,
	}, cfs)
	if err == nil {
		t.Fatalf("expected error for missing file when shouldExist=true")
	}

	// Test 4: directory permission checks (drwxr-xr-x, -rwxr-xr-x, 0755)
	cfs.files["opt/intrinsic/dir"] = os.ModeDir | 0o755
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/dir",
		Permissions: "drwxr-xr-x",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for directory with drwxr-xr-x permissions: %v", err)
	}
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/dir",
		Permissions: "-rwxr-xr-x",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for directory with -rwxr-xr-x permissions: %v", err)
	}
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/dir",
		Permissions: "0755",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for directory with 0755 permissions: %v", err)
	}

	// Test 5: setuid, setgid, and sticky bit permissions
	cfs.files["opt/intrinsic/suid"] = os.ModeSetuid | 0o755
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/suid",
		Permissions: "4755",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for setuid file with 4755 permissions: %v", err)
	}
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/suid",
		Permissions: "04755",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for setuid file with 04755 permissions: %v", err)
	}

	cfs.files["opt/intrinsic/sgid"] = os.ModeSetgid | 0o755
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/sgid",
		Permissions: "2755",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for setgid file with 2755 permissions: %v", err)
	}

	cfs.files["opt/intrinsic/sticky"] = os.ModeSticky | 0o755
	err = checkFileExistence(fileExistenceTest{
		Path:        "/opt/intrinsic/sticky",
		Permissions: "1755",
		ShouldExist: &trueVal,
	}, cfs)
	if err != nil {
		t.Fatalf("unexpected error for sticky file with 1755 permissions: %v", err)
	}
}

func createTarArchive(t *testing.T, targetPath string, files map[string][]byte, symlinks map[string]string, isGzip bool) {
	t.Helper()
	var buf bytes.Buffer
	var tw *tar.Writer
	var gw *gzip.Writer

	if isGzip {
		gw = gzip.NewWriter(&buf)
		tw = tar.NewWriter(gw)
	} else {
		tw = tar.NewWriter(&buf)
	}

	for name, content := range files {
		hdr := &tar.Header{
			Name:     name,
			Mode:     0o755,
			Size:     int64(len(content)),
			Typeflag: tar.TypeReg,
		}
		if err := tw.WriteHeader(hdr); err != nil {
			t.Fatalf("write tar header: %v", err)
		}
		if _, err := tw.Write(content); err != nil {
			t.Fatalf("write tar body: %v", err)
		}
	}

	for name, target := range symlinks {
		hdr := &tar.Header{
			Name:     name,
			Mode:     0o777,
			Linkname: target,
			Typeflag: tar.TypeSymlink,
		}
		if err := tw.WriteHeader(hdr); err != nil {
			t.Fatalf("write tar symlink header: %v", err)
		}
	}

	if err := tw.Close(); err != nil {
		t.Fatalf("close tar writer: %v", err)
	}
	if isGzip {
		if err := gw.Close(); err != nil {
			t.Fatalf("close gzip writer: %v", err)
		}
	}

	if err := os.WriteFile(targetPath, buf.Bytes(), 0o644); err != nil {
		t.Fatalf("write archive file: %v", err)
	}
}

func TestScanTarArchive(t *testing.T) {
	tempDir := t.TempDir()

	// 1. Plain uncompressed tar archive with a regular file and relative symlink
	plainTar := filepath.Join(tempDir, "plain.tar")
	createTarArchive(t, plainTar, map[string][]byte{
		"usr/bin/app": []byte("binary content"),
	}, map[string]string{
		"usr/bin/app_symlink": "app",
	}, false)

	cfs := newContainerFS()
	if err := scanTarArchive(plainTar, cfs); err != nil {
		t.Fatalf("scanTarArchive(plainTar) unexpected error: %v", err)
	}
	if _, exists := cfs.files["usr/bin/app"]; !exists {
		t.Errorf("expected usr/bin/app to be recorded in files")
	}
	if targets, exists := cfs.symlinks["usr/bin/app_symlink"]; !exists || len(targets) == 0 {
		t.Errorf("expected usr/bin/app_symlink to be recorded in symlinks")
	}

	// 2. Gzip-compressed tar archive
	gzipTar := filepath.Join(tempDir, "layer.tar.gz")
	createTarArchive(t, gzipTar, map[string][]byte{
		"etc/config.json": []byte("{}"),
	}, nil, true)

	cfsGzip := newContainerFS()
	if err := scanTarArchive(gzipTar, cfsGzip); err != nil {
		t.Fatalf("scanTarArchive(gzipTar) unexpected error: %v", err)
	}
	if _, exists := cfsGzip.files["etc/config.json"]; !exists {
		t.Errorf("expected etc/config.json to be recorded from gzip layer")
	}

	// 3. Non-tar file ignored gracefully (e.g. index.json or oci-layout)
	dummyFile := filepath.Join(tempDir, "oci-layout")
	if err := os.WriteFile(dummyFile, []byte(`{"imageLayoutVersion": "1.0.0"}`), 0o644); err != nil {
		t.Fatalf("write dummy file: %v", err)
	}
	cfsDummy := newContainerFS()
	if err := scanTarArchive(dummyFile, cfsDummy); err != nil {
		t.Fatalf("scanTarArchive(dummyFile) should safely ignore non-tar files, got error: %v", err)
	}

	// 4. Truncated tar archive mid-stream fails fast
	truncatedTar := filepath.Join(tempDir, "truncated.tar")
	createTarArchive(t, truncatedTar, map[string][]byte{
		"file1.txt": bytes.Repeat([]byte("A"), 2000),
		"file2.txt": bytes.Repeat([]byte("B"), 2000),
	}, nil, false)
	rawBytes, err := os.ReadFile(truncatedTar)
	if err != nil {
		t.Fatalf("read truncatedTar: %v", err)
	}
	if len(rawBytes) > 1000 {
		rawBytes = rawBytes[:1000]
	}
	if err := os.WriteFile(truncatedTar, rawBytes, 0o644); err != nil {
		t.Fatalf("write truncatedTar: %v", err)
	}
	cfsTruncated := newContainerFS()
	if err := scanTarArchive(truncatedTar, cfsTruncated); err == nil {
		t.Fatalf("expected scanTarArchive to fail on truncated archive midway through reading")
	}
}

func TestRunFailsOnMissingLayer(t *testing.T) {
	var stderr bytes.Buffer
	err := run([]string{"-layers", "nonexistent_layer.tar"}, io.Discard, &stderr)
	if err == nil {
		t.Fatalf("expected run() to fail on missing layer")
	}
	if !strings.Contains(stderr.String(), "Error scanning layer") {
		t.Errorf("expected stderr to report error scanning layer, got: %s", stderr.String())
	}
}

func TestRunUnsupportedConfigFails(t *testing.T) {
	tempDir := t.TempDir()
	configFile := filepath.Join(tempDir, "config.yaml")
	configContent := `
fileExistenceTests:
- name: "test file"
  path: "/app/binary"
  shouldExist: true
commandTests:
- name: "test command"
  command: "echo hello"
`
	if err := os.WriteFile(configFile, []byte(configContent), 0o644); err != nil {
		t.Fatalf("write config file: %v", err)
	}

	layerFile := filepath.Join(tempDir, "layer.tar")
	createTarArchive(t, layerFile, map[string][]byte{
		"app/binary": []byte("bin"),
	}, nil, false)

	var stdout, stderr bytes.Buffer
	err := run([]string{
		"-configs", configFile,
		"-layers", layerFile,
		"-target", "//test:target",
	}, &stdout, &stderr)
	if err == nil {
		t.Fatalf("expected run() to fail due to unsupported test types, but got nil")
	}
	if !strings.Contains(err.Error(), "contains unsupported test types") {
		t.Errorf("expected error to mention unsupported test types, got: %v", err)
	}
}

func TestCheckFileExistenceUnresolvedSymlink(t *testing.T) {
	cfs := newContainerFS()
	cfs.symlinks["app/bin/runner"] = []string{"usr/local/lib/python3.10/site-packages/runner"}

	trueVal := true
	err := checkFileExistence(fileExistenceTest{
		Path:        "/app/bin/runner",
		ShouldExist: &trueVal,
	}, cfs)
	if err == nil {
		t.Fatalf("expected error for unresolved symlink")
	}
	if !strings.Contains(err.Error(), "was resolved as a symlink pointing to") {
		t.Errorf("expected error to mention that path is a symlink with missing destination, got: %v", err)
	}
}

func TestRunFailureOutputsDiagnosticHint(t *testing.T) {
	tempDir := t.TempDir()
	configFile := filepath.Join(tempDir, "config.yaml")
	configContent := `
fileExistenceTests:
- name: "missing binary test"
  path: "/usr/local/bin/gunicorn"
  shouldExist: true
`
	if err := os.WriteFile(configFile, []byte(configContent), 0o644); err != nil {
		t.Fatalf("write config file: %v", err)
	}

	layerFile := filepath.Join(tempDir, "app_layer.tar")
	createTarArchive(t, layerFile, map[string][]byte{
		"app/binary": []byte("bin"),
	}, nil, false)

	var stdout, stderr bytes.Buffer
	err := run([]string{
		"-configs", configFile,
		"-layers", layerFile,
		"-target", "//my/package:my_image_test",
	}, &stdout, &stderr)
	if err == nil {
		t.Fatalf("expected run() to fail for missing file")
	}
	errOutput := stderr.String()
	if !strings.Contains(errOutput, "Scanned layer(s)") {
		t.Errorf("expected stderr to include scanned layers list, got: %s", errOutput)
	}
	if !strings.Contains(errOutput, "Hint: This test ran with layer streaming") {
		t.Errorf("expected stderr to include layer streaming diagnostic hint, got: %s", errOutput)
	}
	if !strings.Contains(errOutput, "test_layers = []") {
		t.Errorf("expected stderr to mention test_layers = [] opt-out, got: %s", errOutput)
	}
}
