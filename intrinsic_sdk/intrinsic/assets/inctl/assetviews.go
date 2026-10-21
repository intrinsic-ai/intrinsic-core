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

// Package assetviews contains utils for commands that list Assets.
package assetviews

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"slices"

	"intrinsic/assets/idutils"

	"google.golang.org/protobuf/proto"

	metadatapb "intrinsic/assets/proto/metadata_go_proto"
)

// Asset is an interface for Asset protos that provide metadata.
type Asset interface {
	proto.Message

	GetMetadata() *metadatapb.Metadata
}

// AssetTextViewType is the view type of an Asset when formatted for text output.
type AssetTextViewType string

const (
	// AssetTextViewTypeIDVersion is the view type for outputting Asset ID versions.
	AssetTextViewTypeIDVersion AssetTextViewType = "id_version"
	// AssetTextViewTypeID is the view type for outputting Asset IDs.
	AssetTextViewTypeID AssetTextViewType = "id"
	// AssetTextViewTypeIDVersionProtoBase64 is the view type for outputting Asset ID versions as a proto base64 string.
	AssetTextViewTypeIDVersionProtoBase64 AssetTextViewType = "id_version_proto_base64"
)

// AllAssetTextViewTypes is a list of all AssetTextViewTypes.
var AllAssetTextViewTypes = []AssetTextViewType{
	AssetTextViewTypeIDVersion,
	AssetTextViewTypeID,
	AssetTextViewTypeIDVersionProtoBase64,
}

// AssetTextViewTypeFromString returns the AssetTextViewType for the specified string.
func AssetTextViewTypeFromString(viewType string) (AssetTextViewType, error) {
	vt := AssetTextViewType(viewType)
	if !slices.Contains(AllAssetTextViewTypes, vt) {
		return "", fmt.Errorf("invalid view type: %q", viewType)
	}

	return vt, nil
}

// AssetView describes an Asset.
//
// It implements methods intended for use with printing outputs of Asset list commands.
type AssetView struct {
	asset        Asset
	textViewType AssetTextViewType
}

// MarshalJSON marshals a description of the Asset.
func (v *AssetView) MarshalJSON() ([]byte, error) {
	description, err := descriptionFrom(v.asset)
	if err != nil {
		return nil, err
	}
	return json.Marshal(description)
}

// String returns a string view of the Asset, based on the view's text view type.
func (v *AssetView) String() string {
	switch v.textViewType {
	case AssetTextViewTypeID:
		return idutils.IDFromProtoUnchecked(v.asset.GetMetadata().GetIdVersion().GetId())
	case AssetTextViewTypeIDVersionProtoBase64:
		data, err := proto.Marshal(v.asset.GetMetadata().GetIdVersion())
		if err != nil {
			return ""
		}
		return base64.StdEncoding.EncodeToString(data)
	default: // Default to AssetTextViewTypeIDVersion
		return idutils.IDVersionFromProtoUnchecked(v.asset.GetMetadata().GetIdVersion())
	}
}

// Option is a functional option for an AssetView.
type Option func(*AssetView)

// WithTextViewType sets the text view type of the AssetView.
func WithTextViewType(viewType AssetTextViewType) Option {
	return func(ad *AssetView) {
		ad.textViewType = viewType
	}
}

// FromAsset returns an AssetView for the specified Asset.
func FromAsset(asset Asset, options ...Option) *AssetView {
	assetView := &AssetView{
		asset: asset,
	}
	for _, option := range options {
		option(assetView)
	}
	return assetView
}

type assetDescription struct {
	Name         string `json:"name,omitempty"`
	Vendor       string `json:"vendor,omitempty"`
	PackageName  string `json:"packageName,omitempty"`
	Version      string `json:"version,omitempty"`
	UpdateTime   string `json:"updateTime,omitempty"`
	ID           string `json:"id,omitempty"`
	IDVersion    string `json:"idVersion,omitempty"`
	ReleaseNotes string `json:"releaseNotes,omitempty"`
	Description  string `json:"description,omitempty"`
}

func descriptionFrom(asset Asset) (*assetDescription, error) {
	metadata := asset.GetMetadata()
	idVersion, err := idutils.IDVersionFromProto(metadata.GetIdVersion())
	if err != nil {
		return nil, err
	}
	ivp, err := idutils.NewIDVersionParts(idVersion)
	if err != nil {
		return nil, err
	}

	description := &assetDescription{
		Name:         ivp.Name(),
		Vendor:       metadata.GetVendor().GetDisplayName(),
		PackageName:  ivp.Package(),
		Version:      ivp.Version(),
		ID:           ivp.ID(),
		IDVersion:    idVersion,
		ReleaseNotes: metadata.GetReleaseNotes(),
		Description:  metadata.GetDocumentation().GetDescription(),
	}
	if metadata.GetUpdateTime() != nil {
		description.UpdateTime = metadata.GetUpdateTime().AsTime().String()
	}

	return description, nil
}
