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

#include "intrinsic/icon/server/jogging_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/eigenmath/types.h"
#include "grpcpp/support/time.h"
#include "intrinsic/icon/actions/joint_jogging_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/robot_config.h"
#include "intrinsic/icon/cc_client/session.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/jogging_service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/math/twist.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic::icon {
namespace {

// Added to make the code more readable.
namespace icon_proto = ::intrinsic_proto::icon::v1;

// LINT.IfChange(jogging_limits)
// These match those in /intrinsic/frontend/go/api/iconwebsocket.go
// and are used as upper limits for the jogging action limits.
constexpr double kMaxJointJoggingSpeedRadPerSecond = 30.0 * M_PI / 180;
constexpr double kMaxCartJoggingRotationalSpeedRadPerSecond = 30.0 * M_PI / 180;
constexpr double kMaxCartJoggingTranslationalSpeedMetersPerSecond = 0.25;
constexpr double kMaxCartJoggingRotationalAccelerationRadPerSquareSecond =
    300.0 * M_PI / 180;
constexpr double kMaxCartJoggingTranslationalAccelerationMetersPerSquareSecond =
    5.0;
constexpr double kMaxCartJoggingRotationalJerkRadPerCubicSecond =
    3000.0 * M_PI / 180;
constexpr double kMaxCartJoggingTranslationalJerkMetersPerCubicSecond = 500.0;
// LINT.ThenChange(//intrinsic/frontend/go/api/iconwebsocket.go:jogging_limits)

absl::StatusOr<CartesianLimits> GetClampedCartesianLimits(
    const ::intrinsic_proto::icon::CartesianLimits& initial_cartesian_limits) {
  INTR_ASSIGN_OR_RETURN(CartesianLimits cartesian_limits,
                        FromProto(initial_cartesian_limits));

  const double max_translational_velocity = std::min(
      std::min(cartesian_limits.max_translational_velocity.minCoeff(),
               -cartesian_limits.min_translational_velocity.maxCoeff()),
      kMaxCartJoggingTranslationalSpeedMetersPerSecond);

  const double max_translational_acceleration = std::min(
      std::min(cartesian_limits.max_translational_acceleration.minCoeff(),
               -cartesian_limits.min_translational_acceleration.maxCoeff()),
      kMaxCartJoggingTranslationalAccelerationMetersPerSquareSecond);

  const double max_translational_jerk =
      std::min(std::min(cartesian_limits.max_translational_jerk.minCoeff(),
                        -cartesian_limits.min_translational_jerk.maxCoeff()),
               kMaxCartJoggingTranslationalJerkMetersPerCubicSecond);

  cartesian_limits.max_translational_velocity.setConstant(
      max_translational_velocity);
  cartesian_limits.min_translational_velocity.setConstant(
      -max_translational_velocity);
  cartesian_limits.max_translational_acceleration.setConstant(
      max_translational_acceleration);
  cartesian_limits.min_translational_acceleration.setConstant(
      -max_translational_acceleration);
  cartesian_limits.max_translational_jerk.setConstant(max_translational_jerk);
  cartesian_limits.min_translational_jerk.setConstant(-max_translational_jerk);

  cartesian_limits.max_rotational_velocity =
      std::min(cartesian_limits.max_rotational_velocity,
               kMaxCartJoggingRotationalSpeedRadPerSecond);
  cartesian_limits.max_rotational_acceleration =
      std::min(cartesian_limits.max_rotational_acceleration,
               kMaxCartJoggingRotationalAccelerationRadPerSquareSecond);
  cartesian_limits.max_rotational_jerk =
      std::min(cartesian_limits.max_rotational_jerk,
               kMaxCartJoggingRotationalJerkRadPerCubicSecond);

  if (!cartesian_limits.IsValid()) {
    return absl::InternalError(
        "Cartesian limits are not valid after applying jogging limits. This is "
        "likely a programming error!");
  }

  return cartesian_limits;
}

bool IsValidNormalizedVelocity(double normalized_velocity) {
  return normalized_velocity >= -1 && normalized_velocity <= 1;
}

absl::Status CreateInvalidJointVelocityError(int joint) {
  return absl::InvalidArgumentError(
      absl::Substitute("Invalid joint[$0] normalized velocity "
                       "requested. This must be in the range [-1, 1].",
                       joint));
}

absl::StatusOr<JointLimits> GetClampedJointLimits(
    const ::intrinsic_proto::JointLimits& initial_joint_limits) {
  INTR_ASSIGN_OR_RETURN(JointLimits joint_limits,
                        intrinsic::FromProto(initial_joint_limits));
  joint_limits.max_velocity =
      joint_limits.max_velocity.cwiseMin(kMaxJointJoggingSpeedRadPerSecond);
  return joint_limits;
}

absl::StatusOr<std::vector<std::string>> GetDofNames(
    const intrinsic_proto::world::Object& object) {
  std::vector<std::string> joint_names;
  if (!object.has_kinematic_object_component()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Object ", object.name(),
                     " does not have a kinematic object component."));
  }
  if (object.kinematic_object_component().joint_entity_ids_size() == 0) {
    return absl::FailedPreconditionError(
        absl::StrCat("Object ", object.name(),
                     " has a kinematic object component, but does not have any "
                     "joint entity ids."));
  }
  for (const auto& joint_entity_id :
       object.kinematic_object_component().joint_entity_ids()) {
    joint_names.push_back(object.entities().at(joint_entity_id).name());
  }
  return joint_names;
}

