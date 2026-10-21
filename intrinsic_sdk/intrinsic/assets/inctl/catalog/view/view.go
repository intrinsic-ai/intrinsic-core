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

// Package view provides a printer.View implementation for Asset catalog proto messages.
package view

import (
	"fmt"
	"strconv"
	"strings"
	"text/template"
	"time"

	"intrinsic/assets/idutils"
	"intrinsic/assets/typeutils"
	"intrinsic/tools/inctl/util/printer"

	"google.golang.org/protobuf/encoding/protojson"

	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	assettagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
)

// TableConfig returns a printer.Option configuring a tabular printer with
// printer.DefaultTableConfig and the provided columns.
func TableConfig(columns ...string) printer.Option {
	return func(ot printer.OutputType, p any) error {
		if err := printer.WithTableConfig(printer.DefaultTableConfig, columns...)(ot, p); err != nil {
			return err
		}
		return printer.WithHeaderOverride(headerName)(ot, p)
	}
}

// headerName translates column identifiers into clean, uppercase table headers.
func headerName(name string) string {
	switch name {
	case "id":
		return "ID"
	case "version":
		return "VERSION"
	case "default":
		return "DEFAULT"
	case "org_private":
		return "ORG PRIVATE"
	case "update_time":
		return "UPDATE TIME"
	default:
		return strings.ToUpper(printer.PrettyHeaderColumn(name))
	}
}

type assetView struct {
	asset *acpb.Asset
}

// Of wraps the given Asset proto message in a printer.View.
func Of(asset *acpb.Asset) printer.View {
	return &assetView{asset: asset}
}

// MarshalJSON delegates JSON marshalling to protojson.
func (v *assetView) MarshalJSON() ([]byte, error) {
	if v.asset == nil {
		return []byte("null"), nil
	}
	return protojson.Marshal(v.asset)
}

func idFromProto(id *idpb.Id) string {
	if id.GetPackage() == "" && id.GetName() == "" {
		return ""
	}
	return idutils.IDFromProtoUnchecked(id)
}

func idVersionFromProto(iv *idpb.IdVersion) string {
	if iv.GetId().GetPackage() == "" && iv.GetId().GetName() == "" {
		return iv.GetVersion()
	}
	if iv.GetVersion() == "" {
		return idFromProto(iv.GetId())
	}
	return idutils.IDVersionFromProtoUnchecked(iv)
}

func formatAssetTag(tag assettagpb.AssetTag) string {
	switch tag {
	case assettagpb.AssetTag_ASSET_TAG_CAMERA:
		return "Camera"
	case assettagpb.AssetTag_ASSET_TAG_GRIPPER:
		return "Gripper"
	case assettagpb.AssetTag_ASSET_TAG_SUBPROCESS:
		return "Subprocess"
	case assettagpb.AssetTag_ASSET_TAG_ARM:
		return "Arm"
	default:
		s := strings.TrimPrefix(tag.String(), "ASSET_TAG_")
		return printer.Capitalize(strings.ToLower(s))
	}
}

// Tabulated converts the asset into row values corresponding to the requested columns.
func (v *assetView) Tabulated(columns []string) []string {
	res := make([]string, len(columns))
	for i, col := range columns {
		res[i] = v.value(col)
	}
	return res
}

func (v *assetView) value(col string) string {
	if v.asset == nil {
		return ""
	}
	md := v.asset.GetMetadata()
	switch strings.ToLower(strings.ReplaceAll(col, "-", "_")) {
	case "id":
		return idFromProto(md.GetIdVersion().GetId())
	case "version":
		return md.GetIdVersion().GetVersion()
	case "display_name", "name":
		return md.GetDisplayName()
	case "default":
		if rm := v.asset.GetReleaseMetadata(); rm != nil {
			return strconv.FormatBool(rm.GetDefault())
		}
		return "false"
	case "org_private":
		if rm := v.asset.GetReleaseMetadata(); rm != nil {
			return strconv.FormatBool(rm.GetOrgPrivate())
		}
		return "false"
	case "update_time":
		if t := md.GetUpdateTime(); t != nil {
			return t.AsTime().Format(time.RFC3339)
		}
		return ""
	default:
		return ""
	}
}

