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

package any

import (
	"context"
	"log/slog"
	"math/rand/v2"
	"sync"
	"time"

	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
)

const (
	// Update cached data regularly for Solution Builders who are sideloading
	// assets with new versions of proto messages as they build a solution.
	cacheRefreshMinInterval = 30 * time.Second
	cacheRefreshMaxInterval = 45 * time.Second
)

func cacheRefreshInterval() time.Duration {
	return cacheRefreshMinInterval + rand.N(cacheRefreshMaxInterval-cacheRefreshMinInterval)
}

// AnyResolver implements the MessageTypeResolver and ExtensionTypeResolver interfaces.
type AnyResolver struct {
	greedyResolver *GreedyResolver
	iaResolver     *InstalledAssetsResolver
	cancel         context.CancelFunc
	wg             sync.WaitGroup
}

// NewAnyResolver creates and returns a new instance of AnyResolver.
func NewAnyResolver(protoRegistryAddress string, installedAssetsAddress string) (*AnyResolver, error) {
	protoRegistryResolver, err := NewProtoRegistryResolver(protoRegistryAddress)
	if err != nil {
		return nil, err
	}
	installedAssetsResolver, err := NewInstalledAssetsResolver(installedAssetsAddress)
	if err != nil {
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())

	r := &AnyResolver{
		greedyResolver: NewGreedyResolver([]Resolver{
			// Try the ProtoRegistry resolver first because it has a unique type url prefix,
			// and it returns NotFound immediately if the URL lacks that prefix.
			protoRegistryResolver,
			// Try InstalledAssets service next because many Any types in our APIs are
			// meant to hold parameters and configs for installed assets.
			installedAssetsResolver,
			// Last result, see if it is already in our global types
			protoregistry.GlobalTypes,
		}),
		iaResolver: installedAssetsResolver,
		cancel:     cancel,
	}

	r.wg.Add(1)
	go r.backgroundLoop(ctx)

	return r, nil
}

func (a *AnyResolver) Close() {
	a.cancel()
	a.wg.Wait()
}

func (a *AnyResolver) backgroundLoop(ctx context.Context) {
	defer a.wg.Done()
	timer := time.NewTimer(cacheRefreshInterval())
	defer timer.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-timer.C:
			if err := a.iaResolver.RefreshInstalledAssets(); err != nil {
				slog.ErrorContext(ctx, "Failed to refresh installed assets", "err", err)
			}
			timer.Reset(cacheRefreshInterval())
		}
	}
}

// FindExtensionByName looks up a extension field by the field's full name.
// Note that this is the full name of the field as determined by
// where the extension is declared and is unrelated to the full name of the
// message being extended.
//
// This returns (nil, NotFound) if not found.
func (a *AnyResolver) FindExtensionByName(field protoreflect.FullName) (protoreflect.ExtensionType, error) {
	return a.greedyResolver.FindExtensionByName(field)
}

// FindExtensionByNumber looks up a extension field by the field number
// within some parent message, identified by full name.
//
// This returns (nil, NotFound) if not found.
func (a *AnyResolver) FindExtensionByNumber(message protoreflect.FullName, field protoreflect.FieldNumber) (protoreflect.ExtensionType, error) {
	return a.greedyResolver.FindExtensionByNumber(message, field)
}

// FindMessageByName looks up a message by its full name.
// E.g., "google.protobuf.Any"
//
// This return (nil, NotFound) if not found.
func (a *AnyResolver) FindMessageByName(message protoreflect.FullName) (protoreflect.MessageType, error) {
	return a.greedyResolver.FindMessageByName(message)
}

// FindMessageByURL looks up a message by a URL identifier.
// See documentation on google.protobuf.Any.type_url for the URL format.
//
// This returns (nil, NotFound) if not found.
func (a *AnyResolver) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	return a.greedyResolver.FindMessageByURL(url)
}
