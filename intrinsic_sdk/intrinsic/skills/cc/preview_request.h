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

#ifndef INTRINSIC_SKILLS_CC_PREVIEW_REQUEST_H_
#define INTRINSIC_SKILLS_CC_PREVIEW_REQUEST_H_

#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "intrinsic/util/proto/any.h"

namespace intrinsic {
namespace skills {

// A request for a call to SkillInterface::Preview.
class PreviewRequest {
 public:
  // `param_defaults` can specify default parameter values to merge into any
  // unset fields of `params`.
  explicit PreviewRequest(
      const ::google::protobuf::Message& params,
      ::google::protobuf::Message* param_defaults = nullptr)
  {
    params_any_.PackFrom(params);
    if (param_defaults != nullptr) {
      param_defaults_any_ = google::protobuf::Any();
      param_defaults_any_->PackFrom(*param_defaults);
    }
  }

  // Defers conversion of input Any params to target proto type until accessed
  // by the user in params().
  //
  // This constructor enables conversion from Any to the target type without
  // needing a message pool/factory up front, since params() is templated on the
  // target type.
  explicit PreviewRequest(
      google::protobuf::Any params,
      std::optional<::google::protobuf::Any> param_defaults)
        :
        params_any_(std::move(params)),
        param_defaults_any_(std::move(param_defaults)) {}

  // The skill parameters proto.
  template <class TParams>
  absl::StatusOr<TParams> params() const {
    return UnpackAnyAndMerge<TParams>(params_any_, param_defaults_any_);
  }

  // The skill parameters proto as an Any.
  ::google::protobuf::Any params_any() const { return params_any_; }

 private:

  ::google::protobuf::Any params_any_;
  std::optional<::google::protobuf::Any> param_defaults_any_;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_CC_PREVIEW_REQUEST_H_
