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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_DYNAMIC_PREFILTER_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_DYNAMIC_PREFILTER_H_

#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_collection_prefilter_config.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/proto/iir_filter_config.pb.h"
#include "intrinsic/math/signals/iir_filt.h"

namespace intrinsic::icon {

// MAX_EIGEN_VECTOR_SIZE should be sufficient as maximum dynamic prefilter
// order. Consider running a model order reduction (e.g. balanced truncation) if
// this is not enough.
constexpr int kMaxPrefilterOrder = eigenmath::MAX_EIGEN_VECTOR_SIZE;

// Single-input single-output (SiSo) discrete-time dynamic prefilter. This class
// is a helper to simplify the use of infinite impulse response (IIR) filters
// for dynamic prefiltering purposes. For example, it can be used to enhance the
// position response for articulated robots.
class SisoPrefilter {
 public:
  // Creates a SiSo dynamic prefilter from an a proto config for the IIR filter
  // coefficients. IIR filter is required to be stable, since this factory does
  // not perform an additional stability check. The number of
  // nominator/denominator coefficients in the filter config must be smaller or
  // equal than kMaxPrefilterOrder+1.
  static absl::StatusOr<SisoPrefilter> Create(
      const intrinsic_proto::DiscreteTimeIirFilterConfig& siso_filter_config);

  // Initializes the IIR filter at a steady state around
  // `steady_state_position`. This method is real-time capable and can be called
  // at runtime with measured values.
  void InitializeInSteadyState(double steady_state_position) {
    siso_dynamic_prefilter_.Initialize(steady_state_position);
  }

  // Returns the dynamic filter response following `input`. This method should
  // only be called once per real-time cycle.
  RealtimeStatusOr<double> ComputeControl(double input) {
    return siso_dynamic_prefilter_.Step(input);
  }

  // Returns a reference to the underlying IIR filter class.
  const IirFilter<double, kMaxPrefilterOrder>& GetIirFilter() const {
    return siso_dynamic_prefilter_;
  }

 private:
  explicit SisoPrefilter(IirFilter<double, kMaxPrefilterOrder> prefilter)
      : siso_dynamic_prefilter_(std::move(prefilter)) {}

  // Fixed-size IIR filter instance.
  IirFilter<double, kMaxPrefilterOrder> siso_dynamic_prefilter_;
};

// Multi-DoF dynamic prefilter, which is a collection of individual
// discrete-time dynamic prefilters. This class is a helper to simplify use
// of infinite impulse response (IIR) filters for dynamic prefiltering of
// multi-joint robots. For example, it can be used to enhance the position
// response for articulated robots.
class JointCollectionPrefilter {
 public:
  // Creates a multi-dof dynamic prefilter from
  // `joint_collection_prefilter_config` which holds IIR filter coefficients.
  // The number of degrees of freedom are derived from the number of repeated
  // fields in `joint_collection_prefilter_config`. All IIR filters are required
  // to be stable, since this factory does not perform an additional stability
  // check. The number of DoF-wise nominator/denominator coefficients in the
  // filter config must be smaller or equal than kMaxPrefilterOrder+1.
  static absl::StatusOr<JointCollectionPrefilter> Create(
      const intrinsic_proto::icon::JointCollectionPrefilterConfig&
          joint_collection_prefilter_config);

  // Initializes the IIR filters at a steady state around
  // `steady_state_position`. This method is real-time capable and can be called
  // at runtime with measured values. Returns an error status if the number of
  // positions in `steady_state_position` is not equivalent to the internal
  // number of DoF.
  RealtimeStatus InitializeInSteadyState(
      const eigenmath::VectorNd& steady_state_position);

  // Returns the dynamic filter response following `input`. This method is
  // real-time capable and should only be called once per real-time cycle.
  RealtimeStatusOr<eigenmath::VectorNd> ComputeControl(
      const eigenmath::VectorNd& input);

  int num_prefilters() const { return dof_prefilters_.size(); }

  const SisoPrefilter& GetDofPrefilter(int i) const {
    return dof_prefilters_[i];
  }

 private:
  explicit JointCollectionPrefilter(std::vector<SisoPrefilter> dof_prefilters)
      : dof_prefilters_(std::move(dof_prefilters)) {}

  std::vector<SisoPrefilter> dof_prefilters_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_DYNAMIC_PREFILTER_H_
