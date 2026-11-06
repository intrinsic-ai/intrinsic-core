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

#include <immintrin.h>

#include <cstdint>
#include <limits>

#include "absl/log/check.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/core/parallel.h"

namespace intrinsic::perception {

namespace {

// Store normal values from SIMD registers to 8 consecutive pixels in dst.
void StoreNormals(const __m256& avx_nx, const __m256& avx_ny,
                  const __m256& avx_nz, int32_t x, int32_t row,
                  Image<Normal32f>& dst) {
  float nx[8], ny[8], nz[8];
  _mm256_storeu_ps(nx, avx_nx);
  _mm256_storeu_ps(ny, avx_ny);
  _mm256_storeu_ps(nz, avx_nz);
  Normal32f::PixelType* out = &dst(x, row);
  for (int i = 0; i < 8; ++i) {
    out[i] = {nx[i], ny[i], nz[i]};
  }
}

/**
 * SIMD-optimized accumulator. Only considers values
 * if the depth difference to the center pixel is not larger than threshold.
 */
void Accum(const __m256& avx_i, const __m256& avx_j, const __m256& avx_thres,
           const __m256& avx_delta, __m256& avx_A0, __m256& avx_A1,
           __m256& avx_A2, __m256& avx_b0, __m256& avx_b1) {
  // Create a mask for checking nans.
  const __m256 avx_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
  // Abs difference from central point.
  const __m256 avx_dabs = _mm256_and_ps(avx_mask, avx_delta);
  // Puts 0's for not true or NaN.
  const __m256 avx_which =  // NOLINT
      _mm256_cmp_ps(avx_dabs, avx_thres, _CMP_LT_OQ);
  const __m256 avx_fi = _mm256_and_ps(avx_which, avx_i);
  const __m256 avx_fj = _mm256_and_ps(avx_which, avx_j);
  const __m256 avx_delta_masked = _mm256_and_ps(avx_which, avx_delta);
  avx_A0 += avx_fi * avx_i;
  avx_A1 += avx_fi * avx_j;
  avx_A2 += avx_fj * avx_j;
  avx_b0 += avx_fi * avx_delta_masked;
  avx_b1 += avx_fj * avx_delta_masked;
}

}  // namespace

Image<Normal32f> ComputeNormalsLeastSqr(const IntrinsicParams& intrinsic_params,
                                        const Image<Depth32f>& depth,
                                        float threshold, int32_t radius,
                                        int32_t step) {
  CHECK(depth.cols() >= 2 * radius + 1) << "Depth too small.";
  CHECK(depth.rows() >= 2 * radius + 1) << "Depth too small.";

  Image<Normal32f> dst(depth.dimensions(), Normal32f::Invalid());
  const int32_t w = depth.cols();
  const int32_t r = radius;

  const __m256 avx_inf = _mm256_set1_ps(std::numeric_limits<float>::infinity());
  const __m256 avx_nan =
      _mm256_set1_ps(std::numeric_limits<float>::quiet_NaN());
  const __m256 avx_zero = _mm256_setzero_ps();
  const __m256 avx_fx = _mm256_set1_ps(intrinsic_params.focal_length_x());
  const __m256 avx_fy = _mm256_set1_ps(intrinsic_params.focal_length_y());
  const __m256 avx_cx = _mm256_set1_ps(intrinsic_params.principal_point_x());
  const __m256 avx_cy = _mm256_set1_ps(intrinsic_params.principal_point_y());
  const __m256 avx_thres = _mm256_set1_ps(threshold);

  ParallelFor(r, depth.rows() - r, [&](int32_t row) {
    const __m256 avx_y = _mm256_set1_ps(row);

    // Process 8 pixels at a time.
    for (int32_t x = r; x < w - r - 8; x += 8) {
      const __m256 avx_d = _mm256_loadu_ps(&depth(x, row));
      __m256 avx_A0 = avx_zero;
      __m256 avx_A1 = avx_zero;
      __m256 avx_A2 = avx_zero;
      __m256 avx_b0 = avx_zero;
      __m256 avx_b1 = avx_zero;

      for (int32_t m = -r; m <= r; m += step) {
        const __m256 avx_m = _mm256_set1_ps(m);
        for (int32_t n = -r; n <= r; n += step) {
          const __m256 avx_n = _mm256_set1_ps(n);
          const __m256 avx_current = _mm256_loadu_ps(&depth(x + n, row + m));
          Accum(avx_n, avx_m, avx_thres, avx_current - avx_d, avx_A0, avx_A1,
                avx_A2, avx_b0, avx_b1);
        }
      }

      const __m256 avx_det = _mm256_rcp_ps(avx_A0 * avx_A2 - avx_A1 * avx_A1);
      const __m256 avx_cmp_inf1 =  // NOLINT
          _mm256_cmp_ps(avx_det, avx_inf, _CMP_EQ_OQ);
      const __m256 avx_div = _mm256_blendv_ps(avx_det, avx_nan, avx_cmp_inf1);

      const __m256 avx_ddx = (+avx_A2 * avx_b0 - avx_A1 * avx_b1) * avx_div;
      const __m256 avx_ddy = (-avx_A1 * avx_b0 + avx_A0 * avx_b1) * avx_div;

      const __m256 avx_x =
          _mm256_set_ps(x + 7, x + 6, x + 5, x + 4, x + 3, x + 2, x + 1, x + 0);

      __m256 avx_nx = avx_ddx * avx_fx;
      __m256 avx_ny = avx_ddy * avx_fy;
      __m256 avx_nz =
          -(avx_d + avx_ddx * (avx_x - avx_cx) + avx_ddy * (avx_y - avx_cy));

      const __m256 avx_sqrt =
          _mm256_rsqrt_ps(avx_nx * avx_nx + avx_ny * avx_ny + avx_nz * avx_nz);
      const __m256 avx_cmp_inf2 =  // NOLINT
          _mm256_cmp_ps(avx_sqrt, avx_inf, _CMP_EQ_OQ);
      const __m256 avx_fac = _mm256_blendv_ps(avx_sqrt, avx_nan, avx_cmp_inf2);

      avx_nx *= avx_fac;
      avx_ny *= avx_fac;
      avx_nz *= avx_fac;

      StoreNormals(avx_nx, avx_ny, avx_nz, x, row, dst);
    }
  });
  return dst;
}

}  // namespace intrinsic::perception
