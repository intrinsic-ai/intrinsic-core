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

// Package skillmanifest provides utils for SkillManifests.
package skillmanifest

import (
	"intrinsic/util/proto/sourcecodeinfoview"

	smpb "intrinsic/skills/proto/skill_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
)

// PruneSourceCodeInfo removes source code info from the FileDescriptorSet for all message types
// except those that are referenced by the SkillManifest.
func PruneSourceCodeInfo(m *smpb.SkillManifest, fds *dpb.FileDescriptorSet) error {
	var fullNames []string
	if name := m.GetParameter().GetMessageFullName(); name != "" {
		fullNames = append(fullNames, name)
	}
	if name := m.GetReturnType().GetMessageFullName(); name != "" {
		fullNames = append(fullNames, name)
	}
	return sourcecodeinfoview.PruneSourceCodeInfo(fds,
		sourcecodeinfoview.WithExcludeNames(fullNames),
	)
}