// A dummy implementation of the ICON JoggingService gRPC service.
// Returns empty available parts and fails any jogging request with a
// UnimplementedError status.
class DummyIconJoggingService
    : public ::intrinsic_proto::icon::v1::JoggingService::Service {
 public:
  DummyIconJoggingService() = default;
  ~DummyIconJoggingService() override = default;

  // Returns OkStatus with empty available part.s
  ::grpc::Status GetAvailableParts(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::icon::v1::AvailablePartsRequest* request,
      ::intrinsic_proto::icon::v1::AvailablePartsResponse* response) override {
    return ::grpc::Status::OK;
  };

  // Returns UnimplementedError.
  ::grpc::Status JogRobot(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<::intrinsic_proto::icon::v1::JoggingResponse,
                                 ::intrinsic_proto::icon::v1::JoggingRequest>*
          stream) override {
    return ToGrpcStatus(absl::UnimplementedError(
        "No jogging parts are available. Please ensure a valid HalArmPart is "
        "configured in your ICON server, and that world service is enabled in "
        "your ICON server config via:\n"
        "services {\n"
        "  world_service_from_grpc { world_id: \"world\" }\n"
        "  kinematics_from_world_service: true\n"
        "  assembly_from_world_service: true\n"
        "}"));
  };
};

}  // namespace

absl::StatusOr<std::vector<std::string>>
IconJoggingService::GetPartOrderByKinematicPath() {
  absl::flat_hash_map<std::string, std::string>
      part_name_to_full_kinematic_path;
  std::vector<std::string> ordered_part_names;
  for (const auto& [part_name, part_config] : parts_) {
    INTR_ASSIGN_OR_RETURN(const world::WorldObject world_object,
                          object_world_client_->GetObject(WorldObjectName(
                              part_config.kinematic_object_name)));
    if (world_object.Proto().has_object_full_path() &&
        world_object.Proto().object_full_path().object_names_size() > 0) {
      part_name_to_full_kinematic_path[part_name] = absl::StrJoin(
          world_object.Proto().object_full_path().object_names(), "/");
    } else {
      part_name_to_full_kinematic_path[part_name] =
          part_config.kinematic_object_name;
    }
    ordered_part_names.push_back(part_name);
  }
  // Sorting the parts by their kinematic path results in a depth first ordering
  // according to the kinematic structure.
  std::sort(
      ordered_part_names.begin(), ordered_part_names.end(),
      [&part_name_to_full_kinematic_path](const std::string& part_name_1,
                                          const std::string& part_name_2) {
        return part_name_to_full_kinematic_path[part_name_1] <
               part_name_to_full_kinematic_path[part_name_2];
      });
  return ordered_part_names;
}

