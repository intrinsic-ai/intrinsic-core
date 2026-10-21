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

// Package processfix contains utils that adapt Processes to meet the requirements of the latest
// platform version.
package processfix

import (
	"fmt" 

	"intrinsic/util/proto/descriptorcompatibility" 

	papb "intrinsic/assets/processes/proto/process_asset_go_proto"
	pmpb "intrinsic/assets/processes/proto/process_manifest_go_proto"
)

// Manifest updates a ProcessManifest to meet the requirements of the latest platform version.
func Manifest(manifest *pmpb.ProcessManifest) error {
	return nil
}

// ProcessAsset updates a ProcessAsset to meet the requirements of the latest platform version.
func ProcessAsset(pa *papb.ProcessAsset) error {
	// The metadata in an Asset definition shouldn't specify a version.
	if pa.GetMetadata().GetIdVersion().GetVersion() != "" {
		pa.GetMetadata().GetIdVersion().Version = ""
	}

	if skill := pa.GetBehaviorTree().GetDescription(); skill != nil {
		if paramDesc := skill.GetParameterDescription(); paramDesc != nil {
			if err := descriptorcompatibility.Reconcile(paramDesc.GetParameterDescriptorFileset()); err != nil {
				return fmt.Errorf("unable to reconcile parameter descriptor fileset in BehaviorTree description for Process Asset: %w", err)
			}
		}
		if retDesc := skill.GetReturnValueDescription(); retDesc != nil {
			if err := descriptorcompatibility.Reconcile(retDesc.GetDescriptorFileset()); err != nil {
				return fmt.Errorf("unable to reconcile return value descriptor fileset in BehaviorTree description for Process Asset: %w", err)
			}
		}
	}

	return nil
}
