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

#include "intrinsic/icon/server/example/noop_action.h"

#include <string>

#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic {
namespace icon {

intrinsic_proto::icon::v1::ActionSignature NoopActionSignature() {
  intrinsic_proto::icon::v1::ActionSignature signature;
  signature.set_action_type_name(kNoopAction);
  signature.set_text_description("Does nothing.");
  intrinsic_proto::icon::v1::ActionSignature::PartSlotInfo part_slot_info;
  part_slot_info.set_description("Can be whatever.");
  signature.mutable_part_slot_infos()->insert(
      {std::string(kNoopActionSlotName), part_slot_info});
  return signature;
}

}  // namespace icon
}  // namespace intrinsic
