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

// Package typeutils provides utilities for AssetTypes.
package typeutils

import (
	"fmt"
	"strings"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
)

type assetTypeInfo struct {
	CodeName                  string
	DisplayName               string
	DisplayNamePlural         string
	HasFullViewDeploymentData bool
	HasInstances              bool
	HasObjects                bool
}

// LINT.IfChange(asset_type_info)
var allAssetTypeInfo = map[atypepb.AssetType]assetTypeInfo{
	atypepb.AssetType_ASSET_TYPE_UNSPECIFIED: {
		CodeName:                  "unspecified",
		DisplayName:               "Asset",
		DisplayNamePlural:         "Assets",
		HasFullViewDeploymentData: false,
		HasInstances:              false,
		HasObjects:                false,
	},
	atypepb.AssetType_ASSET_TYPE_SCENE_OBJECT: {
		CodeName:                  "scene_object",
		DisplayName:               "SceneObject",
		DisplayNamePlural:         "SceneObjects",
		HasFullViewDeploymentData: true,
		HasInstances:              true,
		HasObjects:                true,
	},
	atypepb.AssetType_ASSET_TYPE_SERVICE: {
		CodeName:                  "service",
		DisplayName:               "Service",
		DisplayNamePlural:         "Services",
		HasFullViewDeploymentData: false,
		HasInstances:              true,
		HasObjects:                false,
	},
	atypepb.AssetType_ASSET_TYPE_SKILL: {
		CodeName:                  "skill",
		DisplayName:               "Skill",
		DisplayNamePlural:         "Skills",
		HasFullViewDeploymentData: false,
		HasInstances:              false,
		HasObjects:                false,
	},
	atypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE: {
		CodeName:                  "hardware_device",
		DisplayName:               "HardwareDevice",
		DisplayNamePlural:         "HardwareDevices",
		HasFullViewDeploymentData: false,
		HasInstances:              true,
		HasObjects:                true,
	},
	atypepb.AssetType_ASSET_TYPE_DATA: {
		CodeName:                  "data",
		DisplayName:               "Data",
		DisplayNamePlural:         "Data",
		HasFullViewDeploymentData: true,
		HasInstances:              false,
		HasObjects:                false,
	},
	atypepb.AssetType_ASSET_TYPE_PROCESS: {
		CodeName:                  "process",
		DisplayName:               "Process",
		DisplayNamePlural:         "Processes",
		HasFullViewDeploymentData: true,
		HasInstances:              false,
		HasObjects:                false,
	},
}

// LINT.ThenChange(//intrinsic/assets/type_utils.ts:asset_type_info)

// LINT.IfChange(utils)

// AllAssetTypes returns all AssetTypes except ASSET_TYPE_UNSPECIFIED.
func AllAssetTypes() []atypepb.AssetType {
	var assetTypes []atypepb.AssetType
	for assetType := range allAssetTypeInfo {
		if assetType != atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
			assetTypes = append(assetTypes, assetType)
		}
	}
	return assetTypes
}

// AssetTypesWithFullViewDeploymentData returns the AssetTypes that provide deployment data in
// ASSET_VIEW_TYPE_FULL views.
func AssetTypesWithFullViewDeploymentData() []atypepb.AssetType {
	var assetTypes []atypepb.AssetType
	for assetType := range allAssetTypeInfo {
		if allAssetTypeInfo[assetType].HasFullViewDeploymentData {
			assetTypes = append(assetTypes, assetType)
		}
	}
	return assetTypes
}

// AssetTypesWithInstances returns the AssetTypes that have instances.
func AssetTypesWithInstances() []atypepb.AssetType {
	var assetTypes []atypepb.AssetType
	for assetType := range allAssetTypeInfo {
		if allAssetTypeInfo[assetType].HasInstances {
			assetTypes = append(assetTypes, assetType)
		}
	}
	return assetTypes
}

// AssetTypesWithObjects returns the AssetTypes that entail objects.
func AssetTypesWithObjects() []atypepb.AssetType {
	var assetTypes []atypepb.AssetType
	for assetType := range allAssetTypeInfo {
		if allAssetTypeInfo[assetType].HasObjects {
			assetTypes = append(assetTypes, assetType)
		}
	}
	return assetTypes
}