func indent(spaces int, s string) string {
	if s == "" {
		return ""
	}
	prefix := strings.Repeat(" ", spaces)
	var res []string
	for _, line := range strings.Split(s, "\n") {
		if line != "" {
			res = append(res, prefix+line)
		} else {
			res = append(res, "")
		}
	}
	return strings.Join(res, "\n")
}

type releaseTemplateData struct {
	Default    bool
	OrgPrivate bool
}

type assetTemplateData struct {
	ID              string
	Version         string
	DisplayName     string
	Type            string
	Vendor          string
	Tag             string
	UpdateTime      string
	Description     string
	ReleaseNotes    string
	Provides        []string
	ReleaseMetadata *releaseTemplateData
}

var assetTmpl = template.Must(template.New("asset").Funcs(template.FuncMap{
	"indent": indent,
	"containsNewline": func(s string) bool {
		return strings.Contains(s, "\n")
	},
}).Parse(
	`{{- with .ID }}
ID:           {{ . }}
{{- end }}
{{- with .Version }}
Version:      {{ . }}
{{- end }}
{{- with .DisplayName }}
Display Name: {{ . }}
{{- end }}
{{- with .Type }}
Type:         {{ . }}
{{- end }}
{{- with .Vendor }}
Vendor:       {{ . }}
{{- end }}
{{- with .Tag }}
Asset Tag:    {{ . }}
{{- end }}
{{- with .UpdateTime }}
Update Time:  {{ . }}
{{- end }}
{{- with .Description }}
{{- if containsNewline . }}
Description:
{{ indent 2 . }}
{{- else }}
Description:  {{ . }}
{{- end }}
{{- end }}
{{- with .ReleaseNotes }}
{{- if containsNewline . }}
Release Notes:
{{ indent 2 . }}
{{- else }}
Release Notes:
  {{ . }}
{{- end }}
{{- end }}
{{- with .Provides }}
Provides:
{{- range . }}
  - {{ . }}
{{- end }}
{{- end }}
{{- with .ReleaseMetadata }}
Release:
  Default:     {{ .Default }}
  Org Private: {{ .OrgPrivate }}
{{- end }}
`))

// String formats the Asset as a structured, human-readable text presentation.
func (v *assetView) String() string {
	if v.asset == nil {
		return ""
	}

	data := assetTemplateData{}
	md := v.asset.GetMetadata()
	if md != nil {
		data.ID = idFromProto(md.GetIdVersion().GetId())
		data.Version = md.GetIdVersion().GetVersion()
		data.DisplayName = md.GetDisplayName()
		if md.GetAssetType() != atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
			data.Type = typeutils.AssetTypeDisplayName(md.GetAssetType())
		}
		data.Vendor = md.GetVendor().GetDisplayName()
		if md.GetAssetTag() != assettagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
			data.Tag = formatAssetTag(md.GetAssetTag())
		}
		if md.GetUpdateTime() != nil {
			data.UpdateTime = md.GetUpdateTime().AsTime().Format(time.RFC3339)
		}
		data.Description = md.GetDocumentation().GetDescription()
		data.ReleaseNotes = md.GetReleaseNotes()
		for _, p := range md.GetProvides() {
			if uri := p.GetUri(); uri != "" {
				data.Provides = append(data.Provides, uri)
			}
		}
	}

	if rm := v.asset.GetReleaseMetadata(); rm != nil {
		data.ReleaseMetadata = &releaseTemplateData{
			Default:    rm.GetDefault(),
			OrgPrivate: rm.GetOrgPrivate(),
		}
	}

	var sb strings.Builder
	if err := assetTmpl.Execute(&sb, data); err != nil {
		return fmt.Sprintf("Error executing template: %v", err)
	}

	return strings.TrimSpace(sb.String())
}
