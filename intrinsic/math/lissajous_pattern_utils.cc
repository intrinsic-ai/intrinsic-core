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

#include "intrinsic/math/lissajous_pattern_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace lissajous_pattern_details {

absl::StatusOr<std::vector<double>>
GetFlatListOfBestSimpleWholeNumberRatioCandidate(int max_lissajous_integer) {
  if (max_lissajous_integer < kMinLissajousRatio ||
      max_lissajous_integer > kMaxLissajousRatio) {
    return absl::OutOfRangeError(absl::StrCat(
        "max_lissajous_integer is out of range, which is [", kMinLissajousRatio,
        ",", kMaxLissajousRatio, "] but got ", max_lissajous_integer));
  }

  std::vector<double> flattened_coeffs;
  for (int i = 0; i <= max_lissajous_integer - kMinLissajousRatio; ++i) {
    flattened_coeffs.insert(flattened_coeffs.end(),
                            kBestSimpleWholeNumberRatioCandidates[i].begin(),
                            kBestSimpleWholeNumberRatioCandidates[i].end());
  }

  // Remove the zero paddings and shrink to fit.
  flattened_coeffs.erase(
      std::remove(flattened_coeffs.begin(), flattened_coeffs.end(), 0.0),
      flattened_coeffs.end());
  flattened_coeffs.shrink_to_fit();

  return flattened_coeffs;
}

std::pair<int, int> ToFraction(double alpha, int min_denominator_candidate,
                               int max_denominator_candidate) {
  // We seek to restore `b / a = alpha`, where a is bounded to be in the
  // interval [min_denominator_candidate, max_denominator_candidate] and b at
  // maximum max_denominator_candidate+1.
  double min_error = std::numeric_limits<double>::max();
  int a_solution = 1;
  int b_solution = 0;
  for (int a_candidate = min_denominator_candidate;
       a_candidate <= max_denominator_candidate; ++a_candidate) {
    double b_candidate = std::round(alpha * a_candidate);
    double error_est = std::abs(b_candidate / a_candidate - alpha);
    if (error_est < min_error) {
      a_solution = a_candidate;
      b_solution = b_candidate;
      min_error = error_est;
    }
  }
  DVLOG(2) << "Estimated fraction " << b_solution << "/" << a_solution
           << " with error " << min_error;
  return std::make_pair(b_solution, a_solution);
}

