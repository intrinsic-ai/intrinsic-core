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

package auth

import (
	"fmt"
	"net/http"
	"sync"
)

var (
	tsFactoryInit sync.Once
	tsFactory     *sharedTokenSourceFactory
)

func getSharedTokenSourceFactory() *sharedTokenSourceFactory {
	tsFactoryInit.Do(func() {
		tsFactory = &sharedTokenSourceFactory{
			// TODO(csieber): Align on customer user agent and tracing.
			c:   http.DefaultClient,
			tsm: make(map[string]*cachedTokenSource),
		}
	})
	return tsFactory
}

// sharedTokenSourceFactory is a factory for cached token sources.
type sharedTokenSourceFactory struct {
	c     *http.Client
	tsm   map[string]*cachedTokenSource
	smMtx sync.Mutex
}

// cacheKey returns a cache key for the given token service address and API key.
// We re-use a token source if it is based on the same accounts token exchange service address and
// the same API key.
func cacheKey(fsAddr, apiKey string) string {
	return fmt.Sprintf("%s-%s", fsAddr, apiKey)
}

// LoadOrNew returns a cached token source for the given API key and token service address.
// It is thread-safe and can be used by multiple goroutines.
func (s *sharedTokenSourceFactory) LoadOrNew(fsAddr, apiKey string) (*cachedTokenSource, error) {
	k := cacheKey(fsAddr, apiKey)
	// lock the token source map
	s.smMtx.Lock()
	defer s.smMtx.Unlock()
	// lookup the token source for the given API key
	ts, ok := s.tsm[k]
	if ok {
		return ts, nil
	}
	// if there is no token source for the given address and API key, create one and store it
	tsc, err := NewTokensServiceClient(s.c, fsAddr)
	if err != nil {
		return nil, fmt.Errorf("cannot create token exchange: %w", err)
	}
	nts := cachedTokenSource{
		tp:               tsc,
		apiKey:           apiKey,
		minTokenLifetime: defaultMinTokenLifetime,
	}
	s.tsm[k] = &nts
	return &nts, nil
}
