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

// Package gzffile provides utils for GZF files.
package gzffile

import (
	"archive/zip"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strconv"

	"google.golang.org/protobuf/proto"

	sopb "intrinsic/scene/proto/v1/scene_object_go_proto"
)

// ExtractSceneObject unpacks a GZF file into the specified directory.
//
// The geometry files are extracted and named by their hex hashes.
// The SceneObject proto is returned.
func ExtractSceneObject(filename string, dir string) (*sopb.SceneObject, error) {
	r, err := zip.OpenReader(filename)
	if err != nil {
		return nil, fmt.Errorf("failed to open zip reader for %q: %w", filename, err)
	}
	defer r.Close()

	return extractSceneObject(&r.Reader, dir)
}

func extractSceneObject(zipReader *zip.Reader, dir string) (*sopb.SceneObject, error) {
	zipMap := make(map[string]*zip.File)
	for _, f := range zipReader.File {
		zipMap[f.Name] = f
	}

	// Extract and parse the SceneObject manifest.
	soFile, ok := zipMap["SOBJ/0"]
	if !ok {
		return nil, fmt.Errorf("SOBJ/0 not found in GZF")
	}

	rc, err := soFile.Open()
	if err != nil {
		return nil, fmt.Errorf("failed to open SOBJ/0: %w", err)
	}
	defer rc.Close()

	soBytes, err := io.ReadAll(rc)
	if err != nil {
		return nil, fmt.Errorf("failed to read SOBJ/0: %w", err)
	}

	so := &sopb.SceneObject{}
	if err := proto.Unmarshal(soBytes, so); err != nil {
		return nil, fmt.Errorf("failed to unmarshal SceneObject proto: %w", err)
	}

	// Find the geometry refs.
	v0Exact, v0Renderable, v1Exact, v1Renderable := findGeometryRefs(so)

	// Extract V1 geometries.
	for _, ref := range v1Exact {
		decKey, err := hexToDec(ref)
		if err != nil {
			return nil, fmt.Errorf("failed to convert hex %q to dec: %w", ref, err)
		}
		err = extractZipEntry(zipMap, "GEOM/"+decKey, filepath.Join(dir, ref))
		if err != nil {
			return nil, fmt.Errorf("failed to extract GEOM chunk for %q: %w", ref, err)
		}
	}
	for _, ref := range v1Renderable {
		decKey, err := hexToDec(ref)
		if err != nil {
			return nil, fmt.Errorf("failed to convert hex %q to dec: %w", ref, err)
		}
		err = extractZipEntry(zipMap, "GLTF/"+decKey, filepath.Join(dir, ref))
		if err != nil {
			return nil, fmt.Errorf("failed to extract GLTF chunk for %q: %w", ref, err)
		}
	}

	// Extract V0 geometries.
	for _, ref := range v0Exact {
		err = extractZipEntry(zipMap, "EXPO/"+ref, filepath.Join(dir, ref))
		if err != nil {
			return nil, fmt.Errorf("failed to extract EXPO chunk for %q: %w", ref, err)
		}
	}
	for _, ref := range v0Renderable {
		err = extractZipEntry(zipMap, "EXPO/"+ref, filepath.Join(dir, ref))
		if err != nil {
			return nil, fmt.Errorf("failed to extract EXPO chunk for %q: %w", ref, err)
		}
	}

	return so, nil
}

func findGeometryRefs(so *sopb.SceneObject) (v0Exact, v0Renderable, v1Exact, v1Renderable []string) {
	for _, entity := range so.GetEntities() {
		if entity.GetLink() == nil || entity.GetLink().GetGeometryComponent() == nil {
			continue
		}
		gc := entity.GetLink().GetGeometryComponent()
		for _, gcSet := range gc.GetNamedGeometries() {
			// V0 geometries
			for _, g := range gcSet.GetGeometries() {
				refs := g.GetGeometryStorageRefs()
				if refs != nil {
					if refs.GetFingerprint() != "" {
						v0Exact = append(v0Exact, refs.GetFingerprint())
					}
					if refs.GetGeometryRef() != "" {
						v0Exact = append(v0Exact, refs.GetGeometryRef())
					}
					if refs.GetRenderableRef() != "" {
						v0Renderable = append(v0Renderable, refs.GetRenderableRef())
					}
				}
			}
			// V1 geometries
			for _, tg := range gcSet.GetNamedGeometries() {
				g := tg.GetGeometry()
				if g == nil {
					continue
				}
				if g.GetGeoRef() != nil {
					refs := g.GetGeoRef()
					if refs.GetExactGeometryRef() != "" {
						v1Exact = append(v1Exact, refs.GetExactGeometryRef())
					}
					if refs.GetRenderableRef() != "" {
						v1Renderable = append(v1Renderable, refs.GetRenderableRef())
					}
				}
			}
		}
	}
	return unique(v0Exact), unique(v0Renderable), unique(v1Exact), unique(v1Renderable)
}

func unique(slice []string) []string {
	keys := make(map[string]bool)
	list := []string{}
	for _, entry := range slice {
		if _, value := keys[entry]; !value {
			keys[entry] = true
			list = append(list, entry)
		}
	}
	return list
}

func hexToDec(hexStr string) (string, error) {
	if hexStr == "" {
		return "", fmt.Errorf("empty hex string")
	}
	var val uint64
	_, err := fmt.Sscanf(hexStr, "%x", &val)
	if err != nil {
		return "", err
	}
	return strconv.FormatUint(val, 10), nil
}

func extractZipEntry(zipMap map[string]*zip.File, zipPath string, destPath string) error {
	targetFile, ok := zipMap[zipPath]
	if !ok {
		return fmt.Errorf("zip entry %q not found", zipPath)
	}

	rc, err := targetFile.Open()
	if err != nil {
		return err
	}
	defer rc.Close()

	out, err := os.Create(destPath)
	if err != nil {
		return err
	}
	defer out.Close() // Safety fallback.

	_, err = io.Copy(out, rc)
	if err != nil {
		return err
	}
	return out.Close() // Explicit close to catch write errors.
}
