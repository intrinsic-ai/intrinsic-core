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

package transfer

import (
	"encoding/json"
	"fmt"
	"log/slog"
	"os"
	"path/filepath"
	"sync"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"

	lru "github.com/hashicorp/golang-lru/v2"
)

// upstreamStat holds cached metadata for objects known to exist upstream.
type upstreamStat struct {
	Size uint64 `json:"size"`
}

// upstreamCache is a file + LRU based cache for storing upstream stat results.
// It assumes that objects are never deleted from upstream; therefore, once an object's
// existence and size are cached (either in memory or on disk), they remain valid indefinitely.
type upstreamCache struct {
	mu       sync.Mutex
	lruCache *lru.Cache[Digest, *upstreamStat]
	cacheDir string
}

// newUpstreamCache creates a new upstreamCache with the specified LRU capacity and file directory.
func newUpstreamCache(size int, cacheDir string) (*upstreamCache, error) {
	cache, err := lru.New[Digest, *upstreamStat](size)
	if err != nil {
		return nil, err
	}
	if cacheDir != "" {
		if err := os.MkdirAll(cacheDir, 0755); err != nil {
			return nil, fmt.Errorf("failed to create stat cache dir %q: %w", cacheDir, err)
		}
	}
	return &upstreamCache{
		lruCache: cache,
		cacheDir: cacheDir,
	}, nil
}

// statCacheFilePath returns the file path for caching stat results of a given digest in cacheDir.
func statCacheFilePath(cacheDir string, d Digest) string {
	return filepath.Join(cacheDir, string(d)+".stat.json")
}

// Get retrieves the cached upstream stat result for the given digest.
// It first checks the in-memory LRU cache. If not found there, it checks the file on disk.
// If the file exists, it unmarshals the result and adds it to the in-memory cache before returning.
func (c *upstreamCache) Get(d Digest) (*upstreamStat, bool) {
	// 1. Check in-memory LRU cache
	if val, ok := c.lruCache.Get(d); ok {
		if err := idhandling.ValidateObjectSize(string(d), int64(val.Size)); err != nil {
			slog.Warn("Transfer: invalid cached stat size in LRU, removing", slog.String("digest", string(d)), slog.Uint64("size", val.Size), slog.Any("error", err))
			c.lruCache.Remove(d)
			if c.cacheDir != "" {
				filePath := statCacheFilePath(c.cacheDir, d)
				_ = os.Remove(filePath)
			}
			return nil, false
		}
		return val, true
	}

	if c.cacheDir == "" {
		return nil, false
	}

	// 2. Check disk file cache
	filePath := statCacheFilePath(c.cacheDir, d)
	data, err := os.ReadFile(filePath)
	if err != nil {
		return nil, false
	}

	var stat upstreamStat
	if err := json.Unmarshal(data, &stat); err != nil {
		slog.Warn("Transfer: corrupt stat cache file encountered, attempting deletion", slog.String("path", filePath), slog.Any("error", err))
		if delErr := os.Remove(filePath); delErr != nil && !os.IsNotExist(delErr) {
			slog.Warn("Transfer: failed to delete corrupt stat cache file", slog.String("path", filePath), slog.Any("error", delErr))
		}
		return nil, false
	}

	if err := idhandling.ValidateObjectSize(string(d), int64(stat.Size)); err != nil {
		slog.Warn("Transfer: invalid cached stat size in file, removing", slog.String("path", filePath), slog.Uint64("size", stat.Size), slog.Any("error", err))
		if delErr := os.Remove(filePath); delErr != nil && !os.IsNotExist(delErr) {
			slog.Warn("Transfer: failed to delete invalid stat cache file", slog.String("path", filePath), slog.Any("error", delErr))
		}
		return nil, false
	}

	// 3. Add to in-memory LRU cache
	c.lruCache.Add(d, &stat)
	return &stat, true
}

// Add stores the upstream stat result in both the in-memory LRU cache and as a JSON file on disk.
func (c *upstreamCache) Add(d Digest, val *upstreamStat) {
	if val == nil || idhandling.ValidateObjectSize(string(d), int64(val.Size)) != nil {
		slog.Warn("Transfer: refusing to cache invalid stat size", slog.String("digest", string(d)), slog.Any("val", val))
		return
	}

	c.lruCache.Add(d, val)

	if c.cacheDir == "" {
		return
	}

	filePath := statCacheFilePath(c.cacheDir, d)
	tempPath := filePath + ".tmp"
	data, err := json.Marshal(val)
	if err != nil {
		slog.Error("Transfer: failed to marshal stat cache", slog.String("digest", string(d)), slog.Any("error", err))
		return
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	if err := os.WriteFile(tempPath, data, 0644); err != nil {
		slog.Error("Transfer: failed to write temp stat cache file", slog.String("path", tempPath), slog.Any("error", err))
		return
	}
	if err := os.Rename(tempPath, filePath); err != nil {
		os.Remove(tempPath)
		slog.Error("Transfer: failed to rename stat cache file", slog.String("path", filePath), slog.Any("error", err))
	}
}