absl::StatusOr<IconJoggingService::AvailableJoggingFrames>
IconJoggingService::GetValidJoggingFrames(const absl::string_view part_name) {
  if (!parts_.contains(part_name)) {
    return absl::InvalidArgumentError(
        absl::StrCat(part_name, " is not a valid jogging part."));
  }
  const std::string& world_object_name =
      parts_.at(part_name).kinematic_object_name;
  AvailableJoggingFrames available_frames;
  // Get all the world objects
  INTR_ASSIGN_OR_RETURN(const std::vector<world::WorldObject> world_objects,
                        object_world_client_->ListObjects());
  // We must omit all objects which have a kinematic  object component in
  // their parent chain. First we create a set of all the kinematic objects.
  absl::flat_hash_set<WorldObjectName> kinematic_object_names;
  for (const world::WorldObject& world_object : world_objects) {
    if (world_object.Proto().has_kinematic_object_component()) {
      kinematic_object_names.insert(world_object.Name());
    }
  }
  // Holding pointers to avoid copies here.
  absl::flat_hash_set<const world::WorldObject*> part_kinematic_object_children;
  for (const world::WorldObject& world_object : world_objects) {
    // We need to treat the kinematic objects separately. For the object
    // corresponding to the robot we add the child frames. Otherwise we skip.
    if (world_object.Proto().has_object_full_path() &&
        world_object.Proto().object_full_path().object_names_size() > 0) {
      // If the part of interest is a child of the kinematic object we add it
      // to the set of part_kinematic_object_children so that later we can
      // extract the frames for jogging in the tool frame.
      if (auto it = std::find(
              world_object.Proto().object_full_path().object_names().begin(),
              world_object.Proto().object_full_path().object_names().end(),
              world_object_name);
          it != world_object.Proto().object_full_path().object_names().end() &&
          world_object_name != world_object.Name().value()) {
        part_kinematic_object_children.insert(&world_object);
      };
      // Check if the object is the child of any other kinematic object. If it
      // is we skip and don't add any static frames.
      if (auto it = std::find_if(
              world_object.Proto().object_full_path().object_names().begin(),
              world_object.Proto().object_full_path().object_names().end(),
              [&kinematic_object_names](const std::string& object_name) {
                return kinematic_object_names.contains(
                    WorldObjectName(object_name));
              });
          it != world_object.Proto().object_full_path().object_names().end()) {
        continue;
      }
    }
    auto frames = world_object.Frames();
    for (const auto& frame : frames) {
      ::intrinsic_proto::world::FrameReference frame_reference;
      frame_reference.mutable_by_name()->set_object_name(
          frame.ObjectName().value());
      frame_reference.mutable_by_name()->set_frame_name(frame.Name().value());
      available_frames.static_jogging_frames.push_back(frame_reference);
    }
  }

  INTR_ASSIGN_OR_RETURN(world::KinematicObject part_kinematic_object,
                        object_world_client_->GetKinematicObject(
                            WorldObjectName(world_object_name)));
  // For the kinematic object we are jogging we only add child frames which
  // are attachment frames.
  for (const auto& frame : part_kinematic_object.ChildFrames()) {
    if (frame.IsAttachmentFrame()) {
      ::intrinsic_proto::world::FrameReference frame_reference;
      frame_reference.mutable_by_name()->set_object_name(
          frame.ObjectName().value());
      frame_reference.mutable_by_name()->set_frame_name(frame.Name().value());
      available_frames.tool_jogging_frames.push_back(frame_reference);
    }
  }
  // And for the child objects of the kinematic object we add all frames.
  for (const auto& child : part_kinematic_object_children) {
    for (const auto& frame : child->Frames()) {
      ::intrinsic_proto::world::FrameReference frame_reference;
      frame_reference.mutable_by_name()->set_object_name(
          frame.ObjectName().value());
      frame_reference.mutable_by_name()->set_frame_name(frame.Name().value());
      available_frames.tool_jogging_frames.push_back(frame_reference);
    }
  }
  return available_frames;
}

