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

#ifndef INTRINSIC_MATH_SIGNALS_IIR_FILT_H_
#define INTRINSIC_MATH_SIGNALS_IIR_FILT_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/proto/iir_filter_config.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

static constexpr double kSmallValue = 1e-8;

// Generic digital IIR filter coefficients for a given filter with `MaxOrder`.
// Filters can have smaller orders, `MaxOrder` ensures realtime-compatibility.
// We follow the naming convention from Wikipedia, where `b` are the numerator
// coefficients in the transfer function, and `a` are denominator coefficients.
// See https://en.wikipedia.org/wiki/Infinite_impulse_response. Implementations
// in this header assume that the filter coefficients are normalized, e.g. the
// first coefficient in a is a_0=1.0. A filter of Order N has N+1 coefficients.
template <size_t MaxOrder>
struct IirFilterCoefficients {
  std::array<double, MaxOrder + 1> a;
  std::array<double, MaxOrder + 1> b;
};

namespace iir_filter_details {

template <size_t MaxOrder>
inline std::array<double, MaxOrder + 1> CumulativeSum(
    absl::Span<const double> v) {
  QCHECK_EQ(v.size(), MaxOrder + 1);
  std::array<double, MaxOrder + 1> sum;
  absl::c_partial_sum(v, sum.begin());
  return sum;
}

// Returns `si`, a factor needed for initializing the internal state of the
// filter, for further reference see "Determining the Initial States in
// Forward-Backward Filtering" by F. Gustafsson. `si` has size `MaxOrder`.
template <size_t MaxOrder>
inline std::array<double, MaxOrder> InitializeSi(
    const IirFilterCoefficients<MaxOrder>& coeffs) {
  // Compute IIR filter DC-gain (amplification at constant input values).
  const double dc_gain =
      absl::c_accumulate(coeffs.b, 0.0) / absl::c_accumulate(coeffs.a, 0.0);

  // If dc_gain is unbounded, the filter is in fact a FIR instead of an IIR
  // filter and it is stateless. To cover this special case, we just
  // set state coefficients to zero.
  if (!std::isfinite(std::abs(dc_gain))) {
    // Initialize with empty vector if an initialization is not possible
    return std::array<double, MaxOrder>();  // zero initialization.
  }

  std::array<double, MaxOrder + 1> lv;
  for (int i = 0; i < MaxOrder + 1; ++i) {
    lv.at(i) = coeffs.b.at(i) - dc_gain * coeffs.a.at(i);
  }
  absl::c_reverse(lv);
  std::array<double, MaxOrder + 1> cumsum = CumulativeSum<MaxOrder>(lv);
  absl::c_reverse(cumsum);

  std::array<double, MaxOrder> si;
  std::copy(std::next(cumsum.cbegin()), cumsum.cend(),
            si.begin());  // remove first element.
  return si;
}

// Returns the order of the transfer function, or an error status in case the
// filter has zero non-zero coefficients.
template <size_t MaxOrder>
int ComputeOrder(const IirFilterCoefficients<MaxOrder>& filter_coefficients) {
  for (int order = MaxOrder; order >= 0; --order) {
    if (!intrinsic::AlmostEquals(filter_coefficients.a.at(order), 0.0) ||
        !intrinsic::AlmostEquals(filter_coefficients.b.at(order), 0.0)) {
      return order;
    }
  }
  // This code should never be reached.
  CHECK(false)
      << "Filter has zero non-zero coefficients, transfer function not "
         "normalized.";
}

}  // namespace iir_filter_details

// An Infinite-Impulse-Response filter implementation intended for use at
// realtime. Class performs the filtering steps in RT-safe way, and needs to be
// initialized with previously designed filter coefficients.
// TODO(giftthaler): add a method for stability checking.
template <typename T, size_t MaxOrder>
class IirFilter {
 public:
  // Creates a discrete-time IIR Filter from coefficients `b` and `a`. We follow
  // the naming convention from Wikipedia, where `b` are the numerator
  // coefficients in the transfer function, and `a` are denominator
  // coefficients. See https://en.wikipedia.org/wiki/Infinite_impulse_response.
  // Implementations in this header assume that the filter coefficients are
  // normalized, e.g. the first coefficient in a is a_0=1.0. `a` and `b` must be
  // of equal length, which must not exceed `MaxOrder`+1.
  static absl::StatusOr<IirFilter> Create(absl::Span<const double> b,
                                          absl::Span<const double> a) {
    if (a.empty()) {
      return absl::InvalidArgumentError(
          "IIR filter coefficients `a` must not be empty");
    }
    if (b.empty()) {
      return absl::InvalidArgumentError(
          "IIR filter coefficients `b` must not be empty");
    }
    if (a.size() != b.size()) {
      return absl::InvalidArgumentError(
          "IIR filter coefficients `a` and `b` must be of equal length");
    }
    if (a.size() > MaxOrder + 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("Size of IIR filter coefficients `a` must not exceed ",
                       MaxOrder + 1));
    }
    // Default-initialize filter_coefficients with zero.
    IirFilterCoefficients<MaxOrder> filter_coefficients;
    filter_coefficients.a.fill(0.0);
    filter_coefficients.b.fill(0.0);

