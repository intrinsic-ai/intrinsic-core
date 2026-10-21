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

// Package references provides utilities for managing referenced Assets.
package references

import (
	"context"
	"errors"
	"fmt"
	"maps"
	"slices"

	"intrinsic/assets/conversion/runtime"
	"intrinsic/assets/dependencies/traversal"
	"intrinsic/assets/idutils"
	"intrinsic/assets/install/gather"
	"intrinsic/assets/install/localconv"
	"intrinsic/assets/orderedmap"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	dependencypb "intrinsic/assets/proto/v1/dependency_go_proto"
	processedassetpb "intrinsic/assets/proto/v1/processed_asset_go_proto"
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
)

var (
	// ErrInvalidDependencyResolution is returned when a referenced Asset specified as a
	// `ResolvedDependency` value in the default configuration of an Asset does not provide the
	// required interfaces that the field expects.
	ErrInvalidDependencyResolution = errors.New("required interfaces not provided")
)

type assetInfo struct {
	asset   *assetpb.Asset
	runtime *rtrpb.ResourceTypeRuntime
}

func assetID(a *assetpb.Asset) (string, error) {
	switch v := a.GetSource().(type) {
	case *assetpb.Asset_Catalog:
		return idutils.IDFromProtoUnchecked(v.Catalog.GetIdVersion().GetId()), nil
	case *assetpb.Asset_Local:
		switch v2 := v.Local.GetVariant().(type) {
		case *processedassetpb.ProcessedAsset_Data:
			return idutils.IDFromProtoUnchecked(v2.Data.GetMetadata().GetIdVersion().GetId()), nil
		case *processedassetpb.ProcessedAsset_HardwareDevice:
			return idutils.IDFromProtoUnchecked(v2.HardwareDevice.GetMetadata().GetId()), nil
		case *processedassetpb.ProcessedAsset_Process:
			return idutils.IDFromProtoUnchecked(v2.Process.GetMetadata().GetIdVersion().GetId()), nil
		case *processedassetpb.ProcessedAsset_SceneObject:
			return idutils.IDFromProtoUnchecked(v2.SceneObject.GetMetadata().GetId()), nil
		case *processedassetpb.ProcessedAsset_Service:
			return idutils.IDFromProtoUnchecked(v2.Service.GetMetadata().GetId()), nil
		case *processedassetpb.ProcessedAsset_Skill:
			return idutils.IDFromProtoUnchecked(v2.Skill.GetMetadata().GetId()), nil
		default:
			return "", fmt.Errorf("unsupported processed asset variant: %T", v2)
		}
	default:
		return "", fmt.Errorf("unsupported asset source: %T", v)
	}
}

// collectUniqueReferencedAssets collects all referenced Assets from the given slice of
// `items` ResourceTypeRuntimes and returns a map of unique references.
//
// If multiple ResourceTypeRuntimes reference the same Asset, the first encountered
// reference is returned in the result.
func collectUniqueReferencedAssets(items []*rtrpb.ResourceTypeRuntime) (map[string]*assetpb.Asset, error) {
	unique := make(map[string]*assetpb.Asset)

	for _, rtr := range items {
		if len(rtr.GetReferencedAssets()) == 0 {
			continue
		}
		refAssets := rtr.GetReferencedAssets()
		for _, refID := range slices.Sorted(maps.Keys(refAssets)) {
			ref := refAssets[refID]
			id, err := assetID(ref)
			if err != nil {
				return nil, fmt.Errorf("failed to get IDs of referenced Assets for %v: %v", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()), err)
			}
			if _, seen := unique[id]; seen {
				continue
			}
			unique[id] = ref
		}
	}
	return unique, nil
}