absl::StatusOr<world::Frame> IconJoggingService::GetFrameIfAvailable(
    const intrinsic_proto::world::FrameReference& frame_reference,
    const std::vector<intrinsic_proto::world::FrameReference>& available_frames,
    std::string frame_type_for_error_message) {
  auto it = std::find_if(
      available_frames.begin(), available_frames.end(),
      [&frame_reference](const intrinsic_proto::world::FrameReference&
                             available_frame_reference) {
        return available_frame_reference.by_name().frame_name() ==
                   frame_reference.by_name().frame_name() &&
               available_frame_reference.by_name().object_name() ==
                   frame_reference.by_name().object_name();
      });
  if (it == available_frames.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat(frame_reference.by_name().frame_name(), " is not a valid ",
                     frame_type_for_error_message, " frame."));
  }
  return object_world_client_->GetFrame(*it);
};

::grpc::Status IconJoggingService::GetAvailableParts(
    ::grpc::ServerContext* context, const icon_proto::AvailablePartsRequest*,
    icon_proto::AvailablePartsResponse* response) {
  // We need to recalculate the part order every time since the world may
  // have been altered.
  INTR_ASSIGN_OR_RETURN_GRPC(std::vector<std::string> ordered_part_names,
                             GetPartOrderByKinematicPath());

  for (const auto& part_name : ordered_part_names) {
    const PartJoggingConfig& part_config = parts_.at(part_name);
    icon_proto::PartJoggingInfo part;
    part.set_part_name(part_name);
    part.set_num_dofs(part_config.num_dof);
    part.mutable_dof_names()->Add(part_config.dof_names.begin(),
                                  part_config.dof_names.end());
    *part.mutable_joint_limits() = ToProto(part_config.joint_limits);
    if (part_config.cartesian_limits.has_value()) {
      *part.mutable_cartesian_limits() = ToProto(*part_config.cartesian_limits);
    }
    if (part_config.cartesian_jogging_available) {
      INTR_ASSIGN_OR_RETURN_GRPC(AvailableJoggingFrames available_frames,
                                 GetValidJoggingFrames(part_name));
      part.mutable_static_jogging_frames()->Add(
          available_frames.static_jogging_frames.begin(),
          available_frames.static_jogging_frames.end());
      part.mutable_tool_jogging_frames()->Add(
          available_frames.tool_jogging_frames.begin(),
          available_frames.tool_jogging_frames.end());
    }
    part.set_cartesian_jogging_available(
        part_config.cartesian_jogging_available);

    double watchdog_timeout_in_seconds =
        JointJoggingInfo::kWatchdogTimeoutInSeconds;
    INTR_ASSIGN_OR_RETURN_GRPC(
        *part.mutable_stop_timeout(),
        FromAbslDuration(absl::Seconds(watchdog_timeout_in_seconds)));
    response->mutable_parts()->Add(std::move(part));
  }
  return ToGrpcStatus(absl::OkStatus());
}

::grpc::Status IconJoggingService::JogRobot(
    ::grpc::ServerContext* context,
    ::grpc::ServerReaderWriter<icon_proto::JoggingResponse,
                               icon_proto::JoggingRequest>* stream) {
  auto cancellable_stream = RegisterCancellableGrpcStream(context);
  return ToGrpcStatus(JogRobotImpl(context, stream));
}

