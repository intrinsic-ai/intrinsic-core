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

#ifndef INTRINSIC_GEOMETRY_COMPATIBILITY_IO_H_
#define INTRINSIC_GEOMETRY_COMPATIBILITY_IO_H_

#include <optional>

#include "absl/status/statusor.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/renderable.h"
#include "intrinsic/geometry/proto/geometry.pb.h"
#include "intrinsic/geometry/proto/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/lazy_exact_geometry.pb.h"
#include "intrinsic/geometry/proto/primitives.pb.h"
#include "intrinsic/geometry/proto/renderable.pb.h"

namespace intrinsic::geometry_compatibility {

// ## IO:PROTO
absl::StatusOr<intrinsic_proto::geometry::Geometry> ToProto(
    const Geometry& geo);
absl::StatusOr<Geometry> ToGeometry(
    const intrinsic_proto::geometry::Geometry& proto,
    const intrinsic::geo::GeometryOptions& options =
        intrinsic::geo::GeometryOptions::Default(),
    const std::optional<intrinsic_proto::geometry::GeometryStorageRefs>& refs =
        std::nullopt);

absl::StatusOr<intrinsic_proto::geometry::LazyExactGeometry> ToProto(
    const ExactGeometry& geo);

absl::StatusOr<ExactGeometry> ToExactGeometry(
    const intrinsic_proto::geometry::LazyExactGeometry& proto,
    intrinsic::geo::GeometryOptions options =
        intrinsic::geo::GeometryOptions::Default());

absl::StatusOr<intrinsic_proto::geometry::Renderable> ToProto(
    const Renderable& geo);

// ## IO:PROTO:Primitives
absl::StatusOr<Geometry> ToGeometry(
    const ::google::protobuf::RepeatedPtrField<
        intrinsic_proto::geometry::PrimitiveShape>& protos);

// Returns the PrimitiveShapeSet proto representing this Geometry. If the
// geometry instance contains a type that is not supported, then this function
// will return an error. If there are no primitives represented in this geometry
// instance, it will return an empty proto.
// TODO(b/417296237): Delete this once we migrate to v1 based geometry storage
absl::StatusOr<intrinsic_proto::geometry::PrimitiveShapeSet>
ToPrimitiveSetProto(const Geometry& geo);

}  // namespace intrinsic::geometry_compatibility

#endif  // INTRINSIC_GEOMETRY_COMPATIBILITY_IO_H_