    std::copy(a.begin(), a.end(), filter_coefficients.a.begin());
    std::copy(b.begin(), b.end(), filter_coefficients.b.begin());

    return Create(filter_coefficients);
  }

  static absl::StatusOr<IirFilter> Create(
      intrinsic_proto::DiscreteTimeIirFilterConfig config_proto) {
    return Create(config_proto.coefficients_b(), config_proto.coefficients_a());
  }

  // Returns an IIR filter instance using `filter_coefficients` which are
  // normalized and of size MaxOrder. Returns kInvalidArgument in case the
  // coefficients are not normalized.
  static absl::StatusOr<IirFilter> Create(
      IirFilterCoefficients<MaxOrder> filter_coefficients) {
    static_assert(MaxOrder > 0, "IIrFilter - MaxOrder must be > 0");

    if (std::abs(filter_coefficients.a.front() - 1.0) > kSmallValue) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Filter coefficients are not normalized, expected first filter "
          "coefficient a to be 1.0, but got ",
          filter_coefficients.a.front()));
    }

    return IirFilter(std::move(filter_coefficients));
  }

  IirFilter() = delete;

  void Initialize(const T& x_initial) {
    // Compute initial condition which minimizes transient at the start of the
    // signal.
    std::array<double, MaxOrder> si =
        iir_filter_details::InitializeSi<MaxOrder>(filter_coefficients_);
    for (size_t i = 0; i < MaxOrder; ++i) {
      filter_state_.at(i) = si.at(i) * x_initial;
    }

    initialized_ = true;
  }

  // Directly initializes the internal state of the Direct Form II filter.
  // `initial_filter_state` must be of size `MaxOrder`. Supported only at
  // non-realtime since it needs to make copies of non trivially constructible
  // types provided as `T`.
  absl::Status Initialize(absl::Span<const T> initial_filter_state) {
    if (initial_filter_state.size() != MaxOrder) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Initial filter state must be of size 'MaxOrder+1', but got ",
          initial_filter_state.size()));
    }
    std::copy(initial_filter_state.begin(), initial_filter_state.end(),
              filter_state_.begin());
    initialized_ = true;
    return absl::OkStatus();
  }

  // Evaluates IIR filter difference equation, computes filtered output of type
  // `T` based on a input `x_in` and subject to `filter_coefficients_`. Assumes
  // a Direct-Form II filter realization, uses and updates the corresponding
  // `filter_state_`.
  icon::RealtimeStatusOr<T> Step(const T& x_in) {
    if (!initialized_) {
      return icon::FailedPreconditionError(
          "Filter not initialized, call Initialize() before Step()");
    }

    // Compute filtered output "y".
    // Implement the Direct Form II difference equation, see e.g. the overview
    // in https://www.dsprelated.com/freebooks/filters/Four_Direct_Forms.html
    T y = filter_coefficients_.b.at(0) * x_in + filter_state_.at(0);

    // Update filter state.
    for (size_t i = 1; i < MaxOrder + 1; ++i) {
      filter_state_.at(i - 1) = filter_coefficients_.b.at(i) * x_in -
                                filter_coefficients_.a.at(i) * y;
      if (i < MaxOrder) filter_state_.at(i - 1) += filter_state_.at(i);
    }
    return y;
  }

  // Returns the actual order of the transfer function (returns the order of the
  // highest-order non-zero coefficient). Returns an error status if the
  // transfer function has zero non-zero coefficients.
  int Order() const {
    return iir_filter_details::ComputeOrder(filter_coefficients_);
  }

  const IirFilterCoefficients<MaxOrder>& GetFilterCoefficients() const {
    return filter_coefficients_;
  }

 private:
  explicit IirFilter(IirFilterCoefficients<MaxOrder> filter_coefficients)
      : filter_coefficients_(filter_coefficients), filter_state_() {}
  IirFilterCoefficients<MaxOrder> filter_coefficients_;
  // Filter state, denoted as "v" for Direct Form II in
  // https://www.dsprelated.com/freebooks/filters/Four_Direct_Forms.html
  std::array<T, MaxOrder> filter_state_;
  bool initialized_ = false;
};

