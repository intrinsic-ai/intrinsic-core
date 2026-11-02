#include "third_party/meshoptimizer/simplifier.h"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

TEST(MeshoptSimplifierTest, LinksWithStandardMeshoptimizerAndNamespace) {
  // 1. Function from vcacheoptimizer.cpp in @meshoptimizer:
  unsigned int indices[3] = {0, 1, 2};
  meshopt_optimizeVertexCache(indices, indices, 3, 3);
  EXPECT_EQ(indices[0], 0);

  // 2. Functions from upstream simplifier.cpp in @meshoptimizer:
  const float vertices[] = {
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
  };
  float scale = meshopt_simplifyScale(vertices, 3, sizeof(float) * 3);
  EXPECT_GT(scale, 0.0f);

  unsigned int destination[3];
  float error = 0.0f;
  size_t result = meshopt_simplify(destination, indices, 3, vertices, 3,
                                   sizeof(float) * 3, 3, 0.1f, 0, &error);
  EXPECT_EQ(result, 3);

  size_t sloppy_result = meshopt_simplifySloppy(
      destination, indices, 3, vertices, 3, sizeof(float) * 3, nullptr, 3,
      0.1f, &error);
  EXPECT_EQ(sloppy_result, 3);
}

TEST(MeshoptSimplifierTest, SimplifyCustomWithAcceptCallback) {
  const float vertices[] = {
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      1.0f, 1.0f, 0.0f,
  };
  const unsigned int indices[] = {
      0, 1, 2,
      2, 1, 3,
  };

  std::vector<unsigned int> destination(6);
  float result_error = 0.0f;

  size_t callback_invocations = 0;
  auto callback = [&](unsigned int /*v0*/, unsigned int /*v1*/,
                      const unsigned int* triangles,
                      size_t triangle_count) -> bool {
    ++callback_invocations;
    EXPECT_GT(triangle_count, 0);
    EXPECT_NE(triangles, nullptr);
    return true;  // Accept collapse
  };

  size_t result_count = intrinsic_thirdparty::meshopt_simplifyCustom(
      destination.data(), indices, 6, vertices, 4, sizeof(float) * 3,
      3, 1.0f, 0, &result_error, callback);

  EXPECT_GT(callback_invocations, 0);
  EXPECT_EQ(result_count, 3);
}

TEST(MeshoptSimplifierTest, SimplifyCustomWithRejectCallback) {
  const float vertices[] = {
      0.0f, 0.0f, 0.0f,
      1.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f,
      1.0f, 1.0f, 0.0f,
  };
  const unsigned int indices[] = {
      0, 1, 2,
      2, 1, 3,
  };

  std::vector<unsigned int> destination(6);
  float result_error = 0.0f;

  size_t callback_invocations = 0;
  auto reject_all = [&](unsigned int /*v0*/, unsigned int /*v1*/,
                        const unsigned int* /*triangles*/,
                        size_t /*triangle_count*/) -> bool {
    ++callback_invocations;
    return false;  // Reject all collapses
  };

  size_t result_count = intrinsic_thirdparty::meshopt_simplifyCustom(
      destination.data(), indices, 6, vertices, 4, sizeof(float) * 3,
      3, 1.0f, 0, &result_error, reject_all);

  EXPECT_GT(callback_invocations, 0);
  EXPECT_EQ(result_count, 6);
}
