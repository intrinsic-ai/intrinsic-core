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

#include "intrinsic/scene/sdf/scene_object_from_sdf.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/map.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/struct.pb.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/conversion/object_properties_conversion.h"
#include "intrinsic/scene/conversion/user_data.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/object_properties.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/proto/v1/simulation_spec.pb.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/entity_from_sdf.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/sdf/sim_spec_from_sdf.h"
#include "intrinsic/scene/user_data_keys.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/conversion/sdf/joint_conversion.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "ortools/base/path.h"
#include "sdf/Link.hh"
#include "sdf/ParserConfig.hh"
#include "sdf/Root.hh"
#include "sdf/Types.hh"
#include "sdf/World.hh"
#include "sdf/parser.hh"

namespace intrinsic {
namespace scene_object {

namespace {

using SceneObject = intrinsic_proto::scene_object::v1::SceneObject;
using SimulationSpec = intrinsic_proto::scene_object::v1::SimulationSpec;

template <class T>
std::vector<T> AsVector(
    const google::protobuf::RepeatedPtrField<T>& container) {
  return std::vector<T>(container.begin(), container.end());
}

// Return true if the sdf joint type is BALL.
bool IsBallSdfJointType(::sdf::JointType joint_type) {
  return joint_type == ::sdf::JointType::BALL;
}

// Return true if the sdf joint type is FIXED.
bool IsFixedSdfJointType(::sdf::JointType joint_type) {
  return joint_type == ::sdf::JointType::FIXED;
}

}  // namespace

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfRoot(const ::sdf::Root& sdf_root,
                       const sdf::UriResolver& uri_resolver,
                       GeometrySerializer& geometry_serializer,
                       const SceneObjectFromSdfOptions& options) {
  if (sdf_root.WorldCount() > 1) {
    return absl::InvalidArgumentError(
        "Cannot convert SDF Root with more than one world to an Intrinsic "
        "Scene Object.");
  }

  if (sdf_root.WorldCount() == 1) {
    return SceneObjectFromSdfWorld(*sdf_root.WorldByIndex(0), uri_resolver,
                                   geometry_serializer, options);
  }

  if (const auto* model = sdf_root.Model()) {
    return SceneObjectFromSdfModel(*model, uri_resolver, geometry_serializer,
                                   options);
  }

  return absl::InvalidArgumentError(
      "Cannot convert SDF Root with neither a World nor a Model to an "
      "Intrinsic Scene Object.");
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfWorld(const ::sdf::World& sdf_world,
                        const sdf::UriResolver& uri_resolver,
                        GeometrySerializer& geometry_serializer,
                        const SceneObjectFromSdfOptions& options) {
  if (sdf_world.ModelCount() != 1) {
    return absl::InvalidArgumentError(
        "Cannot convert SDF World with not exactly one Model to an Intrinsic "
        "Scene Object.");
  }
  return SceneObjectFromSdfModel(*sdf_world.ModelByIndex(0), uri_resolver,
                                 geometry_serializer, options);
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfModel(const ::sdf::Model& sdf_model,
                        const sdf::UriResolver& uri_resolver,
                        GeometrySerializer& geometry_serializer,
                        const SceneObjectFromSdfOptions& options) {
  if (sdf_model.ModelCount() != 0) {
    return absl::InvalidArgumentError(
        "Cannot convert SDF Model with child Models to an Intrinsic Scene "
        "Object");
  }
  if (sdf_model.LinkCount() == 0) {
    return absl::InvalidArgumentError(
        "Cannot convert SDF Model without any link to an Intrinsic Scene "
        "Object");
  }

  intrinsic_proto::scene_object::v1::SceneObject scene_object_model;
  scene_object_model.set_name(sdf_model.Name());

  const auto frame_count = sdf_model.FrameCount();
  for (auto i = 0; i < frame_count; ++i) {
    const ::sdf::Frame* frame = sdf_model.FrameByIndex(i);
    INTR_ASSIGN_OR_RETURN(auto frame_entity, EntityFromSdfFrame(*frame),
                          _ << "While parsing frames in SDF Model "
                            << sdf_model.Name()
                            << " to convert to an Intrinsic Scene Object.");
    if (frame_entity.has_value()) {
      *scene_object_model.add_entities() = std::move(frame_entity).value();
    }
  }

  const auto link_count = sdf_model.LinkCount();
  for (auto i = 0; i < link_count; ++i) {
    const ::sdf::Link* link = sdf_model.LinkByIndex(i);
    INTR_ASSIGN_OR_RETURN(
        auto link_entities,
        EntitiesFromSdfLink(*link, uri_resolver, geometry_serializer),
        _ << "While parsing links in SDF Model " << sdf_model.Name()
          << " to convert to an Intrinsic Scene Object.");
    // Avoids deep copies by moving created link and sensor entities into
    // scene_object_model.
    for (auto&& link_entity : link_entities) {
      *scene_object_model.add_entities() = std::move(link_entity);
    }
  }

  bool has_non_fixed_joint = false;
  const auto joint_count = sdf_model.JointCount();
  for (auto i = 0; i < joint_count; ++i) {
    const ::sdf::Joint* joint = sdf_model.JointByIndex(i);
    has_non_fixed_joint = !IsFixedSdfJointType(joint->Type());
    if (has_non_fixed_joint) {
      break;
    }
  }

  absl::flat_hash_map<std::string, std::string> ball_joint_sdf;
  for (auto i = 0; i < joint_count; ++i) {
    const ::sdf::Joint* joint = sdf_model.JointByIndex(i);
    INTR_ASSIGN_OR_RETURN(auto joint_entities, EntitiesFromSdfJoint(*joint),
                          _ << "While parsing joints in SDF Model "
                            << sdf_model.Name()
                            << " to convert to an Intrinsic Scene Object.");

    const auto& joint_name = joint_entities.joint_entity.name();
    // Keep track of ball joints in a map of joint name to joint sdf string
    // This information will then be stored in the scene object's user data
    if (IsBallSdfJointType(joint->Type())) {
      // TODO(b/498350265) Exclude nested <sensor> elements when serializing the
      // <joint> element to string.
      ball_joint_sdf[joint->Name()] = joint->ToElement()->ToString("");
    }

    // First find the joint child and reparent joint child or bypass fixed
    // joint.
    auto child_entity = absl::c_find_if(
        *scene_object_model.mutable_entities(),
        [&](auto& e) { return e.name() == joint_entities.joint_child_name; });
    if (child_entity == scene_object_model.mutable_entities()->end()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot find corresponding joint child $0 for joint $1",
          joint_entities.joint_child_name, joint_name));
    }
    if (!child_entity->parent_name().empty()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot reparent joint child entity '$0' to joint entity '$1' "
          "because it is already a child of entity '$2'.",
          child_entity->name(), joint_name, child_entity->parent_name()));
    }

