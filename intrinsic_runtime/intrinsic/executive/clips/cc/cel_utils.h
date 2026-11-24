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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_UTILS_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_UTILS_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "cel/expr/syntax.pb.h"
#include "eval/public/cel_expression.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"

namespace intrinsic::executive::clips {

absl::StatusOr<
    std::unique_ptr<google::api::expr::runtime::CelExpressionBuilder>>
CreateCelExpressionBuilder(
    const ProtobufManager::DescriptorPoolInfo& pool_info);

// Returns a string representation of the given CEL expression proto.
//
// Only supports the subset of the CEL syntax that is relevant for behavior
// trees in the Intrinsic platform. Returns an invalid argument error if the
// expression or parts of it are not supported.
absl::StatusOr<std::string> UnparseCelExpression(const cel::expr::Expr& expr);

}  // namespace intrinsic::executive::clips

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_UTILS_H_