absl::StatusOr<GeometricalLissajousPattern> OptimizeLissajousPatternByRatio(
    double x_size, double y_size, int max_lissajous_integer,
    double max_translational_velocity_per_dimension) {
  if ((max_lissajous_integer < kMinLissajousRatio) |
      (max_lissajous_integer > kMaxLissajousRatio)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "max_lissajous_integer must be integers between [", kMinLissajousRatio,
        ",", kMaxLissajousRatio, "], but got ", max_lissajous_integer));
  }
  if (max_translational_velocity_per_dimension < 1e-10) {
    return absl::InvalidArgumentError(
        "max_translational_velocity_per_dimension must be > 1e-10");
  }

  // Select the best simple whole number ratio candidates with its larger
  // integer up to max_lissajous_integer.
  INTR_ASSIGN_OR_RETURN(
      const auto best_simple_whole_number_ratios,
      GetFlatListOfBestSimpleWholeNumberRatioCandidate(max_lissajous_integer));
  const eigenmath::VectorXd best_simple_whole_number_ratios_eigen =
      Eigen::Map<const eigenmath::VectorXd>(
          best_simple_whole_number_ratios.data(),
          best_simple_whole_number_ratios.size());

  // Small workaround to avoid division by zero when computing aspect ratio.
  constexpr double kLargeButBoundedDouble = 1.0e12;
  const double aspect_ratio =
      std::clamp(y_size / (x_size + (1.0 / kLargeButBoundedDouble)), 0.0,
                 kLargeButBoundedDouble);

  // Find the pattern whose Lissajous ratio
  // x_frequency_rad_sec:y_frequency_rad_sec is closest to y_size:x_size, which
  // ensures a good evenness of the "pattern grid", since x number represents
  // the number of lobes in the x direction, and y number represents the number
  // of lobes in the y direction (only if simple whole number ratios), and we
  // want num_lobes_in_x:num_lobes_in_y as proportional to y_size:x_size as
  // possible.
  const eigenmath::VectorXd aspect_ratio_cost_fun =
      best_simple_whole_number_ratios_eigen.array() - aspect_ratio;
  Eigen::Index index_of_opt_element;
  aspect_ratio_cost_fun.cwiseAbs().minCoeff(&index_of_opt_element);

  const double best_ratio =
      best_simple_whole_number_ratios_eigen(index_of_opt_element);
  DVLOG(2) << "Best Lissajous ratio: " << best_ratio;

  // Convert the Lissajous ratio back to a fraction in the form [nominator /
  // denominator] using Euclidean algorithm. Derive Lissajous functions
  const auto best_whole_number_ratio =
      ToFraction(best_ratio, kMinLissajousRatio, max_lissajous_integer);
  const int x_integer = best_whole_number_ratio.first;
  const int y_integer = best_whole_number_ratio.second;
  const double x_amplitude = x_size / 2;
  const double y_amplitude = y_size / 2;

  // Scale x_integer and y_integer so that velocity is bounded by
  // max_velocity. Note : peak velocity throughout a pattern in each axis x
  // and y is their respective amplitude* lissajous_number.
  const double scale_by_x =
      max_translational_velocity_per_dimension / x_amplitude / x_integer;
  const double scale_by_y =
      max_translational_velocity_per_dimension / y_amplitude / y_integer;
  // Scale can be an arbitrarily large number, but we bound it to avoid
  // numerical issues.
  const double scale = std::min(std::min(scale_by_x, scale_by_y),
                                (std::numeric_limits<double>::max() /
                                 (std::max(x_integer, y_integer) + 1)));
  if (scale <= 1e-10) {
    return absl::InternalError("scaling factor must be > 1e-10");
  }
  const double x_frequency_rad_sec = static_cast<double>(x_integer) * scale;
  const double y_frequency_rad_sec = static_cast<double>(y_integer) * scale;
  const double period_seconds =
      (2.0 * M_PI / std::gcd(x_integer, y_integer)) / scale;

  DVLOG(2) << "Lissajous pattern: best_whole_number_ratio: " << x_integer << "/"
           << y_integer;
  DVLOG(2) << "Lissajous pattern: x_frequency_rad_sec: " << x_frequency_rad_sec;
  DVLOG(2) << "Lissajous pattern: y_frequency_rad_sec: " << y_frequency_rad_sec;
  DVLOG(2) << "Lissajous pattern: x_amplitude: " << x_amplitude;
  DVLOG(2) << "Lissajous pattern: y_amplitude: " << y_amplitude;
  DVLOG(2) << "Lissajous pattern: period_seconds: " << period_seconds;

  return GeometricalLissajousPattern{
      .x_frequency = x_frequency_rad_sec,
      .y_frequency = y_frequency_rad_sec,
      .x_amplitude = x_amplitude,
      .y_amplitude = y_amplitude,
      .period_seconds = period_seconds,
  };
}

eigenmath::Vector2d EvaluateLissajousPatternPosition(
    double time_since_start_sec, const GeometricalLissajousPattern& pattern) {
  eigenmath::Vector2d result;
  result.x() = pattern.x_amplitude *
               std::sin(pattern.x_frequency * time_since_start_sec);
  result.y() = pattern.y_amplitude *
               std::sin(pattern.y_frequency * time_since_start_sec);
  return result;
}

eigenmath::Vector2d EvaluateLissajousPatternVelocity(
    double time_since_start_sec, const GeometricalLissajousPattern& pattern) {
  eigenmath::Vector2d result;
  result.x() = pattern.x_frequency * pattern.x_amplitude *
               std::cos(pattern.x_frequency * time_since_start_sec);
  result.y() = pattern.y_frequency * pattern.y_amplitude *
               std::cos(pattern.y_frequency * time_since_start_sec);
  return result;
}

}  // namespace lissajous_pattern_details

}  // namespace intrinsic
