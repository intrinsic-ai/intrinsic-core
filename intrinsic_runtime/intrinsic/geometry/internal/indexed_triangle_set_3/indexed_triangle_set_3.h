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

#ifndef INTRINSIC_GEOMETRY_INTERNAL_INDEXED_TRIANGLE_SET_3_INDEXED_TRIANGLE_SET_3_H_
#define INTRINSIC_GEOMETRY_INTERNAL_INDEXED_TRIANGLE_SET_3_INDEXED_TRIANGLE_SET_3_H_

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <vector>

#include "intrinsic/geometry/internal/kernel_3/exact_predicates_inexact_constructions_kernel.h"

namespace intrinsic::geo {
// 3-dimensional indexed triangle set.
// A set of triangles, each represented by a vector triples of indices pointing
// into a vector of points.
// This class replaces one-to-one geometry::Mesh.
// Specifically, there is no distinction between point and vertex.
// Points may be duplicated to represent vertex duplication, e.g. to encode
// surfaces that are just topologically manifold. This is also in line with the
// encoding used in formats such as obj. Model of:
// * IndexedFaceSet.
template <class Kernel>
class IndexedTriangleSet3 {
  // TODO(b/164234913): Consider to make index types typesafe.
  // e.g. similar to third_party/draco/src/draco/core/draco_index_type.h
 public:
  using Point = typename Kernel::Point_3;
  using PointIndex = std::size_t;
  using PointRange = std::vector<Point>;
  using Points = std::vector<Point>;

  using FaceIndex = std::size_t;
  using Triple = std::vector<PointIndex>;
  using FaceRange = std::vector<Triple>;
  using SizeType = FaceRange::size_type;
  using Triples = std::vector<Triple>;
  using VertexIndex = std::size_t;
  using VertexRange = std::vector<Point>;

  IndexedTriangleSet3() = default;
  template <class PointRange, class FaceRange>
  IndexedTriangleSet3(const PointRange& point_range,
                      const FaceRange& triple_range)
      : points_(point_range), triples_(triple_range) {}

  FaceRange& triples() { return triples_; }
  const FaceRange& triples() const { return triples_; }
  FaceRange& faces() { return triples_; }
  const FaceRange& faces() const { return triples_; }

  PointRange& points() { return points_; }
  const PointRange& points() const { return points_; }

  const PointRange& GetPointRange() const { return points_; }

  const FaceRange& TriangleForFaceRange() const { return triples_; }

  bool IsEmpty() const { return points().size() == 0 && faces().size() == 0; }

  PointIndex AddPoint(const Point& p) {
    points_.push_back(p);
    return points_.size() - 1;
  }

  IndexedTriangleSet3& Append(const IndexedTriangleSet3& other) {
    const PointIndex offset = points_.size();
    points_.insert(points_.end(), other.GetPointRange().begin(),
                   other.GetPointRange().end());
    std::transform(other.TriangleForFaceRange().begin(),
                   other.TriangleForFaceRange().end(),
                   std::back_inserter(triples_), [offset](Triple t) {
                     t[0] += offset;
                     t[1] += offset;
                     t[2] += offset;
                     return t;
                   });
    return *this;
  }

  static Triple MakeTriple(const PointIndex& i, const PointIndex& j,
                           const PointIndex& k) {
    return Triple{i, j, k};
  }

  FaceIndex AddTriple(PointIndex i, PointIndex j, PointIndex k) {
    triples_.push_back(MakeTriple(i, j, k));
    return triples_.size() - 1;
  }

  FaceIndex AddTriangle(const Point& a, const Point& b, const Point& c) {
    auto index = points_.size();
    points_.push_back(a);
    points_.push_back(b);
    points_.push_back(c);
    return AddTriple(index, index + 1, index + 2);
  }

  FaceIndex AddTriangle(const typename Kernel::Triangle_3& triangle) {
    return this->AddTriangle(triangle.vertex(0), triangle.vertex(1),
                             triangle.vertex(2));
  }

  FaceIndex AddTriangles(const std::vector<EpicTriangle3>& triangles) {
    points().reserve(points().size() + 3 * triangles.size());
    triples().reserve(triples().size() + triangles.size());
    for (const auto& t : triangles) {
      this->AddTriangle(t);
    }
    return triples().size() - 1;
  }

  void Clear() {
    points_.clear();
    triples_.clear();
  }

  bool IsEmpty() { return points().empty() && triples().empty(); }

 private:
  PointRange points_;
  FaceRange triples_;
};

using EpicIndexedTriangleSet3 = IndexedTriangleSet3<EpicKernel>;

}  // namespace intrinsic::geo
#endif  // INTRINSIC_GEOMETRY_INTERNAL_INDEXED_TRIANGLE_SET_3_INDEXED_TRIANGLE_SET_3_H_