// AssetTypeCodeName returns the code name of an AssetType.
func AssetTypeCodeName(a atypepb.AssetType) string {
	if info, ok := allAssetTypeInfo[a]; ok {
		return info.CodeName
	}
	return allAssetTypeInfo[atypepb.AssetType_ASSET_TYPE_UNSPECIFIED].CodeName
}

// AssetTypeFromCodeName returns the AssetType for the given code name, or
// ASSET_TYPE_UNSPECIFIED if the code name is not known.
func AssetTypeFromCodeName(name string) atypepb.AssetType {
	for assetType, info := range allAssetTypeInfo {
		if info.CodeName == name {
			return assetType
		}
	}
	return atypepb.AssetType_ASSET_TYPE_UNSPECIFIED
}

// AssetTypeDisplayName returns the display name of an AssetType.
func AssetTypeDisplayName(a atypepb.AssetType) string {
	if info, ok := allAssetTypeInfo[a]; ok {
		return info.DisplayName
	}
	return allAssetTypeInfo[atypepb.AssetType_ASSET_TYPE_UNSPECIFIED].DisplayName
}

// AssetTypeFromDisplayName returns the AssetType for the given display name, or
// ASSET_TYPE_UNSPECIFIED if the display name is not known.
func AssetTypeFromDisplayName(name string) atypepb.AssetType {
	for assetType, info := range allAssetTypeInfo {
		if info.DisplayName == name {
			return assetType
		}
	}
	return atypepb.AssetType_ASSET_TYPE_UNSPECIFIED
}

// AssetTypeDisplayNamePlural returns the plural display name of an AssetType.
func AssetTypeDisplayNamePlural(a atypepb.AssetType) string {
	if info, ok := allAssetTypeInfo[a]; ok {
		return info.DisplayNamePlural
	}
	return allAssetTypeInfo[atypepb.AssetType_ASSET_TYPE_UNSPECIFIED].DisplayNamePlural
}

// AssetTypeDisplayNamesPlural returns the plural display names for a list of AssetTypes.
func AssetTypeDisplayNamesPlural(types []atypepb.AssetType) string {
	displayNames := make([]string, 0, len(types))
	foundDisplayNames := map[string]struct{}{}
	for _, at := range types {
		displayName := AssetTypeDisplayNamePlural(at)
		if displayName == "Assets" {
			return "Assets"
		}
		if _, ok := foundDisplayNames[displayName]; !ok {
			foundDisplayNames[displayName] = struct{}{}
			displayNames = append(displayNames, displayName)
		}
	}
	switch len(displayNames) {
	case 0:
		return "Assets"
	case 1:
		return displayNames[0]
	case 2:
		return fmt.Sprintf("%s and %s", displayNames[0], displayNames[1])
	default:
		displayNames, last := displayNames[:len(displayNames)-1], displayNames[len(displayNames)-1]
		return fmt.Sprintf("%s, and %s", strings.Join(displayNames, ", "), last)
	}
}

// AssetTypeName returns the name of the given AssetType.
func AssetTypeName(t atypepb.AssetType) string {
	return t.String()
}

// AssetTypeFromName returns an AssetType, given its name, or ASSET_TYPE_UNSPECIFIED if the name is
// not known.
func AssetTypeFromName(t string) atypepb.AssetType {
	if i, ok := atypepb.AssetType_value[t]; ok {
		return atypepb.AssetType(i)
	}
	return atypepb.AssetType_ASSET_TYPE_UNSPECIFIED
}

// AssetTypeFromInt returns an AssetType enum from an integer.
func AssetTypeFromInt(i int32) atypepb.AssetType {
	if _, ok := atypepb.AssetType_name[i]; ok {
		return atypepb.AssetType(i)
	}
	return atypepb.AssetType_ASSET_TYPE_UNSPECIFIED
}

// UniqueAssetTypes returns the unique AssetTypes from a list.
//
// Order is arbitrary, and the unspecified AssetType is ignored.
func UniqueAssetTypes(assetTypes []atypepb.AssetType) []atypepb.AssetType {
	assetTypesMap := map[atypepb.AssetType]struct{}{}
	for _, assetType := range assetTypes {
		if assetType != atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
			assetTypesMap[assetType] = struct{}{}
		}
	}
	uniqueAssetTypes := make([]atypepb.AssetType, len(assetTypesMap))
	i := 0
	for assetType := range assetTypesMap {
		uniqueAssetTypes[i] = assetType
		i++
	}
	return uniqueAssetTypes
}

// LINT.ThenChange(//intrinsic/assets/type_utils.ts:utils)
