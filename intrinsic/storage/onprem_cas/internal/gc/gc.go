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

// Package gc provides garbage collection utilities for local CAS.
package gc

import (
	"context"
	"log/slog"
	"os"
	"path/filepath"
	"time"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	"intrinsic/storage/onprem_cas/internal"
)

// IdentifyPartialFiles reads the directory and returns a list of files with .partial extension.
// This should be called before starting the server to identify stale partial files.
func IdentifyPartialFiles(dir string) ([]string, error) {
	files, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var partials []string
	for _, entry := range files {
		if entry.Type().IsRegular() && filepath.Ext(entry.Name()) == internal.PartialExt {
			partials = append(partials, filepath.Join(dir, entry.Name()))
		}
	}
	return partials, nil
}

// IdentifyCorruptObjects scans the objects directory and returns paths of all files
// whose size does not match the expectations for their digest (filename), e.g. 0-byte files for non-empty digests.
func IdentifyCorruptObjects(dir string) ([]string, error) {
	files, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var corrupt []string
	for _, entry := range files {
		if !entry.Type().IsRegular() {
			continue
		}
		digest := entry.Name()
		if err := idhandling.ValidateDigest(digest); err != nil {
			continue
		}
		info, err := entry.Info()
		if err != nil {
			slog.Warn("GC: Failed to get file info for object; skipping", slog.String("file", digest), slog.Any("error", err))
			continue
		}
		if err := idhandling.ValidateObjectSize(digest, info.Size()); err != nil {
			slog.Warn("GC: Corrupt object identified", slog.String("file", digest), slog.Int64("size", info.Size()), slog.Any("error", err))
			corrupt = append(corrupt, filepath.Join(dir, digest))
		}
	}
	return corrupt, nil
}

// IdentifyInactiveObjects scans the objects directory and returns paths of all files
// whose digest (filename) is not present in activeObjects and whose age exceeds gracePeriod.
// If gracePeriod > 0, files created/modified within the grace period (age <= gracePeriod) are not returned.
func IdentifyInactiveObjects(dir string, activeObjects map[string]struct{}, gracePeriod time.Duration) ([]string, error) {
	files, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var inactives []string
	now := time.Now()
	for _, entry := range files {
		if !entry.Type().IsRegular() {
			continue
		}
		digest := entry.Name()
		if _, isActive := activeObjects[digest]; isActive {
			continue
		}
		if gracePeriod > 0 {
			info, err := entry.Info()
			if err != nil {
				slog.Warn("GC: Failed to get file info for inactive object; skipping", slog.String("file", digest), slog.Any("error", err))
				continue
			}
			if now.Sub(info.ModTime()) <= gracePeriod {
				slog.Debug("GC: Inactive object within grace period; skipping deletion", slog.String("file", digest), slog.Duration("age", now.Sub(info.ModTime())), slog.Duration("grace_period", gracePeriod))
				continue
			}
		}
		inactives = append(inactives, filepath.Join(dir, digest))
	}
	return inactives, nil
}

// DeleteFiles deletes the given list of files.
func DeleteFiles(ctx context.Context, files []string) error {
	if err := ctx.Err(); err != nil {
		slog.Warn("GC: Deletion cancelled before starting", slog.Any("error", err))
		return ctx.Err()
	}

	deletedCount := 0
	for _, path := range files {
		select {
		case <-ctx.Done():
			slog.Warn("GC: Deletion cancelled", slog.Any("error", ctx.Err()))
			return ctx.Err()
		default:
		}

		if err := os.Remove(path); err != nil {
			if !os.IsNotExist(err) {
				slog.Warn("GC: Failed to delete file", slog.String("path", path), slog.Any("error", err))
			}
		} else {
			slog.Info("GC: Deleted stale file", slog.String("path", path))
			deletedCount++
		}
	}
	slog.Info("GC: Deletion finished", slog.Int("deleted_count", deletedCount))
	return nil
}
