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

#include "intrinsic/icon/control/algorithms/dynamic_prefilter.h"

#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_collection_prefilter_config.pb.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/proto/iir_filter_config.pb.h"
#include "intrinsic/math/signals/iir_filt.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

/*static*/
absl::StatusOr<SisoPrefilter> SisoPrefilter::Create(
    const intrinsic_proto::DiscreteTimeIirFilterConfig& siso_filter_config) {
  INTR_ASSIGN_OR_RETURN(
      auto prefilter,
      (IirFilter<double, kMaxPrefilterOrder>::Create(siso_filter_config)));
  return SisoPrefilter(std::move(prefilter));
}

/*static*/
absl::StatusOr<JointCollectionPrefilter> JointCollectionPrefilter::Create(
    const intrinsic_proto::icon::JointCollectionPrefilterConfig&
        joint_collection_prefilter_config) {
  const int ndof = joint_collection_prefilter_config.joint_prefilter_size();
  std::vector<SisoPrefilter> dof_prefilters;
  dof_prefilters.reserve(ndof);
  for (int i = 0; i < ndof; ++i) {
    INTR_ASSIGN_OR_RETURN(
        SisoPrefilter dof_prefilter,
        SisoPrefilter::Create(
            joint_collection_prefilter_config.joint_prefilter(i)));
    dof_prefilters.push_back(std::move(dof_prefilter));
  }
  return JointCollectionPrefilter(std::move(dof_prefilters));
}

RealtimeStatus JointCollectionPrefilter::InitializeInSteadyState(
    const eigenmath::VectorNd& steady_state_position) {
  if (steady_state_position.size() != dof_prefilters_.size()) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Size mismatch between position command, which is ",
        steady_state_position.size(), " and filter size, which is ",
        dof_prefilters_.size(), "."));
  }

  for (int i = 0; i < dof_prefilters_.size(); ++i) {
    dof_prefilters_[i].InitializeInSteadyState(steady_state_position[i]);
  }

  return OkStatus();
}

RealtimeStatusOr<eigenmath::VectorNd> JointCollectionPrefilter::ComputeControl(
    const eigenmath::VectorNd& input) {
  if (input.size() != dof_prefilters_.size()) {
    return InvalidArgumentError(FixedStrCat<RealtimeStatus::kMaxMessageLength>(
        "Size mismatch between ComputeControl() input, which is ", input.size(),
        " and filter size, which is ", dof_prefilters_.size(), "."));
  }

  eigenmath::VectorNd output =
      eigenmath::VectorNd::Zero(dof_prefilters_.size());
  for (int i = 0; i < dof_prefilters_.size(); ++i) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(output[i],
                                  dof_prefilters_[i].ComputeControl(input(i)));
  }
  return output;
}

}  // namespace intrinsic::icon
