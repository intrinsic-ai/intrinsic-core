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

#include "intrinsic/world/service/objects/world_comparator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/map.h"
#include "google/protobuf/util/message_differencer.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/matrix.pb.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/ppr_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "intrinsic/world/proto/simulation_component.pb.h"

namespace intrinsic {
namespace object_world {
namespace {

class MatrixFieldComparator
    : public google::protobuf::util::SimpleFieldComparator {
 public:
  using ComparisonResult =
      google::protobuf::util::FieldComparator::ComparisonResult;

  MatrixFieldComparator() : google::protobuf::util::SimpleFieldComparator() {}

  ComparisonResult Compare(
      const google::protobuf::Message& message_1,
      const google::protobuf::Message& message_2,
      const google::protobuf::FieldDescriptor* field, int index_1, int index_2,
      const google::protobuf::util::FieldContext* context) override {
    if (field->cpp_type() ==
        google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      const google::protobuf::Message& sub_msg_1 =
          (field->is_repeated())
              ? message_1.GetReflection()->GetRepeatedMessage(message_1, field,
                                                              index_1)
              : message_1.GetReflection()->GetMessage(message_1, field);

      const google::protobuf::Message& sub_msg_2 =
          (field->is_repeated())
              ? message_2.GetReflection()->GetRepeatedMessage(message_2, field,
                                                              index_2)
              : message_2.GetReflection()->GetMessage(message_2, field);

      if (sub_msg_1.GetDescriptor()->full_name() == "intrinsic_proto.Matrixd" &&
          sub_msg_2.GetDescriptor()->full_name() == "intrinsic_proto.Matrixd") {
        intrinsic_proto::Matrixd m1, m2;
        m1.CopyFrom(sub_msg_1);
        m2.CopyFrom(sub_msg_2);

        // Matrix protos are considered identical if they're identity or unset.
        const bool identical = [&] {
          if (IsUnset(m1)) {
            return IsUnset(m2) || IsIdentity(m2);
          } else if (IsIdentity(m1)) {
            return IsUnset(m2) || (IsIdentity(m2) && m1.rows() == m2.rows());
          }

          // m1 is not unset nor identity, so match it against m2 numerically.
          auto mat1 = FromProto(m1), mat2 = FromProto(m2);
          return mat1.ok() && mat2.ok() && mat1->isApprox(*mat2);
        }();
        return identical ? ComparisonResult::SAME : ComparisonResult::DIFFERENT;
      }
    }
    return SimpleCompare(message_1, message_2, field, index_1, index_2,
                         context);
  }

 private:
  static bool IsUnset(const intrinsic_proto::Matrixd& m) {
    return m.rows() == 0 && m.cols() == 0 && m.values().empty();
  }

  static bool IsIdentity(const intrinsic_proto::Matrixd& mat_proto) {
    absl::StatusOr<intrinsic::eigenmath::MatrixXd> m = FromProto(mat_proto);
    return m.ok() && m->rows() > 0 && m->rows() == m->cols() && m->isIdentity();
  }
};

using ::intrinsic_proto::world::CompareWorldsRequest;
using ::intrinsic_proto::world::Entity;
using ::intrinsic_proto::world::Frame;
using ::intrinsic_proto::world::IdAndName;
using ::intrinsic_proto::world::KinematicObjectComponent;
using ::intrinsic_proto::world::NamedJointConfiguration;
using ::intrinsic_proto::world::Object;
using ::intrinsic_proto::world::ObjectComponent;
using ::intrinsic_proto::world::ObjectReference;

template <typename T>
std::string CompareStringMap(
    absl::string_view map_entry_name,
    google::protobuf::Map<std::string, T> map1,
    google::protobuf::Map<std::string, T> map2,
    std::function<std::string(const T&, const T&)> internal_comparator) {
  std::vector<std::string> unique_first_entry_names;
  for (const auto& [name, _] : map1) {
    if (!map2.contains(name)) {
      unique_first_entry_names.push_back(name);
    }
  }
  std::vector<std::string> unique_second_entry_names;
  for (const auto& [name, _] : map2) {
    if (!map1.contains(name)) {
      unique_second_entry_names.push_back(name);
    }
  }

  if (!unique_first_entry_names.empty() || !unique_second_entry_names.empty()) {
    const std::string contains_string =
        absl::StrFormat("contains %s for key%s [%s]", map_entry_name,
                        unique_first_entry_names.size() == 1 ? "" : "s",
                        absl::StrJoin(unique_first_entry_names, ", "));

    const std::string missing_string =
        absl::StrFormat("is missing %s for key%s [%s]", map_entry_name,
                        unique_second_entry_names.size() == 1 ? "" : "s",
                        absl::StrJoin(unique_second_entry_names, ", "));

    return !unique_first_entry_names.empty() &&
                   !unique_second_entry_names.empty()
               ? absl::StrFormat("%s, and %s", contains_string, missing_string)
               : absl::StrFormat("%s", unique_first_entry_names.empty()
                                           ? missing_string
                                           : contains_string);
  }

  for (const auto& [name, entry1] : map1) {
    const T& entry2 = map2.at(name);
    std::string error = internal_comparator(entry1, entry2);
    if (!error.empty()) {
      return absl::StrFormat("at entry '%s' %s", name, error);
    }
  }

  return "";
}

template <typename T>
bool ProtosEqual(const T& proto_a, const T& proto_b) {
  google::protobuf::util::MessageDifferencer differencer;
  // Use a custom comparator here to match identity matrices to unset ones.
  MatrixFieldComparator comparator;
  comparator.set_treat_nan_as_equal(true);
  comparator.set_float_comparison(
      google::protobuf::util::SimpleFieldComparator::APPROXIMATE);
  differencer.set_field_comparator(&comparator);
  differencer.set_message_field_comparison(
      google::protobuf::util::MessageDifferencer::EQUIVALENT);
  return differencer.Compare(proto_a, proto_b);
}

bool PoseProtosApproximatelyEqual(const intrinsic_proto::Pose& pose_proto_a,
                                  const intrinsic_proto::Pose& pose_proto_b) {
  if (ProtosEqual(pose_proto_a, pose_proto_b)) {
    return true;
  }

  absl::StatusOr<Pose3d> pose_a = FromProto(pose_proto_a);
  absl::StatusOr<Pose3d> pose_b = FromProto(pose_proto_b);
  return pose_a.ok() && pose_b.ok() && pose_a->isApprox(*pose_b, 0.0005);
}

absl::StatusOr<std::string> ComputeTransformBetweenPoseProtos(
    const intrinsic_proto::Pose& pose_proto_a,
    const intrinsic_proto::Pose& pose_proto_b) {
  INTR_ASSIGN_OR_RETURN(Pose3d pose_a, FromProto(pose_proto_a),
                        _.SetCode(absl::StatusCode::kInvalidArgument)
                            << "Failed to parse first pose proto");
  INTR_ASSIGN_OR_RETURN(Pose3d pose_b, FromProto(pose_proto_b),
                        _.SetCode(absl::StatusCode::kInvalidArgument)
                            << "Failed to parse second pose proto");

  const Pose3d a_t_b = pose_b * pose_a.inverse();
  return absl::StrJoin(
      {a_t_b.translation().x(), a_t_b.translation().y(),
       a_t_b.translation().z(), a_t_b.quaternion().x(), a_t_b.quaternion().y(),
       a_t_b.quaternion().z(), a_t_b.quaternion().w()},
      ",");
}

// Used to access string member of containers member values (e.g. protos).
template <typename C>
using AccessorHelper =
    absl::AnyInvocable<std::string(const typename C::value_type&)>;

// If there exists an x in c1 such that f(x) != f(y) for all y in c2, then
// returns f(x). Otherwise returns nullopt.
template <typename C>
absl::StatusOr<std::optional<std::string>> IsMissing(const C& c1, const C& c2,
                                                     AccessorHelper<C>& f) {
  WorldHashSet<std::string> c2fys;
  for (const auto& y : c2) {
    std::string fy = f(y);
    if (!c2fys.insert(fy).second) {
      return absl::FailedPreconditionError(
          absl::StrFormat("Found duplicate: %s", fy));
    }
  }
  for (const auto& x : c1) {
    std::string fx = f(x);
    if (!c2fys.contains(fx)) {
      return fx;
    }
  }

  return std::nullopt;
}

// If a value x exists in 'changed' that is not in 'base' under the mapped view
// via 'f', returns absl:StrFormat("contains " + format, f(x));
//
// Alternatively,
//
// If a value x exists in 'base' that is not in 'changed' under the mapped view
// via 'f', returns absl:StrFormat("missing " + format, f(x));
//
// Otherwise returns nullopt.
template <typename C>
absl::StatusOr<std::optional<std::string>> FindMismatch(
    const C& base, const C& changed, const absl::string_view in_format,
    AccessorHelper<C> f) {
  auto format = absl::ParsedFormat<'s'>::New(in_format);
  INTR_ASSIGN_OR_RETURN(std::optional<std::string> mismatch,
                        IsMissing(base, changed, f));
  if (mismatch.has_value()) {
    return "Missing expected " + absl::StrFormat(*format, mismatch.value());
  }

  INTR_ASSIGN_OR_RETURN(mismatch, IsMissing(changed, base, f));
  if (mismatch.has_value()) {
    return "Contains unexpected " + absl::StrFormat(*format, mismatch.value());
  }

  return std::nullopt;
}

absl::StatusOr<std::string> FramesEqual(
    const Frame& first_frame, const Frame& second_frame, bool compare_poses,
    CompareWorldsRequest::IdentificationMode id_mode) {
  if (first_frame.name() != second_frame.name()) {
    return absl::StrFormat("Names differ: '%s' vs '%s'", first_frame.name(),
                           second_frame.name());
  }
  if (id_mode == CompareWorldsRequest::BY_NAME) {
    if (first_frame.object().name() != second_frame.object().name()) {
      return absl::StrFormat("Parent object names differ: '%s' vs '%s'",
                             first_frame.object().name(),
                             second_frame.object().name());
    }
  } else if (!ProtosEqual(first_frame.object(), second_frame.object())) {
    return absl::StrFormat(
        "Parent objects differ: %s:'%s' vs %s:'%s')", first_frame.object().id(),
        first_frame.object().name(), second_frame.object().id(),
        second_frame.object().name());
  }
  if (first_frame.parent_frame().name() != second_frame.parent_frame().name()) {
    return absl::StrFormat("Parent frames differ: '%s' vs '%s'",
                           first_frame.parent_frame().name(),
                           second_frame.parent_frame().name());
  }

  INTR_ASSIGN_OR_RETURN(
      std::optional<std::string> mismatch,
      FindMismatch(first_frame.child_frames(), second_frame.child_frames(),
                   "child frame '%s'",
                   [](const IdAndName& idname) { return idname.name(); }));
  if (mismatch.has_value()) return mismatch.value();

  if (compare_poses &&
      !PoseProtosApproximatelyEqual(first_frame.parent_t_this(),
                                    second_frame.parent_t_this())) {
    return "Poses differ";
  }

  if (first_frame.is_attachment_frame() &&
      !second_frame.is_attachment_frame()) {
    return "Isn't attachment frame";
  } else if (!first_frame.is_attachment_frame() &&
             second_frame.is_attachment_frame()) {
    return "Is attachment frame";
  }

  return "";
}

absl::StatusOr<std::string> ObjectsEqual(
    const Object& first_object, const Object& second_object,
    CompareWorldsRequest::IdentificationMode id_mode) {
  const bool has_entities =
      !first_object.entities().empty() || !second_object.entities().empty();
  if (id_mode == CompareWorldsRequest::BY_ID &&
      first_object.id() != second_object.id()) {
    return absl::StrFormat("Ids differ: '%s' vs '%s'", first_object.id(),
                           second_object.id());
  }
  if (first_object.name() != second_object.name()) {
    return absl::StrFormat("Names differ: '%s' vs '%s'", first_object.name(),
                           second_object.name());
  }
  if (id_mode == CompareWorldsRequest::BY_ID &&
      first_object.parent().id() != second_object.parent().id()) {
    return absl::StrFormat("Parent object ids differ: '%s' vs '%s'",
                           first_object.parent().id(),
                           second_object.parent().id());
  }
  if (first_object.parent().name() != second_object.parent().name()) {
    return absl::StrFormat("Parent object names differ: '%s' vs '%s'",
                           first_object.parent().name(),
                           second_object.parent().name());
  }

  // Compare object components independently
  const ObjectComponent& first_component = first_object.object_component();
  const ObjectComponent& second_component = second_object.object_component();

  // If we're comparing by entities, then use their poses for comparison. But
  // if we're not, then use the pose in the object component.
  if (!has_entities) {
    if (!PoseProtosApproximatelyEqual(first_component.parent_t_this(),
                                      second_component.parent_t_this())) {
      INTR_ASSIGN_OR_RETURN(
          std::string transform,
          ComputeTransformBetweenPoseProtos(first_component.parent_t_this(),
                                            second_component.parent_t_this()),
          _.SetCode(absl::StatusCode::kInvalidArgument)
              << "Failed to compute transform between pose protos");

      return absl::StrFormat("Pose Transform: %s", transform);
    }
  }

#define COMPARE_COMPONENT(component)                                           \
  do {                                                                         \
    if (first_component.has_##component() ||                                   \
        second_component.has_##component()) {                                  \
      if (!ProtosEqual(first_component.component(),                            \
                       second_component.component())) {                        \
        return absl::StrReplaceAll(                                            \
            absl::StrFormat("Object's %s differs", #component), {{"_", " "}}); \
      }                                                                        \
    }                                                                          \
  } while (false)

  COMPARE_COMPONENT(ppr_component);  
  COMPARE_COMPONENT(simulation_component);
#undef COMPARE_COMPONENT

  // Check children by ID
  if (id_mode == CompareWorldsRequest::BY_ID) {
    INTR_ASSIGN_OR_RETURN(
        std::optional<std::string> mismatch,
        FindMismatch(first_object.children(), second_object.children(),
                     "child object with ID %s",
                     [](const IdAndName& idname) { return idname.id(); }));
    if (mismatch.has_value()) return mismatch.value();
  }

  // Check children by name
  {
    INTR_ASSIGN_OR_RETURN(
        std::optional<std::string> mismatch,
        FindMismatch(first_object.children(), second_object.children(),
                     "child object '%s'",
                     [](const IdAndName& idname) { return idname.name(); }));
    if (mismatch.has_value()) return mismatch.value();
  }

  // Check frames by name
  {
    INTR_ASSIGN_OR_RETURN(
        std::optional<std::string> mismatch,
        FindMismatch(first_object.frames(), second_object.frames(),
                     "frame '%s'",
                     [](const Frame& frame) { return frame.name(); }));
    if (mismatch.has_value()) return mismatch.value();
  }

  for (auto const& frame : second_object.frames()) {
    auto it = absl::c_find_if(first_object.frames(), [&frame](const Frame& f) {
      return f.name() == frame.name();
    });
    if (it == first_object.frames().end()) {
      return absl::InternalError("Shouldn't get here -- this is a bug.");
    }
    INTR_ASSIGN_OR_RETURN(std::string frame_modification,
                          FramesEqual(*it, frame, !has_entities, id_mode));
    if (!frame_modification.empty()) {
      return absl::StrFormat("Frame '%s' differs: %s", frame.name(),
                             frame_modification);
    }
  }

  if (first_object.has_kinematic_object_component() ||
      second_object.has_kinematic_object_component()) {
    if (id_mode == CompareWorldsRequest::BY_NAME) {
      WorldHashMap<std::string, std::string> second_entity_name_to_id;
      for (const auto& [id, entity] : second_object.entities()) {
        second_entity_name_to_id[entity.name()] = id;
      }
      for (const Frame& frame : second_object.frames()) {
        second_entity_name_to_id[frame.name()] = frame.id();
      }

      WorldHashMap<std::string, std::string> first_entity_name_to_id;
      for (const auto& [id, entity] : first_object.entities()) {
        first_entity_name_to_id[entity.name()] = id;
      }
      for (const Frame& frame : first_object.frames()) {
        first_entity_name_to_id[frame.name()] = frame.id();
      }

      WorldHashMap<std::string, std::string> second_to_first_id;
      for (const auto& [name, second_id] : second_entity_name_to_id) {
        auto it = first_entity_name_to_id.find(name);
        if (it != first_entity_name_to_id.end()) {
          second_to_first_id[second_id] = it->second;
        }
      }
      second_to_first_id[second_object.id()] = first_object.id();
      if (!second_object.root_entity_id().empty() &&
          !first_object.root_entity_id().empty()) {
        second_to_first_id[second_object.root_entity_id()] =
            first_object.root_entity_id();
      }
      KinematicObjectComponent normalized_first_koc =
          first_object.kinematic_object_component();
      KinematicObjectComponent normalized_second_koc =
          second_object.kinematic_object_component();

      normalized_first_koc.clear_are_kinematics_updated();
      normalized_second_koc.clear_are_kinematics_updated();

      for (int i = 0; i < normalized_second_koc.joint_entity_ids_size(); ++i) {
        auto it =
            second_to_first_id.find(normalized_second_koc.joint_entity_ids(i));
        if (it != second_to_first_id.end()) {
          *normalized_second_koc.mutable_joint_entity_ids(i) = it->second;
        }
      }
      for (KinematicObjectComponent::IkSolver& solver :
           *normalized_second_koc.mutable_ik_solvers()) {
        auto it_base = second_to_first_id.find(solver.base_entity_id());
        if (it_base != second_to_first_id.end()) {
          solver.set_base_entity_id(it_base->second);
        }
        auto it_tip = second_to_first_id.find(solver.tip_entity_id());
        if (it_tip != second_to_first_id.end()) {
          solver.set_tip_entity_id(it_tip->second);
        }
      }
      auto solver_comparator = [](const KinematicObjectComponent::IkSolver& a,
                                  const KinematicObjectComponent::IkSolver& b) {
        return std::tie(a.kinematic_solver_key(), a.base_entity_id(),
                        a.tip_entity_id()) < std::tie(b.kinematic_solver_key(),
                                                      b.base_entity_id(),
                                                      b.tip_entity_id());
      };
      std::sort(normalized_first_koc.mutable_ik_solvers()->begin(),
                normalized_first_koc.mutable_ik_solvers()->end(),
                solver_comparator);
      std::sort(normalized_second_koc.mutable_ik_solvers()->begin(),
                normalized_second_koc.mutable_ik_solvers()->end(),
                solver_comparator);

      for (IdAndName& flange :
           *normalized_second_koc.mutable_iso_flange_frames()) {
        auto it = second_to_first_id.find(flange.id());
        if (it != second_to_first_id.end()) {
          flange.set_id(it->second);
        }
      }
      auto flange_comparator = [](const IdAndName& a, const IdAndName& b) {
        return a.name() < b.name();
      };
      std::sort(normalized_first_koc.mutable_iso_flange_frames()->begin(),
                normalized_first_koc.mutable_iso_flange_frames()->end(),
                flange_comparator);
      std::sort(normalized_second_koc.mutable_iso_flange_frames()->begin(),
                normalized_second_koc.mutable_iso_flange_frames()->end(),
                flange_comparator);

      auto config_comparator = [](const NamedJointConfiguration& a,
                                  const NamedJointConfiguration& b) {
        return a.name() < b.name();
      };
      std::sort(
          normalized_first_koc.mutable_named_joint_configurations()->begin(),
          normalized_first_koc.mutable_named_joint_configurations()->end(),
          config_comparator);
      std::sort(
          normalized_second_koc.mutable_named_joint_configurations()->begin(),
          normalized_second_koc.mutable_named_joint_configurations()->end(),
          config_comparator);

      if (!ProtosEqual(normalized_first_koc, normalized_second_koc)) {
        return "Object's kinematic component differs";
      }
    } else {
      if (!ProtosEqual(first_object.kinematic_object_component(),
                       second_object.kinematic_object_component())) {
        return "Object's kinematic component differs";
      }
    }
  }

  if (id_mode == CompareWorldsRequest::BY_ID) {
    if (first_object.parent_entity().id() !=
        second_object.parent_entity().id()) {
      return absl::StrFormat("Parent entity id differs: %s vs %s",
                             first_object.parent_entity().id(),
                             second_object.parent_entity().id());
    }

    if (first_object.root_entity_id() != second_object.root_entity_id()) {
      return absl::StrFormat("Root entity id differs: %s vs %s",
                             first_object.root_entity_id(),
                             second_object.root_entity_id());
    }
  }

  if (has_entities) {
    // Compare entities by id
    if (id_mode == CompareWorldsRequest::BY_ID) {
      INTR_ASSIGN_OR_RETURN(
          std::optional<std::string> mismatch,
          FindMismatch(first_object.entities(), second_object.entities(),
                       "entity with ID %s",
                       [](const auto& e) { return e.first; }));
      if (mismatch.has_value()) return mismatch.value();
    } else {
      INTR_ASSIGN_OR_RETURN(
          std::optional<std::string> mismatch,
          FindMismatch(first_object.entities(), second_object.entities(),
                       "entity '%s'",
                       [](const auto& e) { return e.second.name(); }));
      if (mismatch.has_value()) return mismatch.value();
    }

    WorldHashMap<std::string, const Entity*> second_entities;
    WorldHashMap<std::string, const Entity*> second_entities_by_id;
    for (const auto& [entity_id, entity] : second_object.entities()) {
      second_entities[entity.name()] = &entity;
      second_entities_by_id[entity_id] = &entity;
    }

    for (const auto& [entity_id, first_entity] : first_object.entities()) {
      const Entity& second_entity = id_mode == CompareWorldsRequest::BY_ID
                                        ? *second_entities_by_id[entity_id]
                                        : *second_entities[first_entity.name()];
      if (id_mode == CompareWorldsRequest::BY_ID &&
          first_entity.parent_id() != second_entity.parent_id()) {
        return absl::StrFormat("Entity '%s' has mismatched parent ID: %s vs %s",
                               second_entity.name(), first_entity.parent_id(),
                               second_entity.parent_id());
      }

      if (!PoseProtosApproximatelyEqual(first_entity.parent_t_this(),
                                        second_entity.parent_t_this())) {
        INTR_ASSIGN_OR_RETURN(
            std::string transform,
            ComputeTransformBetweenPoseProtos(first_entity.parent_t_this(),
                                              second_entity.parent_t_this()));
        return absl::StrFormat("Pose for entity '%s' changed by: %s",
                               first_entity.name(), transform);
      }

#define COMPARE_COMPONENT(component)                                           \
  do {                                                                         \
    if (!ProtosEqual(first_entity.component(), second_entity.component())) {   \
      return absl::StrReplaceAll(                                              \
          absl::StrFormat("Entity '%s' has modified %s", second_entity.name(), \
                          #component),                                         \
          {{"_", " "}});                                                       \
    }                                                                          \
  } while (false)

      COMPARE_COMPONENT(ppr_component);  
      {
        auto first_kinematics = first_entity.kinematics_component();
        auto second_kinematics = second_entity.kinematics_component();
        first_kinematics.clear_wall_clock_timestamp();
        second_kinematics.clear_wall_clock_timestamp();
        if (!ProtosEqual(first_kinematics, second_kinematics)) {
          return absl::StrFormat(
              "Entity '%s' has modified kinematics component",
              second_entity.name());
        }
      }
      COMPARE_COMPONENT(physics_component);
      COMPARE_COMPONENT(sensor_component);

      // Geometry we compare separately to provide support for v0 and v1 refs.
      if (first_entity.has_geometry_component() &&
          second_entity.has_geometry_component()) {
        const intrinsic_proto::world::GeometryComponent& first_geo =
            first_entity.geometry_component();
        const intrinsic_proto::world::GeometryComponent& second_geo =
            second_entity.geometry_component();

        using GeometrySet =
            ::intrinsic_proto::world::GeometryComponent::GeometrySet;
        const std::string error = CompareStringMap<GeometrySet>(
            "geometries", first_geo.named_geometries(),
            second_geo.named_geometries(),
            [](const GeometrySet& first_geo_set,
               const GeometrySet& second_geo_set) -> std::string {
              const auto has_geos_v1 = [](const GeometrySet& geo_set) {
                return !geo_set.named_geometries().empty();
              };
              if (has_geos_v1(first_geo_set) || has_geos_v1(second_geo_set)) {
                using Geometry =
                    ::intrinsic_proto::geometry::v1::TransformedGeometry;
                return CompareStringMap<Geometry>(
                    "v1 geometries", first_geo_set.named_geometries(),
                    second_geo_set.named_geometries(),
                    [](const Geometry& geo_1, const Geometry& geo_2) {
                      if (!ProtosEqual(geo_1, geo_2)) {
                        return "has modified v1 geometry";
                      }
                      return "";
                    });
              }

              const auto has_geos_v0 = [](const GeometrySet& geo_set) {
                return !geo_set.geometries().empty();
              };
              if (has_geos_v0(first_geo_set) || has_geos_v0(second_geo_set)) {
                using Geometry =
                    ::intrinsic_proto::world::GeometryComponent::Geometry;
                google::protobuf::Map<std::string, Geometry> geos1;
                for (int i = 0; i < first_geo_set.geometries().size(); ++i) {
                  geos1[absl::StrFormat("%i", i)] =
                      first_geo_set.geometries()[i];
                }

                google::protobuf::Map<std::string, Geometry> geos2;
                for (int i = 0; i < second_geo_set.geometries().size(); ++i) {
                  geos2[absl::StrFormat("%i", i)] =
                      second_geo_set.geometries()[i];
                }

                return CompareStringMap<Geometry>(
                    "v0 geometries", geos1, geos2,
                    [](const Geometry& geo_1, const Geometry& geo_2) {
                      if (!ProtosEqual(geo_1, geo_2)) {
                        return "has modified v0 geometry";
                      }
                      return "";
                    });
              }

              // No geos for either entity for this key -- return equal.
              return "";
            });
        if (error != "") {
          return absl::StrFormat("Entity '%s' %s", second_entity.name(), error);
        }
      } else {
        // If either entity has a geometry component, then it should have
        // serialized to the default one.
        COMPARE_COMPONENT(geometry_component);
      }
#undef COMPARE_COMPONENT
    }
  }

  return "";
}

absl::StatusOr<const Entity*> FindFrameEntity(const Object& object,
                                              const Frame& frame) {
  INTR_ASSIGN_OR_RETURN(
      EntityId frame_entity_world_id,
      ObjectWorldResourceIdToEntityId(ObjectWorldResourceId(frame.id())));
  ObjectWorldResourceId frame_entity_id =
      ObjectWorldResourceIdForEntity(AttachmentEntityId(frame_entity_world_id));
  const Entity* frame_entity = nullptr;
  for (auto& [entity_id, entity] : object.entities()) {
    if (ObjectWorldResourceId(entity_id) == frame_entity_id) {
      if (frame_entity != nullptr) {
        return absl::FailedPreconditionError(absl::StrFormat(
            "frame name %s is not unique among entities", frame.name()));
      }
      frame_entity = &entity;
    }
  }

  if (frame_entity == nullptr) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "frame name %s is not found among entities", frame.name()));
  }

  return frame_entity;
}

}  // namespace

absl::Status WorldComparator::CompareObjectFrames(
    const Object& first_object, const Object& second_object,
    CompareWorldsRequest::IdentificationMode id_mode) {
  WorldHashMap<std::string, const Frame> first_frames;
  for (const Frame& frame : first_object.frames()) {
    auto it = id_mode == CompareWorldsRequest::BY_NAME
                  ? first_frames.insert({frame.name(), frame})
                  : first_frames.insert({frame.id(), frame});
    if (!it.second) {
      return absl::FailedPreconditionError(
          absl::StrFormat("frame name %s is not unique", frame.name()));
    }
  }

  const bool has_entities =
      !first_object.entities().empty() || !second_object.entities().empty();
  for (const Frame& frame : second_object.frames()) {
    auto it = id_mode == CompareWorldsRequest::BY_NAME
                  ? first_frames.find(frame.name())
                  : first_frames.find(frame.id());
    if (it == first_frames.end()) {
      added_frames_.push_back(frame);
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        std::string modification,
        FramesEqual(it->second, frame, !has_entities, id_mode));
    if (modification.empty() && has_entities) {
      INTR_ASSIGN_OR_RETURN(const Entity* first_frame_entity,
                            FindFrameEntity(first_object, it->second));
      INTR_ASSIGN_OR_RETURN(const Entity* second_frame_entity,
                            FindFrameEntity(second_object, frame));

      if (!PoseProtosApproximatelyEqual(first_frame_entity->parent_t_this(),
                                        second_frame_entity->parent_t_this())) {
        modification = absl::StrFormat("Poses differ");
      }
    }

    if (!modification.empty()) {
      modified_frames_.push_back(std::make_pair(frame, modification));
    }

    // Frames are equal
    first_frames.erase(it);
  }

  for (const auto& [id, frame] : first_frames) {
    removed_frames_.push_back(frame);
  }
  return absl::OkStatus();
}

absl::StatusOr<WorldComparator> WorldComparator::Compare(
    const intrinsic_proto::world::World& base_world,
    const intrinsic_proto::world::World& changed_world,
    CompareWorldsRequest::IdentificationMode id_mode) {
  WorldComparator comparator;

  if (id_mode == CompareWorldsRequest::IDENTIFICATION_MODE_UNSPECIFIED) {
    id_mode = CompareWorldsRequest::BY_ID;
  }

  WorldHashMap<std::string, const Object*> base_objects;
  for (const auto& base_object : base_world.objects()) {
    const std::string& unique_id = (id_mode == CompareWorldsRequest::BY_NAME)
                                       ? base_object.name()
                                       : base_object.id();
    if (!base_objects.insert({unique_id, &base_object}).second) {
      return absl::InvalidArgumentError(
          absl::StrFormat("object '%s' is not unique", unique_id));
    }
  }

  for (const auto& changed_object : changed_world.objects()) {
    const std::string& unique_id = (id_mode == CompareWorldsRequest::BY_NAME)
                                       ? changed_object.name()
                                       : changed_object.id();

    if (auto it = base_objects.find(unique_id); it == base_objects.end()) {
      ObjectReference object_reference;
      object_reference.set_id(changed_object.id());
      if (id_mode == CompareWorldsRequest::BY_NAME) {
        object_reference.mutable_by_name()->set_object_name(
            changed_object.name());
      }
      comparator.added_.push_back(object_reference);
      for (const Frame& frame : changed_object.frames()) {
        comparator.added_frames_.push_back(frame);
      }
    } else {
      INTR_ASSIGN_OR_RETURN(
          std::string mismatch,
          ObjectsEqual(*(it->second), changed_object, id_mode));
      if (!mismatch.empty()) {
        ObjectReference object_reference;
        object_reference.set_id(changed_object.id());
        if (id_mode == CompareWorldsRequest::BY_NAME) {
          object_reference.mutable_by_name()->set_object_name(
              changed_object.name());
        }
        comparator.modified_.push_back(
            std::make_pair(object_reference, mismatch));
        INTR_RETURN_IF_ERROR(comparator.CompareObjectFrames(
            *(it->second), changed_object, id_mode));
      }
      base_objects.erase(it);
    }
  }

  for (const auto& [id, object] : base_objects) {
    comparator.removed_.push_back(*object);
    for (const Frame& frame : object->frames()) {
      comparator.removed_frames_.push_back(frame);
    }
  }
  return comparator;
}

const std::vector<ObjectReference>& WorldComparator::GetAdded() const {
  return added_;
}

const std::vector<Object>& WorldComparator::GetRemoved() const {
  return removed_;
}

const std::vector<std::pair<ObjectReference, std::string>>&
WorldComparator::GetModified() const {
  return modified_;
}

const std::vector<Frame>& WorldComparator::GetAddedFrames() const {
  return added_frames_;
}

const std::vector<Frame>& WorldComparator::GetRemovedFrames() const {
  return removed_frames_;
}

const std::vector<std::pair<Frame, std::string>>&
WorldComparator::GetModifiedFrames() const {
  return modified_frames_;
}

}  // namespace object_world
}  // namespace intrinsic
