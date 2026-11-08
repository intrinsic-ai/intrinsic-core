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

#ifndef INTRINSIC_MATH_LISSAJOUS_PATTERN_UTILS_H_
#define INTRINSIC_MATH_LISSAJOUS_PATTERN_UTILS_H_

#include <array>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"

namespace intrinsic {

// The parameters that uniquely describe the 2D Lissajous pattern.
struct GeometricalLissajousPattern {
  double x_frequency;  // [rad/sec]
  double y_frequency;  // [rad/sec]
  double x_amplitude;
  double y_amplitude;
  double period_seconds;
};

namespace lissajous_pattern_details {

// Minimum supported x/y frequency ratio for 2-d Lissajous patterns.
constexpr double kMinLissajousRatio = 4;
// Maximum supported x/y frequency ratio for 2-d Lissajous patterns.
constexpr double kMaxLissajousRatio = 9;

// Table that defines a subset of Lissajous ratios which create Lissajous curves
// with the following properties:
// * ratios in the range [kMinLissajousOrder, kMaxLissajousOrder]. The minimum
// and maximum order have been determined empirically and have been found to
// result in patterns with reasonable density.
// * Ratios between odd integers are excluded, which is the condition for
// patterns being symmetric about the x and y axis and also do not have repeated
// paths.
// Below arrays are zero-padded to match necessary array dimensions.
static const std::array<std::array<double, 8>, 6>
    kBestSimpleWholeNumberRatioCandidates = {{
        /*For up to 4*/ {1. / 4., 3. / 4., 4. / 1., 4. / 3., 0., 0., 0., 0.0},
        /*For up to 5*/
        {1. / 5., 2. / 5., 4. / 5., 5. / 1., 5. / 2., 5. / 4., 0., 0.},
        /*For up to 6*/ {1. / 6., 5. / 6., 6. / 1., 6. / 5.},
        /*For up to 7*/
        {1. / 7., 2. / 7., 4. / 7., 6. / 7., 7. / 1., 7. / 2., 7. / 4.,
         7. / 6.},
        /*For up to 8*/
        {1. / 8., 3. / 8., 5. / 8., 7. / 8., 8. / 1., 8. / 3., 8. / 5.,
         8. / 7.},
        /*For up to 9*/
        {1. / 9., 2. / 9., 4. / 9., 8. / 9., 9. / 1., 9. / 2., 9. / 4.,
         9. / 8.},
    }};

// Returns a flat list of best whole number ratio candidates for up to
// `max_lissajous_integer`, excluding zeros. The flat list is extracted from the
// table `kBestSimpleWholeNumberRatioCandidates`.
absl::StatusOr<std::vector<double>>
GetFlatListOfBestSimpleWholeNumberRatioCandidate(int max_lissajous_integer);

// A brute force approach to reconstruct the original fraction from a floating
// point value `alpha_floating_point`, where the `min_denominator_candidate` and
// `max_denominator_candidate` (both integers) are known. This method
// does not scale to large numbers of max_denominator_candidate, use with care.
std::pair<int, int> ToFraction(double alpha_floating_point,
                               int min_denominator_candidate,
                               int max_denominator_candidate);

// Optimizes the Lissajous pattern by the ratio of the desired `x_size` and
// `y_size`. This function will always successfully generate a decently dense
// and evenly spread pattern bounded by `max_velocity` (if provided), which
//   1. has the start point at the center of the pattern,
//   2. has the pattern symmetric both about the x-axis and y-axis,
//   3. creates a pattern without internal overlap within a pattern cycle.
// The density of the pattern can be optionally controlled by
// `max_lissajous_integer`. The larger the number, the denser the pattern.
// Note that if `max_velocity` is not provided, velocity will be bounded by
// `max(x_size / 2, y_size / 2) * max_lissajous_integer`.
// The Lissajous path is based on the following functions for x/y displacements:
// x = x_amplitude * sin(x_frequency * t) = x_amplitude * sin(2 * pi / T_x * t)
// y = y_amplitude * sin(y_frequency * t) = y_amplitude * sin(2 * pi / T_y * t)
// x_size: The size of the rectangular area in the x direction, in meters.
// y_size: The size of the rectangular area in the y direction, in meters.
// max_translational_velocity_per_dimension: The maximum velocity along each
// axis x and y, in meters/second. Required to create patterns with reasonable
// time periods.
// max_lissajous_integer: The maximum Lissajous integer out of simple whole
// number ratio. This controls the density of the pattern generated and can
// range from 4 to 9. The higher this value, the denser. Returns the optimized
// Lissajous pattern that contains all the parameters for the pattern.
absl::StatusOr<GeometricalLissajousPattern> OptimizeLissajousPatternByRatio(
    double x_size, double y_size, int max_lissajous_integer,
    double max_translational_velocity_per_dimension);

// Evaluates the pattern position w.r.t. `time_since_start_sec`.
// x = x_amplitude * sin(x_frequency * t) = x_amplitude * sin(2*pi / T_x * t)
// y = y_amplitude * sin(y_frequency * t) = y_amplitude * sin(2*pi / T_y * t)
// Returns an eigenmath vector containing the lissajous pattern values for x and
// y direction.
eigenmath::Vector2d EvaluateLissajousPatternPosition(
    double time_since_start_sec, const GeometricalLissajousPattern& pattern);

// Evaluates the Lissajous pattern velocity wrt `time_since_start_sec`.
// vx = 2 * pi * x_amplitude / T_x * cos(2 * pi / T_x * t).
// vy = 2 * pi * y_amplitude / T_y * cos(2 * pi / T_y * t).
// Returns an eigenmath vector containing the lissajous pattern velocities for x
// and y direction.
eigenmath::Vector2d EvaluateLissajousPatternVelocity(
    double time_since_start_sec, const GeometricalLissajousPattern& pattern);

}  // namespace lissajous_pattern_details
}  // namespace intrinsic

#endif  // INTRINSIC_MATH_LISSAJOUS_PATTERN_UTILS_H_