    // Check if the joint parent is valid.
    const auto& parent_name = joint_entities.joint_entity.parent_name();
    if (!parent_name.empty() &&
        absl::c_none_of(scene_object_model.entities(),
                        [&](auto& e) { return e.name() == parent_name; })) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot find corresponding joint parent entity '$0' for joint '$1'",
          parent_name, joint_name));
    }

    // We bypass any fixed joint if there is no sensor attached to it.
    const bool is_fixed_joint =
        joint_entities.joint_entity.joint()
            .kinematics_component()
            .motion_type() ==
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED;
    // We do not inline fixed joints if they have sensors attached to them or if
    // the model has other non fixed joints.
    const bool bypass_joint = !has_non_fixed_joint && is_fixed_joint &&
                              joint_entities.sensor_entities.empty();
    if (bypass_joint) {
      child_entity->set_parent_name(parent_name);
      *child_entity->mutable_parent_t_this() =
          std::move(joint_entities.joint_entity.parent_t_this());
      continue;
    }

    child_entity->set_parent_name(joint_name);
    *child_entity->mutable_parent_t_this() = ToProto(Pose3d());

    // Add all created entities to scene Object.
    scene_object_model.mutable_entities()->Add(
        std::move(joint_entities.joint_entity));
    for (auto&& sensor_entity : joint_entities.sensor_entities) {
      scene_object_model.mutable_entities()->Add(std::move(sensor_entity));
    }
  }

  // All entity parents should be set by this point. Make sure we only have one
  // root entity and clear its parent_t_this.
  std::vector<absl::string_view> root_entity_names;
  for (auto& e : *scene_object_model.mutable_entities()) {
    if (e.parent_name().empty()) {
      root_entity_names.push_back(e.name());
      e.clear_parent_t_this();
    }
  }

  if (root_entity_names.size() != 1) {
    return absl::FailedPreconditionError(
        absl::StrCat("Expected only one root entity, but found: ",
                     absl::StrJoin(root_entity_names, ", ")));
  }

  // Parses optional <intrinsic:cartesian_limits> tag.
  INTR_ASSIGN_OR_RETURN(const auto limits,
                        CartesianLimitsFromSdfModel(sdf_model));
  if (limits.has_value()) {
    *scene_object_model.mutable_properties()
         ->mutable_kinematics()
         ->mutable_limits() = std::move(*limits);
  }

  // Parses optional <intrinsic:ik_solver> tag.
  INTR_ASSIGN_OR_RETURN(const auto ik_solvers,
                        IkSolversFromSdfModel(sdf_model));
  if (!ik_solvers.empty()) {
    scene_object_model.mutable_properties()
        ->mutable_kinematics()
        ->mutable_ik_solvers()
        ->Assign(ik_solvers.begin(), ik_solvers.end());
  }

  // Parse and apply sim plugins.
  INTR_ASSIGN_OR_RETURN(
      std::optional<SimulationSpec> sim_spec,
      ExtractSimulationSpecFromSdf(sdf_model),
      _.LogError() << absl::Substitute(
          "Failed to extract simulation spec from SDF Model: '$0'.",
          scene_object_model.name()));
  if (sim_spec.has_value()) {
    *scene_object_model.mutable_simulation_spec() = std::move(*sim_spec);
  }

  // Ball joints are treated as fixed joints in the scene object and World.
  // However, we keep the <joint> SDF element in the scene object's user data
  // to convert back to ball joint for simulation. This ensures that the
  // appropriate simulation params (such as joint stiffness) are also preserved
  // and propagated to the simulator.
  // The joint data is stored with the `kGazeboCustomJoint` user data key, as
  // a `protobuf::Struct` key-value map. Key is the joint name and value is
  // the `<joint>` element xml string.
  // Nested <sensor> elements are converted to `Sensor` entities. So the
  // <sensor> elements are removed from the stored <joint> sdf data to avoid
  // multiple SoT for the same data.
  // TODO(b/492585418): Support ball joint type in World
  if (!ball_joint_sdf.empty()) {
    google::protobuf::Struct struct_msg;
    auto& fields = *struct_msg.mutable_fields();
    for (const auto& [joint_name, joint_sdf] : ball_joint_sdf) {
      google::protobuf::Value v;
      v.set_string_value(joint_sdf);
      fields[joint_name] = v;
    }
    auto* user_data_map = scene_object_model.mutable_user_data();
    google::protobuf::Any& any_data_from_map =
        (*user_data_map)[sdf::kGazeboCustomJoint];
    static_cast<void>(
        any_data_from_map.PackFrom(struct_msg));  // TODO: Handle return value
  }

  if (sdf_model.Element()->HasElement(
          std::string(sdf::kUserDataCustomElement))) {
    auto element = sdf_model.Element()->GetElement(
        std::string(sdf::kUserDataCustomElement));
    INTR_ASSIGN_OR_RETURN(
        auto user_data,
        UserDataFromString(element->Get<std::string>(), options.user_data_fds));

    auto* mutable_user_data = scene_object_model.mutable_user_data();
    for (const auto& [key, value] : user_data) {
      mutable_user_data->insert({key, value});
    }
  }

  return scene_object_model;
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfFile(absl::string_view sdf_file,
                       const sdf::UriResolver& uri_resolver,
                       GeometrySerializer& geometry_serializer,
                       const SceneObjectFromSdfOptions& options) {
  auto sdf_parser_config = ::sdf::ParserConfig::GlobalConfig();
  sdf_parser_config.SetStoreResolvedURIs(true);
  sdf_parser_config.AddURIPath("model://",
                               std::string(file::Dirname(sdf_file)));
  sdf_parser_config.AddURIPath("file://", std::string(file::Dirname(sdf_file)));
  sdf_parser_config.SetFindCallback(
      [&uri_resolver](const std::string& uri) -> std::string {
        auto resolved_path = uri_resolver(uri);
        if (resolved_path.ok()) {
          return std::move(resolved_path).value();
        }
        LOG(ERROR) << "Failed to resolve URI '" << uri
                   << "': " << resolved_path.status();
        return "";
      });

  ::sdf::Errors errors;
  auto sdf_root = std::make_unique<::sdf::Root>();
  INTR_RETURN_IF_ERROR(
      sdf::ToStatus(sdf_root->Load(std::string(sdf_file), sdf_parser_config)))
      << "Failed to convert sdf file: " << sdf_file << " to scene object.";

  return SceneObjectFromSdfRoot(*sdf_root, uri_resolver, geometry_serializer,
                                options);
}

