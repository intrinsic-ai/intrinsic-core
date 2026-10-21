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

package typeurl

const (
	// DefaultPrefix is the default type URL prefix used by Protobuf.
	DefaultPrefix = "type.googleapis.com/"

	// IntrinsicPrefix is the prefix for Intrinsic type URLs which can be resolved
	// by the proto registry.
	IntrinsicPrefix = "type.intrinsic.ai/"

	// IntrinsicAreaSkills is the area (=the first path element) used in Intrinsic
	// type URLs for skills.
	IntrinsicAreaSkills = "skills"

	// IntrinsicAreaAssets is the area (=the first path element) used in Intrinsic
	// type URLs for assets.
	IntrinsicAreaAssets = "assets"

	// IntrinsicAreaCommon is the area (=the first path element) used in Intrinsic
	// type URLs for common types.
	IntrinsicAreaCommon = "common"

	// IntrinsicAreaWellKnown is the area (=the first path element) used in
	// Intrinsic type URLs for well known types.
	//
	// Deprecated: Use [IntrinsicAreaCommon] instead.
	IntrinsicAreaWellKnown = "well-known"

	// Separator is the top-level separator used in type URLs.
	Separator = "/"
)
