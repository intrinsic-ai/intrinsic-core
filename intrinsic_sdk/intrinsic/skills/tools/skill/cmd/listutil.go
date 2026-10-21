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

// Package listutil contains utils for commands that list released skills.
package listutil

import (
	"encoding/json"
	"fmt"
	"sort"
	"strings"

	spb "intrinsic/skills/proto/skills_go_proto"
)

// SkillDescription has custom proto->json conversion to handle fields like the update timestamp.
type SkillDescription struct {
	Name        string `json:"name,omitempty"`
	PackageName string `json:"packageName,omitempty"`
	ID          string `json:"id,omitempty"`
	IDVersion   string `json:"idVersion,omitempty"`
	Description string `json:"description,omitempty"`
}

// SkillDescriptions wraps the required data for the output of skill list commands.
type SkillDescriptions struct {
	Skills []SkillDescription `json:"skills"`
}

// SkillDescriptionsFromSkills creates a SkillDescriptions instance from Skill protos
func SkillDescriptionsFromSkills(skills []*spb.Skill) *SkillDescriptions {
	out := SkillDescriptions{Skills: make([]SkillDescription, len(skills))}

	for i, skill := range skills {
		out.Skills[i] = SkillDescription{
			Name:        skill.GetSkillName(),
			PackageName: skill.GetPackageName(),
			ID:          skill.GetId(),
			IDVersion:   skill.GetIdVersion(),
			Description: skill.GetDescription(),
		}
	}

	return &out
}

// MarshalJSON converts a SkillDescription to a byte slice.
func (sd SkillDescriptions) MarshalJSON() ([]byte, error) {
	return json.Marshal(struct {
		Skills []SkillDescription `json:"skills"`
	}{Skills: sd.Skills})
}

// String converts a SkillDescription to a string
func (sd SkillDescriptions) String() string {
	lines := []string{}
	for _, skill := range sd.Skills {
		lines = append(lines, fmt.Sprintf("%s", skill.IDVersion))
	}
	sort.Strings(lines)
	return strings.Join(lines, "\n")
}