absl::StatusOr<
    std::optional<intrinsic_proto::scene_object::v1::CartesianLimits>>
CartesianLimitsFromSdfModel(const ::sdf::Model& model) {
  if (!model.Element()->HasElement(
          std::string(sdf::kCartesianLimitsCustomElement))) {
    return std::nullopt;
  }

  auto cartesian_limits_element = model.Element()->GetElement(
      std::string(sdf::kCartesianLimitsCustomElement));
  INTR_ASSIGN_OR_RETURN(CartesianLimits cartesian_limits,
                        sdf::ParseCartesianLimits(cartesian_limits_element));

  const auto proto = ToProto(cartesian_limits);
  if (!cartesian_limits.IsValid()) {
    return absl::FailedPreconditionError(
        absl::StrCat("Parsed Cartesian Limits are not valid. Limits: ", proto));
  }

  return proto;
}

absl::StatusOr<std::vector<intrinsic_proto::scene_object::v1::IkSolver>>
IkSolversFromSdfModel(const ::sdf::Model& model) {
  const auto ik_solver_elements = sdf::GetChildrenByTag(
      model.Element(), std::string(sdf::kIkSolverCustomElement));

  std::vector<intrinsic_proto::scene_object::v1::IkSolver> ik_solvers;
  ik_solvers.reserve(ik_solver_elements.size());

  for (const auto& ik_solver_element : ik_solver_elements) {
    INTR_ASSIGN_OR_RETURN(auto solver_and_tip_name,
                          sdf::ParseIkSolver(ik_solver_element));

    intrinsic_proto::scene_object::v1::IkSolver solver;
    solver.set_ik_solver(solver_and_tip_name.first);
    if (solver_and_tip_name.second.has_value()) {
      solver.set_tip_link_name(*solver_and_tip_name.second);
    }

    ik_solvers.push_back(std::move(solver));
  }

  // TODO(b/343309271): Support multiple ik_solvers.
  if (ik_solvers.size() > 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Multiple ik solvers found for model '", model.Name(),
        "'. Only one should be provided. Found [",
        absl::StrJoin(ik_solvers, ", ",
                      [](std::string* str, const auto& ik_solver) {
                        absl::StrAppend(str, ik_solver.ik_solver());
                      }),
        "]."));
  }

  return ik_solvers;
}

}  // namespace scene_object
}  // namespace intrinsic