absl::Status IconJoggingService::JogRobotImpl(
    ::grpc::ServerContext* context,
    ::grpc::ServerReaderWriter<icon_proto::JoggingResponse,
                               icon_proto::JoggingRequest>* stream) {
  icon_proto::JoggingRequest request;
  if (!stream->Read(&request)) {
    return absl::AbortedError("Failed to read initial request.");
  }
  if (parts_.empty()) {
    return absl::FailedPreconditionError(
        "No jogging parts are available. Please ensure a "
        "valid HalArmPart is configured in your ICON server.");
  }

  if (!request.has_initial_jogging_data()) {
    return absl::InvalidArgumentError(
        "Initial JoggingRequest is missing initial_jogging_data.");
  }
  if (request.initial_jogging_data().part_name().empty()) {
    return absl::InvalidArgumentError(
        "Initial JoggingRequest initial_jogging_data is missing arm_part.");
  }
  if (!parts_.contains(request.initial_jogging_data().part_name())) {
    return absl::InvalidArgumentError(
        absl::StrCat(request.initial_jogging_data().part_name(),
                     " is not a valid jogging part."));
  }
  const PartJoggingConfig& part_config =
      parts_.at(request.initial_jogging_data().part_name());
  // Parse the initial jogging specification.
  if (request.initial_jogging_data().has_initial_joint_jogging_spec()) {
    JointJoggingInfo::FixedParams fixed_params;
    *fixed_params.mutable_joint_limits() = ToProto(part_config.joint_limits);
    return RunJointJogging(context, stream,
                           request.initial_jogging_data().part_name(),
                           fixed_params);
  }
  return absl::InvalidArgumentError("No valid initial jogging spec provided.");
}

absl::Status IconJoggingService::RunJointJogging(
    ::grpc::ServerContext* context,
    ::grpc::ServerReaderWriter<icon_proto::JoggingResponse,
                               icon_proto::JoggingRequest>* stream,
    const std::string& part_name,
    const JointJoggingInfo::FixedParams& fixed_params) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<Session> session,
      Session::Start(icon_channel_, {std::string(part_name)}));
  LOG(INFO) << "Session started.";
  ActionDescriptor jogging_descriptor =
      intrinsic::icon::ActionDescriptor(JointJoggingInfo::kActionTypeName,
                                        ActionInstanceId(1), part_name)
          .WithFixedParams(fixed_params);
  INTR_ASSIGN_OR_RETURN(Action joint_jogging_action,
                        session->AddAction(jogging_descriptor));

  INTR_ASSIGN_OR_RETURN(
      auto joint_jogging_writer,
      session->StreamWriter<JointJoggingInfo::StreamingParams>(
          joint_jogging_action, JointJoggingInfo::kStreamingInputName));

  INTR_RETURN_IF_ERROR(session->StartAction(joint_jogging_action))
      << "While starting Action";

  // Send initial response to the client.
  icon_proto::JoggingResponse jogging_response;

  if (!stream->Write(jogging_response)) {
    return absl::AbortedError(
        "Failed to write streaming response to the jogging client. Assuming "
        "the client is disconnected and ending session.");
  }

  icon_proto::JoggingRequest jogging_request;
  while (stream->Read(&jogging_request)) {
    // First we check that the request is valid.
    if (jogging_request.has_initial_jogging_data()) {
      return absl::InvalidArgumentError(
          "Cannot send initial jogging data while active jogging session is "
          "open.");
    }

    if (!jogging_request.has_jogging_command() ||
        !jogging_request.jogging_command().has_joint_jogging_command()) {
      return absl::InvalidArgumentError("Missing joint jogging command.");
    }

    auto joint_jogging_command =
        jogging_request.jogging_command().joint_jogging_command();
    int joint_index = joint_jogging_command.joint_index();
    if (joint_index >= parts_.at(part_name).num_dof) {
      return absl::InvalidArgumentError(
          "Invalid joint index requested. Requested index is greater than the "
          "number of joints.");
    }

    if (!IsValidNormalizedVelocity(
            joint_jogging_command.normalized_velocity())) {
      return CreateInvalidJointVelocityError(joint_index);
    }

    eigenmath::VectorNd velocity_command =
        eigenmath::VectorNd::Constant(parts_.at(part_name).num_dof, 0);

    velocity_command[joint_index] =
        joint_jogging_command.normalized_velocity() *
        parts_.at(part_name)
            .joint_limits.max_velocity[jogging_request.jogging_command()
                                           .joint_jogging_command()
                                           .joint_index()];

    JointJoggingInfo::StreamingParams move_params;
    *move_params.mutable_goal_velocity()->mutable_joints() = {
        velocity_command.begin(), velocity_command.end()};
    INTR_RETURN_IF_ERROR(joint_jogging_writer->Write(move_params));
    icon_proto::JoggingResponse jogging_response;
    if (!stream->Write(jogging_response)) {
      return absl::AbortedError(
          "Failed to write streaming response to the client. Assuming the "
          "client is dead and ending session.");
    }
  }
  return absl::OkStatus();
}