// referencedAssetInfos returns a map of referenced Asset IDs to their
// corresponding `assetInfo` (Asset and ResourceTypeRuntime).
//
// It gathers referenced Assets from the Asset Catalog and converts Local Assets
// to ResourceTypeRuntimes.
func referencedAssetInfos(ctx context.Context, aciClient acigrpcpb.AssetCatalogInternalClient, uniqueReferenced map[string]*assetpb.Asset) (map[string]assetInfo, error) {
	result := make(map[string]assetInfo)
	var pullFromCatalog []*idpb.IdVersion
	catalogLookup := make(map[string]*assetpb.Asset)

	for _, id := range slices.Sorted(maps.Keys(uniqueReferenced)) {
		a := uniqueReferenced[id]
		switch v := a.GetSource().(type) {
		case *assetpb.Asset_Catalog:
			pullFromCatalog = append(pullFromCatalog, v.Catalog.GetIdVersion())
			idv := idutils.IDVersionFromProtoUnchecked(v.Catalog.GetIdVersion())
			catalogLookup[idv] = a
		case *assetpb.Asset_Local:
			rtr, err := localconv.ProcessedAssetToRuntime(ctx, v.Local, runtime.WithACIClient(aciClient))
			if err != nil {
				return nil, fmt.Errorf("failed to convert Asset to runtime %q: %v", id, err)
			}
			result[id] = assetInfo{asset: a, runtime: rtr}
		default:
			return nil, fmt.Errorf("unsupported asset source: %T", v)
		}
	}

	if len(pullFromCatalog) == 0 {
		return result, nil
	}

	catalogGatherer := gather.FromAssetCatalog(aciClient, gather.WithSkipUnavailable(true))
	rtrs, _, err := catalogGatherer(ctx, pullFromCatalog)
	if err != nil {
		return result, fmt.Errorf("failed to gather referenced assets from catalog: %v", err)
	}

	for _, rtr := range rtrs {
		idv := idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())
		if a, ok := catalogLookup[idv]; ok {
			id, err := assetID(a)
			if err != nil {
				return nil, fmt.Errorf("failed to get asset ID for %q: %v", idv, err)
			}
			result[id] = assetInfo{asset: a, runtime: rtr}
		}
	}
	return result, nil
}

// AddToInstall identifies Assets referenced by those already in `toInstall`
// and adds them to the map for installation later. It also returns a map of
// referenced Assets found in the updated `toInstall` that did not exist in the
// initial collection.
//
// Assets are added for installation if:
//  1. They are not already present in `toInstall` (if they are, they will be
//     installed anyway).
//  2. They are referenced in the original Asset's default configuration (or for
//     Process Assets, in `ReferencedAssets`).
//
// If multiple referenced Assets with the same ID but different versions or definitions are found,
// the first encountered referenced version for that Asset ID takes precedence.
//
// Reference resolution is recursive and will include Assets referenced by
// referenced Assets (and so on).
func AddToInstall(ctx context.Context, toInstall *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime], aciClient acigrpcpb.AssetCatalogInternalClient) (map[string]*rtrpb.ResourceTypeRuntime, error) {
	referenced := make(map[string]*rtrpb.ResourceTypeRuntime)
	refAssetInfos := make(map[string]assetInfo)

	var referencing []*rtrpb.ResourceTypeRuntime
	for _, rtr := range toInstall.Items() {
		if len(rtr.GetReferencedAssets()) > 0 {
			referencing = append(referencing, rtr)
		}
	}

	for len(referencing) > 0 {
		uniqueReferenced, err := collectUniqueReferencedAssets(referencing)
		if err != nil {
			return nil, err
		}
		missingRefAssetInfos := make(map[string]*assetpb.Asset)
		for id, a := range uniqueReferenced {
			if _, ok := refAssetInfos[id]; !ok {
				missingRefAssetInfos[id] = a
			}
		}
		if len(missingRefAssetInfos) > 0 {
			infos, err := referencedAssetInfos(ctx, aciClient, missingRefAssetInfos)
			if err != nil {
				return nil, err
			}
			maps.Copy(refAssetInfos, infos)
		}

		var nextReferencing []*rtrpb.ResourceTypeRuntime
		for _, sourceRtr := range referencing {
			sourceID := idutils.IDFromProtoUnchecked(sourceRtr.GetMetadata().GetIdVersion().GetId())
			if len(sourceRtr.GetReferencedAssets()) == 0 {
				continue
			}

			var runtimes []*rtrpb.ResourceTypeRuntime
			refAssets := sourceRtr.GetReferencedAssets()
			for _, k := range slices.Sorted(maps.Keys(refAssets)) {
				asset := refAssets[k]
				aID, err := assetID(asset)
				if err != nil {
					return nil, fmt.Errorf("failed to get IDs of referenced Assets for %v: %v", sourceID, err)
				}
				// Almost all referenced assets IDs, `aID` should already be present in refAssetInfos since
				// the map was constructed from the unique referenced assets. However, catalog references of
				// Assets are gathered with the `SkipUnavailable` option set to true, and these missing
				// or unavailable (due to lack of permissions) Assets are not added to refAssetInfos.
				if info, ok := refAssetInfos[aID]; ok {
					runtimes = append(runtimes, info.runtime)
				}
			}

			// Filter out Assets that are referenced in `ResourceTypeRuntime.ReferencedAssets` but are not
			// present in the original default configuration. For Process Assets, which do not have a default
			// configuration, retain all referenced Assets.
			filtered, err := referencedInAssetDefinition(sourceRtr, runtimes)
			if err != nil {
				return nil, err
			}

			for _, refRtr := range filtered {
				refID := idutils.IDFromProtoUnchecked(refRtr.GetMetadata().GetIdVersion().GetId())
				// Skip if the Asset is already present in toInstall.
				if _, present := toInstall.Get(refID); present {
					continue
				}

				referenced[refID] = refRtr
				// Note that we are adding Asset references to the ordered map after the original Assets in
				// `toInstall`. This is because the original Assets are installed first, and then the
				// referenced Assets are installed.
				//
				// This violates the expected behavior that multiple `CreateInstalledAsset` requests in series
				// return the same state of the solution as a batch request with `CreateInstalledAssets`.
				toInstall.Set(refID, refRtr)
				if len(refRtr.GetReferencedAssets()) > 0 {
					nextReferencing = append(nextReferencing, refRtr)
				}
			}
		}
		referencing = nextReferencing
	}

	return referenced, nil
}

