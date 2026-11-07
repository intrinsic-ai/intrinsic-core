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

#include "intrinsic/perception/core/compute_normals_least_sqr.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/parallel.h"

namespace intrinsic::perception {

namespace {

template <int32_t kTile>
inline void ComputeNormalsTile(int32_t col, int32_t row, int32_t r,
                               int32_t step, float threshold, float fx,
                               float fy, float cx, float cy,
                               const Image<Depth32f>& depth,
                               Normal32f::PixelType* __restrict normal_row) {
  const float dy = static_cast<float>(row) - cy;
  const float* __restrict depth_row = depth.datarow(row);
  const float nan = std::numeric_limits<float>::quiet_NaN();

  alignas(32) float d[kTile];
  for (int32_t k = 0; k < kTile; ++k) {
    d[k] = depth_row[col + k];
  }

  alignas(32) float a0[kTile] = {};
  alignas(32) float a1[kTile] = {};
  alignas(32) float a2[kTile] = {};
  alignas(32) float b0[kTile] = {};
  alignas(32) float b1[kTile] = {};

  for (int32_t m = -r; m <= r; m += step) {
    const float j = static_cast<float>(m);
    const float jj = j * j;
    const float* __restrict nbr_row = depth.datarow(row + m) + col;
    alignas(32) float sum_i[kTile] = {};
    alignas(32) int32_t count_m[kTile] = {};
    alignas(32) float sum_delta[kTile] = {};

    for (int32_t n = -r; n <= r; n += step) {
      const float i = static_cast<float>(n);
      const float* __restrict nbr = nbr_row + n;
#ifdef __clang__
#pragma clang loop unroll(disable) vectorize_width(kTile)
#endif
      for (int32_t k = 0; k < kTile; ++k) {
        // Depth32f::Invalid() is quiet_NaN(). Under IEEE-754, if either d[k]
        // or nbr[k] is NaN, delta is NaN and std::abs(delta) < threshold
        // evaluates to false without needing a separate IsValid() check.
        const float delta = nbr[k] - d[k];
        const bool valid = std::abs(delta) < threshold;
        const float fi = valid ? i : 0.0f;
        const float fdelta = valid ? delta : 0.0f;
        a0[k] += fi * i;
        sum_i[k] += fi;
        count_m[k] += valid ? 1 : 0;
        sum_delta[k] += fdelta;
        b0[k] += fdelta * i;
      }
    }
#ifdef __clang__
#pragma clang loop unroll(disable) vectorize_width(kTile)
#endif
    for (int32_t k = 0; k < kTile; ++k) {
      a1[k] += sum_i[k] * j;
      a2[k] += static_cast<float>(count_m[k]) * jj;
      b1[k] += sum_delta[k] * j;
    }
  }

  // Compute and normalize the 2x2 least-squares normal across all kTile pixels.
  // When d[k] is NaN (invalid depth), a0..b1 are 0, so det == 0, inv_det is
  // NaN, and the resulting normal is {NaN, NaN, NaN} (Normal32f::Invalid()).
  alignas(32) float nx_arr[kTile];
  alignas(32) float ny_arr[kTile];
  alignas(32) float nz_arr[kTile];
  const float col_base = static_cast<float>(col) - cx;
#ifdef __clang__
#pragma clang loop unroll(disable) vectorize_width(kTile)
#endif
  for (int32_t k = 0; k < kTile; ++k) {
    const float det = a0[k] * a2[k] - a1[k] * a1[k];
    const float inv_det = (det != 0.0f) ? (1.0f / det) : nan;
    const float ddx = (+a2[k] * b0[k] - a1[k] * b1[k]) * inv_det;
    const float ddy = (-a1[k] * b0[k] + a0[k] * b1[k]) * inv_det;
    const float nx = fx * ddx;
    const float ny = fy * ddy;
    const float nz =
        -(d[k] + dy * ddy + (col_base + static_cast<float>(k)) * ddx);
    const float norm2 = nx * nx + ny * ny + nz * nz;
    const float inv_norm = (norm2 > 0.0f) ? (1.0f / std::sqrt(norm2)) : nan;
    nx_arr[k] = nx * inv_norm;
    ny_arr[k] = ny * inv_norm;
    nz_arr[k] = nz * inv_norm;
  }
  for (int32_t k = 0; k < kTile; ++k) {
    normal_row[col + k] = {nx_arr[k], ny_arr[k], nz_arr[k]};
  }
}

}  // namespace

Image<Normal32f> ComputeNormalsLeastSqr(const IntrinsicParams& intrinsic_params,
                                        const Image<Depth32f>& depth,
                                        float threshold, int32_t radius,
                                        int32_t step) {
  CHECK_GE(radius, 0);
  CHECK_GT(step, 0);
  CHECK_GE(depth.cols(), 2 * radius + 1) << "Depth too small.";
  CHECK_GE(depth.rows(), 2 * radius + 1) << "Depth too small.";

  Image<Normal32f> dst(depth.dimensions(), Normal32f::Invalid());
  const int32_t w = depth.cols();
  const int32_t r = radius;

  const float fx = intrinsic_params.focal_length_x();
  const float fy = intrinsic_params.focal_length_y();
  const float cx = intrinsic_params.principal_point_x();
  const float cy = intrinsic_params.principal_point_y();

  constexpr int32_t kTile = 8;

  ParallelFor(r, depth.rows() - r, [&](int32_t row) {
    const float* __restrict depth_row = depth.datarow(row);
    Normal32f::PixelType* __restrict normal_row = dst.datarow(row);

    int32_t col = r;
    // Process kTile adjacent columns at a time so the compiler auto-vectorizes
    // the innermost k loop with unit-stride SIMD loads (AVX/AVX2 on x86_64,
    // NEON on arm64).
    for (; col <= w - r - kTile; col += kTile) {
      ComputeNormalsTile<kTile>(col, row, r, step, threshold, fx, fy, cx, cy,
                                depth, normal_row);
    }

    // Scalar tail for remaining columns near the right border.
    for (; col < w - r; ++col) {
      if (Depth32f::IsValid(depth_row[col])) {
        ComputeNormalsTile<1>(col, row, r, step, threshold, fx, fy, cx, cy,
                              depth, normal_row);
      }
    }
  });

  return dst;
}

}  // namespace intrinsic::perception
