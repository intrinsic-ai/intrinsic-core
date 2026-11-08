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

#include "intrinsic/icon/server/testing/test_with_status_spec.h"

#include <gmock/gmock.h>

#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/icon/server/extended_status_constants.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_specs.h"

namespace intrinsic::icon {
void TestWithStatusSpec::SetUp() {
  ASSERT_OK_AND_ASSIGN(auto specs,
                       ParseTextProto<intrinsic_proto::assets::StatusSpecs>(
                           intrinsic::icon::kStatusSpecs));
  ASSERT_OK(intrinsic::InitExtendedStatusSpecs(
      intrinsic::icon::kExtendedStatusComponent, specs.status_info()));
}

TestWithStatusSpec::~TestWithStatusSpec() { ClearExtendedStatusSpecs(); }

}  // namespace intrinsic::icon
