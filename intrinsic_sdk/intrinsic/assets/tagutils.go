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

// Package tagutils provides utilities for AssetTags.
package tagutils

import (
	"fmt"
	"slices"
	"strings"

	"google.golang.org/protobuf/encoding/prototext"

	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"

	_ "embed"
)

//go:embed proto/asset_tags.textproto
var assetTags []byte

// allAssetTagMetadata is the cached return value of AllAssetTagMetadata.
var allAssetTagMetadata []*atagpb.AssetTagMetadata

// allAssetTagMetadataComputed indicates whether allAssetTagMetadata has been computed.
var allAssetTagMetadataComputed bool

// LINT.IfChange(utils)

// AllAssetTags returns all AssetTags except ASSET_TAG_UNSPECIFIED.
//
// Tags are returned in enum order.
func AllAssetTags() []atagpb.AssetTag {
	allTags := make([]atagpb.AssetTag, len(atagpb.AssetTag_value)-1)
	for _, value := range atagpb.AssetTag_value {
		tag := atagpb.AssetTag(value)
		if tag != atagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
			allTags[value-1] = tag
		}
	}
	return allTags
}

// AssetTagDisplayName returns the display name of the given AssetTag, or "Unknown" if the tag is
// not known.
func AssetTagDisplayName(tag atagpb.AssetTag) string {
	metadata, err := AssetTagMetadataForTag(tag)
	if err != nil {
		return "Unknown"
	}
	return metadata.GetDisplayName()
}

// AssetTagFromDisplayName returns the AssetTag for the given display name, or
// ASSET_TAG_UNSPECIFIED if the display name is not known.
func AssetTagFromDisplayName(name string) atagpb.AssetTag {
	lcName := strings.ReplaceAll(strings.ToLower(name), " ", "_")
	allMetadata, err := AllAssetTagMetadata()
	if err != nil {
		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED
	}
	for _, tagMetadata := range allMetadata {
		if strings.ReplaceAll(strings.ToLower(tagMetadata.GetDisplayName()), " ", "_") == lcName {
			return tagMetadata.GetAssetTag()
		}
	}
	return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED
}

// AssetTagName returns the name of the given AssetTag.
func AssetTagName(tag atagpb.AssetTag) string {
	return tag.String()
}

// AssetTagFromName returns an AssetTag, given its name, or ASSET_TAG_UNSPECIFIED if the name is not
// known.
func AssetTagFromName(name string) atagpb.AssetTag {
	value, ok := atagpb.AssetTag_value[name]
	if !ok {
		return atagpb.AssetTag_ASSET_TAG_UNSPECIFIED
	}

	return atagpb.AssetTag(value)
}

// AssetTagsForTypes returns a list of AssetTags that apply to any of the specified AssetTypes.
func AssetTagsForTypes(assetTypes []atypepb.AssetType) ([]atagpb.AssetTag, error) {
	tags := make(map[atagpb.AssetTag]struct{})
	for _, assetType := range assetTypes {
		assetTagMetadata, err := AssetTagMetadataForType(assetType)
		if err != nil {
			return nil, err
		}
		for _, tagMetadata := range assetTagMetadata {
			tags[tagMetadata.GetAssetTag()] = struct{}{}
		}
	}

	// Construct the tags in enum order.
	tagsForTypes := make([]atagpb.AssetTag, 0, len(tags))
	for tag := range atagpb.AssetTag_name {
		if _, ok := tags[atagpb.AssetTag(tag)]; ok {
			tagsForTypes = append(tagsForTypes, atagpb.AssetTag(tag))
		}
	}

	return tagsForTypes, nil
}

// AssetTagsForType returns a list of AssetTags that apply to the specified AssetType.
//
// Tags are returned in enum order.
func AssetTagsForType(assetType atypepb.AssetType) ([]atagpb.AssetTag, error) {
	metadata, err := AssetTagMetadataForType(assetType)
	if err != nil {
		return nil, err
	}

	tags := map[atagpb.AssetTag]struct{}{}
	for _, tagMetadata := range metadata {
		tags[tagMetadata.GetAssetTag()] = struct{}{}
	}

	// Construct the tags in enum order.
	tagsForType := make([]atagpb.AssetTag, 0, len(tags))
	for tag := range atagpb.AssetTag_name {
		if _, ok := tags[atagpb.AssetTag(tag)]; ok {
			tagsForType = append(tagsForType, atagpb.AssetTag(tag))
		}
	}

	return tagsForType, nil
}

// AllAssetTagMetadata returns a list of all AssetTagMetadata.
func AllAssetTagMetadata() ([]*atagpb.AssetTagMetadata, error) {
	// Return cached value if it has been computed.
	if allAssetTagMetadataComputed {
		return allAssetTagMetadata, nil
	}

	metadataSet := &atagpb.AssetTagMetadataSet{}
	if err := prototext.Unmarshal(assetTags, metadataSet); err != nil {
		return nil, err
	}

	allAssetTagMetadata = metadataSet.GetTags()
	allAssetTagMetadataComputed = true

	return allAssetTagMetadata, nil
}

// AssetTagMetadataForTag returns the AssetTagMetadata for the specified AssetTag.
func AssetTagMetadataForTag(tag atagpb.AssetTag) (*atagpb.AssetTagMetadata, error) {
	allMetadata, err := AllAssetTagMetadata()
	if err != nil {
		return nil, err
	}
	for _, tagMetadata := range allMetadata {
		if tagMetadata.GetAssetTag() == tag {
			return tagMetadata, nil
		}
	}
	return nil, fmt.Errorf("no metadata found for tag %v", tag)
}

// AssetTagMetadataForType returns a list of AssetTagMetadata for the specified AssetType.
func AssetTagMetadataForType(assetType atypepb.AssetType) ([]*atagpb.AssetTagMetadata, error) {
	allMetadata, err := AllAssetTagMetadata()
	if err != nil {
		return nil, err
	}

	metadata := make([]*atagpb.AssetTagMetadata, 0, len(allMetadata))
	for _, tagMetadata := range allMetadata {
		if slices.Contains(tagMetadata.GetApplicableAssetTypes(), assetType) {
			metadata = append(metadata, tagMetadata)
		}
	}

	return metadata, nil
}

// LINT.ThenChange(//intrinsic/assets/tag_utils.ts:utils)
