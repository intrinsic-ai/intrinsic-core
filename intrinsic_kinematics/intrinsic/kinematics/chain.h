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

#ifndef INTRINSIC_KINEMATICS_CHAIN_H_
#define INTRINSIC_KINEMATICS_CHAIN_H_

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/kinematics/elements.h"
#include "intrinsic/kinematics/group.h"
#include "intrinsic/kinematics/model_interface.h"

namespace intrinsic {
namespace kinematics {

// A Chain is a special version of a Group where all elements have a single
// child.
//
// Like for Group, the Chain doesn't own its elements so the model that owns
// the elements used in the Chain must outlive the Chain.
class Chain : public Group {
 public:
  explicit Chain(absl::string_view name = "");

  absl::Status ExtractFromModel(
      const ModelInterface& model, const ElementId& base_id,
      const ElementId& tip_id) INTRINSIC_NON_REALTIME_ONLY;

  ElementId GetTipId() const;

  // Removed since we enforce a single tip.
  absl::Status ExtractFromModel(const ModelInterface& model,
                                const ElementId& base_id,
                                absl::Span<const ElementId> tip_ids) = delete;
  // Removed since we don't support the creation of a chain from two branches.
  absl::Status ExtractFromModel(const ModelInterface& model,
                                absl::Span<const ElementId> tip_ids) = delete;
};

// Convert a model to a chain. Will return an error if the model is not a
// kinematic chain.
absl::StatusOr<Chain> CreateChainFromModel(
    const ModelInterface& model ABSL_ATTRIBUTE_LIFETIME_BOUND);
absl::StatusOr<Chain> CreateChainFromModel(
    const ModelInterface& model ABSL_ATTRIBUTE_LIFETIME_BOUND,
    const ElementId& base_id, const ElementId& tip_id);

// Converts a model to a chain, using the model's base link and unique tip link
// (if applicable).
// Returns an error if the model is not a single kinematic chain. This function
// ignores coordinate frames. If you want to build a chain that includes
// coordinate frames, use CreateChainFromModel.
absl::StatusOr<Chain> ExtractNonBranchingChain(const ModelInterface& model);

}  // namespace kinematics
}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_CHAIN_H_