IconJoggingService::IconJoggingService(
    std::shared_ptr<intrinsic::ChannelInterface> icon_channel,
    std::unique_ptr<icon::Client> icon_client,
    std::shared_ptr<const world::ObjectWorldClient> object_world_client,
    absl::flat_hash_map<std::string, PartJoggingConfig> parts)
    : icon_channel_(icon_channel),
      icon_client_(std::move(icon_client)),
      object_world_client_(object_world_client),
      parts_(parts),
      shutdown_notification_(std::make_shared<absl::Notification>()) {}

absl::StatusOr<
    std::unique_ptr<::intrinsic_proto::icon::v1::JoggingService::Service>>
IconJoggingService::Create(
    std::shared_ptr<intrinsic::ChannelInterface> icon_channel,
    std::shared_ptr<const world::ObjectWorldClient> object_world_client) {
  if (object_world_client == nullptr) {
    LOG(WARNING) << "Failed to get ICON world service when creating jogging "
                    "service. Starting dummy jogging service.";
    return std::make_unique<DummyIconJoggingService>();
  }

  auto icon_client = std::make_unique<icon::Client>(icon_channel);
  INTR_ASSIGN_OR_RETURN(RobotConfig icon_config, icon_client->GetConfig());

  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::icon::v1::ActionSignature> action_signatures,
      icon_client->ListActionSignatures());

  auto it_joint_jogging_signatures = std::find_if(
      action_signatures.begin(), action_signatures.end(),
      [](const intrinsic_proto::icon::v1::ActionSignature& signature) {
        return signature.action_type_name() ==
               JointJoggingInfo::kActionTypeName;
      });
  bool has_cartesian_jogging_signature = false;
  absl::flat_hash_map<std::string, PartJoggingConfig> parts;

  // Create a service without any parts if there are no jogging actions
  // supported. We return early to avoid special logic due to errors when
  // checking for compatible parts if the action types are not registered.
  if (it_joint_jogging_signatures == action_signatures.end() &&
      !has_cartesian_jogging_signature) {
    return absl::WrapUnique(
        new IconJoggingService(icon_channel, std::move(icon_client),
                               object_world_client, std::move(parts)));
  }

  std::vector<std::string> joint_jogging_compatible_parts;
  if (it_joint_jogging_signatures != action_signatures.end()) {
    INTR_ASSIGN_OR_RETURN(
        joint_jogging_compatible_parts,
        icon_client->ListCompatibleParts({JointJoggingInfo::kActionTypeName}));
  }

  absl::flat_hash_set<std::string> cartesian_jogging_compatible_parts;
  // A part may support joint jogging, cartesian jogging, or both. Union and
  // deduplicate the compatible parts so configuration is populated for any
  // part that supports at least one active jogging mode.
  std::vector<std::string> joggable_parts = joint_jogging_compatible_parts;
  for (const auto& part_name : cartesian_jogging_compatible_parts) {
    if (std::find(joint_jogging_compatible_parts.begin(),
                  joint_jogging_compatible_parts.end(),
                  part_name) == joint_jogging_compatible_parts.end()) {
      joggable_parts.push_back(part_name);
    }
  }

  for (const auto& part_name : joggable_parts) {
    PartJoggingConfig part_config;
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::icon::GenericPartConfig generic_part_config,
        icon_config.GetGenericPartConfig(part_name));
    part_config.num_dof =
        generic_part_config.joint_position_config().num_joints();
    INTR_ASSIGN_OR_RETURN(
        part_config.joint_limits,
        GetClampedJointLimits(
            generic_part_config.joint_limits_config().application_limits()));
    INTR_ASSIGN_OR_RETURN(std::string hardware_resource_name,
                          icon_config.GetHardwareResourceName(part_name));
    if (hardware_resource_name.empty()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Part ", part_name, " is missing a hardware resource name."));
    }
    // Get the world object from the world service to ensure existence for
    // later and fail early if it doesn't exist.
    INTR_ASSIGN_OR_RETURN(world::KinematicObject kinematic_object,
                          object_world_client->GetKinematicObject(
                              WorldObjectName(hardware_resource_name)));
    INTR_ASSIGN_OR_RETURN(part_config.dof_names,
                          GetDofNames(kinematic_object.Proto()));
    part_config.kinematic_object_name = hardware_resource_name;
    if (cartesian_jogging_compatible_parts.contains(part_name)) {
      part_config.cartesian_jogging_available = true;
      INTR_ASSIGN_OR_RETURN(part_config.cartesian_limits,
                            GetClampedCartesianLimits(
                                generic_part_config.cartesian_limits_config()
                                    .default_cartesian_limits()));
    }
    parts.insert({part_name, part_config});
  }

  return absl::WrapUnique(
      new IconJoggingService(icon_channel, std::move(icon_client),
                             object_world_client, std::move(parts)));
}