// referencedInAssetDefinition returns a list of Assets from `referenced` that
// are referenced as a ResolvedDependency in the default configuration of
// `referencedBy`. Returns all referenced Assets for Process Assets, since they
// do not have a default configuration.
func referencedInAssetDefinition(referencedBy *rtrpb.ResourceTypeRuntime, referenced []*rtrpb.ResourceTypeRuntime) ([]*rtrpb.ResourceTypeRuntime, error) {
	if referencedBy.GetMetadata().GetAssetType() == atpb.AssetType_ASSET_TYPE_PROCESS {
		return referenced, nil
	}
	defaultConfig := referencedBy.GetDefaultConfiguration()
	if defaultConfig == nil {
		return nil, nil
	}
	fds := referencedBy.GetMetadata().GetFileDescriptorSet()
	if fds == nil {
		return nil, nil
	}

	seen := make(map[string]bool)
	allRefs := make(map[string]*rtrpb.ResourceTypeRuntime)
	var found []*rtrpb.ResourceTypeRuntime
	for _, asset := range referenced {
		allRefs[idutils.IDFromProtoUnchecked(asset.GetMetadata().GetIdVersion().GetId())] = asset
	}

	if err := traversal.ForEachResolvedDependency(defaultConfig, fds, func(annotations *dependencypb.Dependency, msg *rdpb.ResolvedDependency, _ traversal.FieldContext) (*rdpb.ResolvedDependency, error) {
		if msg == nil {
			return nil, nil
		}
		name := msg.GetName()
		if name == "" {
			return msg, nil
		}
		requires := annotations.GetRequires()

		// Verify that the referenced asset provides the required interfaces, and
		// therefore, is a valid dependency.
		//
		// Note: This check implicitly assumes that the referenced Asset is a Data Asset,
		// since `allRefs` is keyed by the Asset ID, which is only true for Data Assets.
		// For gRPC interfaces the `name` field of the `ResolvedDependency` are not
		// of the Asset ID format, so they will not be found in `allRefs`.
		if rtr, ok := allRefs[name]; ok {
			provides := rtr.GetMetadata().GetProvides()
			var providesURIs []string
			for _, p := range provides {
				providesURIs = append(providesURIs, p.GetUri())
			}
			var missing bool
			for _, iface := range requires {
				if !slices.Contains(providesURIs, iface) {
					missing = true
					break
				}
			}
			if missing {
				return nil, fmt.Errorf("referenced Asset %q does not provide the required interfaces: %w", name, ErrInvalidDependencyResolution)
			}
			if !seen[name] {
				seen[name] = true
				found = append(found, rtr)
			}
		}
		return msg, nil
	}); err != nil {
		return nil, err
	}
	return found, nil
}
