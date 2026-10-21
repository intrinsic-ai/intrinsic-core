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

// Package datafix contains utils that adapt Data Assets to meet the requirements of the latest
// platform version.
package datafix

import (
	"fmt" 

	"intrinsic/util/proto/descriptorcompatibility" 

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dmpb "intrinsic/assets/data/proto/v1/data_manifest_go_proto"
)

// Manifest updates a DataManifest to meet the requirements of the latest platform version.
func Manifest(manifest *dmpb.DataManifest) error {
	return nil
}

// DataAsset updates a DataAsset to meet the requirements of the latest platform version.
func DataAsset(da *dapb.DataAsset) error {
	// The metadata in an Asset definition shouldn't specify a version.
	if da.GetMetadata().GetIdVersion().GetVersion() != "" {
		da.GetMetadata().GetIdVersion().Version = ""
	}

	if err := descriptorcompatibility.Reconcile(da.GetFileDescriptorSet()); err != nil {
		return fmt.Errorf("unable to reconcile the file descriptor set for Data Asset: %w", err)
	}

	return nil
}