namespace iir_filter_details {

// Filters discrete-time (digital) signal `data` of type `T` using
// `filter_coefficients`, subject to a given `initial_filter_state`. It is
// assumed that the provided filter coefficients are normalized. The
// realization of the filter follows the Direct-Form II, hence the size of
// `initial_filter_state` must be equal to the filter MaxOrder. This method only
// performs a single forward pass, the resulting signal will be subject to phase
// shift. Returns filtered data, or a non-ok status in case of errors. Only for
// use in non-realtime.
template <typename T, size_t MaxOrder>
absl::StatusOr<std::vector<T>> Filter(
    const IirFilterCoefficients<MaxOrder>& filter_coefficients,
    absl::Span<const T> data, absl::Span<const T> initial_filter_state) {
  if (data.empty()) {
    return std::vector<T>();
  }

  INTR_ASSIGN_OR_RETURN(auto filter,
                        (IirFilter<T, MaxOrder>::Create(filter_coefficients)));

  INTR_RETURN_IF_ERROR(filter.Initialize(initial_filter_state));

  // Perform filtering step.
  std::vector<T> filtered_data;
  filtered_data.reserve(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(T y, filter.Step(data.at(i)));
    filtered_data.push_back(y);
  }

  return filtered_data;
}

}  // namespace iir_filter_details

// Filters discrete-time (digital) signal `data` with underlying datatype `T`
// subject to digital `filter_coefficients`. It is assumed that the provided
// filter coefficients are normalized. This method only performs a single
// forward pass, the resulting signal will be subject to phase shift. First data
// point is used for filter state initialization in order to minimize
// transients. Returns filtered signal or a non-ok status in case of errors.
template <typename T, size_t MaxOrder>
absl::StatusOr<std::vector<T>> Filter(
    const IirFilterCoefficients<MaxOrder>& filter_coefficients,
    absl::Span<const T> data) {
  if (data.empty()) {
    return std::vector<T>();
  }

  INTR_ASSIGN_OR_RETURN(auto filter,
                        (IirFilter<T, MaxOrder>::Create(filter_coefficients)));

  filter.Initialize(data.front());

  // Perform filtering step.
  std::vector<T> filtered_data;
  filtered_data.reserve(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    INTRINSIC_RT_ASSIGN_OR_RETURN(T y, filter.Step(data.at(i)));
    filtered_data.push_back(y);
  }

  return filtered_data;
}

struct StateSpaceModel {
  eigenmath::MatrixNd a_matrix;
  eigenmath::MatrixNd b_matrix;
  eigenmath::MatrixNd c_matrix;
  eigenmath::MatrixNd d_matrix;
};

// Returns a single-input-single-output disrete-time linear state space model
// equivalent to the transfer function coefficients `coeffs`.
//
// Converts the filter "normal" coefficients (b0, b1, ..., bN) and (a0, a1, ...,
// aN), where a0 = 1.0, and which are equivalent to the SISO difference equation
// y[k]+a1*y[k−1]+a2*y[k−2]+...+aN*y[k−N] =
// b0*u[k]+b1*u[k−1]+b2*u[k−2]+...+bN*u[k−N]
// to one possible discrete time state space representation. The resulting
// state-space realization will be of dimension `Order`.
// TODO(giftthaler): this should return a state space model with the minimum
// possible order.
template <size_t MaxOrder>
absl::StatusOr<StateSpaceModel> ToLinearStateSpaceModel(
    const IirFilterCoefficients<MaxOrder>& coeffs) {
  static_assert(MaxOrder > 0, "IirFilter - Order must be > 0.");

  if (iir_filter_details::ComputeOrder(coeffs) + 1 >
      eigenmath::MAX_EIGEN_VECTOR_SIZE) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot execute state-space conversion, since the maximum "
                     "size of a VectorNd is currently limited to ",
                     eigenmath::MAX_EIGEN_VECTOR_SIZE));
  }

  if (std::abs(coeffs.a.front() - 1.0) > kSmallValue) {
    return absl::InvalidArgumentError(
        absl::StrCat("Coefficients are not normalized, expected first "
                     "coefficient a to be 1.0, but got ",
                     coeffs.a.front(), "."));
  }

  StateSpaceModel sys;

  // State matrix.
  sys.a_matrix = eigenmath::MatrixNd::Zero(MaxOrder, MaxOrder);
  sys.a_matrix.bottomLeftCorner(MaxOrder - 1, MaxOrder - 1).setIdentity();
  Eigen::Map<const eigenmath::VectorXd> a_map(coeffs.a.data(), MaxOrder + 1, 1);
  sys.a_matrix.topRows(1) = -a_map.tail(MaxOrder).transpose();

  // Input matrix.
  sys.b_matrix = eigenmath::MatrixNd::Zero(MaxOrder, 1);
  sys.b_matrix(0, 0) = 1.0;

  // Output / observation matrix.
  sys.c_matrix = eigenmath::MatrixNd::Zero(1, MaxOrder);
  Eigen::Map<const eigenmath::VectorXd> b_map(coeffs.b.data(), MaxOrder + 1, 1);
  sys.c_matrix = b_map.tail(MaxOrder).transpose();

  // Direct action (feedthrough).
  sys.d_matrix.resize(1, 1);
  sys.d_matrix(0, 0) = coeffs.b.front();

  return sys;
}

}  // namespace intrinsic

#endif  // INTRINSIC_MATH_SIGNALS_IIR_FILT_H_
