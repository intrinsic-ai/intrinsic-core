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
	"errors"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	"intrinsic/storage/onprem_cas/internal"
)

// IdentifyPartialFiles reads the directory and returns a list of files with .partial extension.
// This should be called before starting the server to identify stale partial files.
//
// Subdirectories are not scanned on purpose: resumable upload sessions in
// internal.SessionsSubdir must survive a restart and are cleaned up by IdentifyExpiredSessions.
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

// sessionID returns the ID of the resumable upload session that the file belongs to, or "" if the
// file doesn't belong to a session.
func sessionID(name string) string {
	name = strings.TrimSuffix(name, internal.SessionTmpExt)
	for _, ext := range []string{internal.SessionMetadataExt, internal.PartialExt} {
		if id, ok := strings.CutSuffix(name, ext); ok && id != "" {
			return id
		}
	}
	return ""
}

// SessionFiles returns the paths of all files of the resumable upload session id in dir and the
// time of the last activity, i.e. the newest modification time of these files.
func SessionFiles(dir, id string) ([]string, time.Time, error) {
	var paths []string
	var lastActivity time.Time
	for _, name := range []string{
		id + internal.PartialExt,
		id + internal.SessionMetadataExt,
		id + internal.SessionMetadataExt + internal.SessionTmpExt,
	} {
		path := filepath.Join(dir, name)
		info, err := os.Stat(path)
		if errors.Is(err, fs.ErrNotExist) {
			continue
		}
		if err != nil {
			return nil, time.Time{}, err
		}
		paths = append(paths, path)
		if info.ModTime().After(lastActivity) {
			lastActivity = info.ModTime()
		}
	}
	return paths, lastActivity, nil
}

// IdentifyExpiredSessions scans the sessions directory and returns the IDs of resumable upload
// sessions whose last activity (the newest modification time of their files) is older than ttl.
// A missing directory has no sessions.
func IdentifyExpiredSessions(dir string, ttl time.Duration) ([]string, error) {
	files, err := os.ReadDir(dir)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	lastActivity := make(map[string]time.Time)
	for _, entry := range files {
		if !entry.Type().IsRegular() {
			continue
		}
		id := sessionID(entry.Name())
		if id == "" {
			continue
		}
		info, err := entry.Info()
		if err != nil {
			slog.Warn("GC: Failed to get file info for upload session file; skipping", slog.String("file", entry.Name()), slog.Any("error", err))
			continue
		}
		if t, ok := lastActivity[id]; !ok || info.ModTime().After(t) {
			lastActivity[id] = info.ModTime()
		}
	}
	var expired []string
	now := time.Now()
	for id, t := range lastActivity {
		if now.Sub(t) > ttl {
			expired = append(expired, id)
		}
	}
	slices.Sort(expired)
	return expired, nil
}

// SessionStore provides access to resumable upload sessions for garbage collection.
type SessionStore interface {
	// Dir returns the directory where session files are stored.
	Dir() string
	// TryAcquire locks the session id if no caller is currently working on it.
	TryAcquire(id string) (release func(), ok bool)
	// Remove deletes all files of the session id.
	Remove(id string)
}

// CleanupSessions deletes the upload sessions whose last activity is older than ttl, whether they
// were finalized or not. Sessions that another caller is working on are skipped.
func CleanupSessions(ctx context.Context, store SessionStore, ttl time.Duration) error {
	ids, err := IdentifyExpiredSessions(store.Dir(), ttl)
	if err != nil {
		return err
	}
	deleted := 0
	for _, id := range ids {
		if err := ctx.Err(); err != nil {
			return err
		}
		release, ok := store.TryAcquire(id)
		if !ok {
			continue
		}
		// Check again under the lock: a caller may have used the session after it was identified.
		files, lastActivity, err := SessionFiles(store.Dir(), id)
		if err == nil && len(files) > 0 && time.Since(lastActivity) > ttl {
			store.Remove(id)
			deleted++
			slog.Info("GC: Deleted expired upload session", slog.String("session", id), slog.Time("last_activity", lastActivity))
		}
		release()
	}
	if len(ids) > 0 {
		slog.Info("GC: Upload session cleanup finished", slog.Int("expired_count", len(ids)), slog.Int("deleted_count", deleted))
	}
	return nil
}

// RunSessionCleanup calls CleanupSessions right away and then every interval until ctx is done.
// ttl and interval must be positive.
func RunSessionCleanup(ctx context.Context, store SessionStore, ttl, interval time.Duration) {
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		if err := CleanupSessions(ctx, store, ttl); err != nil && ctx.Err() == nil {
			slog.Error("GC: Upload session cleanup failed", slog.Any("error", err))
		}
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
	}
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