IconJoggingService::ScopedCancellableGrpcContext
IconJoggingService::RegisterCancellableGrpcStream(
    ::grpc::ServerContext* context) {
  return ScopedCancellableGrpcContext(*this, context);
}

IconJoggingService::ScopedCancellableGrpcContext::ScopedCancellableGrpcContext(
    IconJoggingService& service, ::grpc::ServerContext* context)
    : service_(service),
      context_(context),
      shutdown_notification_(service.GetShutdownNotification()) {
  absl::MutexLock l(service_.open_streams_mutex_);
  service_.open_grpc_streams_.insert(context_);
}

IconJoggingService::ScopedCancellableGrpcContext::
    ~ScopedCancellableGrpcContext() {
  absl::MutexLock l(service_.open_streams_mutex_);
  service_.open_grpc_streams_.erase(context_);
}

IconJoggingService::~IconJoggingService() {
  // Cancel all open grpc streams.
  {
    absl::MutexLock l(open_streams_mutex_);
    LOG(INFO) << "Trying to cancel " << open_grpc_streams_.size()
              << " grpc streams and close sessions.";
    for (const auto& context : open_grpc_streams_) {
      context->TryCancel();
    }

    LOG(INFO) << "Cancelled all grpc streams.";
    if (open_streams_mutex_.AwaitWithTimeout(
            absl::Condition(this, &IconJoggingService::HasNoOpenStreams),
            absl::Seconds(1))) {
      LOG(INFO) << "All grpc streams are closed.";
    } else {
      LOG(WARNING) << "Failed to close grpc streams within timeout.";
      // Signalling this Notification effectively "disarms" any remaining
      // ScopedCancellableGrpcContext objects. Otherwise,if they are destroyed
      // after we exit this destructor, they will access invalid memory (i.e.
      // the old `this` pointer for this JoggingService).
      shutdown_notification_->Notify();
    }
  }
}

bool IconJoggingService::HasNoOpenStreams() const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_) {
  return open_grpc_streams_.empty();
}

}  // namespace intrinsic::icon
