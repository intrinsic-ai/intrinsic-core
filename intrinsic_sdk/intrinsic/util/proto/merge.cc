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

#include "intrinsic/util/proto/merge.h"

#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace intrinsic {

namespace {

absl::flat_hash_set<const google::protobuf::OneofDescriptor*> OneofsSet(
    absl::Span<const google::protobuf::FieldDescriptor* const>
        field_descriptors) {
  absl::flat_hash_set<const google::protobuf::OneofDescriptor*> oneofs_set;
  for (const auto* field : field_descriptors) {
    if (const google::protobuf::OneofDescriptor* one_of =
            field->containing_oneof();
        one_of != nullptr) {
      oneofs_set.insert(one_of);
    }
  }
  return oneofs_set;
}

}  // namespace

absl::Status MergeUnset(const google::protobuf::Message& from,
                        google::protobuf::Message& to) {
  if (from.GetDescriptor() != to.GetDescriptor()) {
    return absl::InvalidArgumentError("`from` and `to` must be the same type");
  }

  const google::protobuf::Reflection* to_reflection = to.GetReflection();

  std::vector<const google::protobuf::FieldDescriptor*> to_fields;
  to_reflection->ListFields(to, &to_fields);
  std::vector<const google::protobuf::FieldDescriptor*> from_fields;
  from.GetReflection()->ListFields(from, &from_fields);

  const absl::flat_hash_set<const google::protobuf::FieldDescriptor*>
      to_field_set = {to_fields.begin(), to_fields.end()};

  const absl::flat_hash_set<const google::protobuf::OneofDescriptor*>
      to_field_set_oneofs = OneofsSet(to_fields);

  absl::flat_hash_set<const google::protobuf::FieldDescriptor*>
      fields_set_in_from_but_not_to = {from_fields.begin(), from_fields.end()};

  absl::erase_if(
      fields_set_in_from_but_not_to,
      [&to_field_set,
       &to_field_set_oneofs](const google::protobuf::FieldDescriptor* field) {
        return to_field_set.contains(field) ||
               // Don't overwrite oneof fields of which a different member of
               // the oneof is set.
               (field->containing_oneof() != nullptr &&
                to_field_set_oneofs.contains(field->containing_oneof()));
      });

  std::vector<const google::protobuf::FieldDescriptor*>
      in_from_set_not_in_to_set_vec = {fields_set_in_from_but_not_to.begin(),
                                       fields_set_in_from_but_not_to.end()};

  std::unique_ptr<google::protobuf::Message> swap_space(from.New());
  swap_space->CopyFrom(from);
  to_reflection->SwapFields(swap_space.get(), &to,
                            in_from_set_not_in_to_set_vec);

  return absl::OkStatus();
}

}  // namespace intrinsic
