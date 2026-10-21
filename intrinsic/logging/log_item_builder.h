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

#ifndef INTRINSIC_LOGGING_LOG_ITEM_BUILDER_H_
#define INTRINSIC_LOGGING_LOG_ITEM_BUILDER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/executive/proto/clips_snapshot.pb.h"
#include "intrinsic/executive/proto/log_items.pb.h"
#include "intrinsic/icon/proto/cart_space.pb.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/logging/errors/proto/error_report.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/logging/proto/flowstate_event.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/motion_planning/path_planning/path_planner.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/world/service/world_compatibility_service.pb.h"
#include "intrinsic/world/world.pb.h"

namespace intrinsic::data_logger {

// Builder for LogItem. LogItems are constructed by calling one of the factory
// methods, followed by optional metadata and context setters. Finally, the
// composed LogItem can be retrieved by calling Item().
//
// Example usage:
// Builder::From(world)
//   .WithMetadata(time)        // optional.
//   .WithContext({parent_id})  // optional.
//   .Item();
//
// All With* calls are optional. The acquisition time of the LogItem is set to
// absl::TimeNow, if not overwritten manually using 'WithMetadata'.
class Builder {
 public:
  explicit Builder(intrinsic_proto::data_logger::LogItem&& item);

  static Builder From(const google::protobuf::Any& any_proto);
  template <typename T>
  static Builder PackAnyFrom(const T& proto_message) {
    google::protobuf::Any any_proto;
    static_cast<void>(
        any_proto.PackFrom(proto_message));  // TODO: Handle return value
    return From(any_proto);
  }
  static Builder From(
      const intrinsic_proto::executive::LoggedOperation& logged_operation,
      bool is_for_history = false);
  static Builder From(const intrinsic_proto::icon::JointState& joint_state);
  static Builder From(const intrinsic_proto::icon::RobotStatus& robot_status);
  static Builder From(const intrinsic_proto::skills::ExecutionSummary& summary);

  // This should generally not be called anywhere but in the executive.
  // It already collects a complete status hierarchy and we would want
  // to log that only once.
  static Builder From(const intrinsic_proto::status::ExtendedStatus& status);

  // Optional. Sets metadata.event_source.
  Builder& WithEventSource(std::string_view event_source) &
      ABSL_ATTRIBUTE_LIFETIME_BOUND;
  Builder&& WithEventSource(std::string_view event_source) &&
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Optional.
  Builder& WithContext(const intrinsic_proto::data_logger::Context& context) &
      ABSL_ATTRIBUTE_LIFETIME_BOUND;
  Builder&& WithContext(const intrinsic_proto::data_logger::Context& context) &&
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Optional. Sets metadata.uid. If unset, a random ID is generated.
  Builder& WithUid(uint64_t uid) & ABSL_ATTRIBUTE_LIFETIME_BOUND;
  Builder&& WithUid(uint64_t uid) && ABSL_ATTRIBUTE_LIFETIME_BOUND;

  // Use to get LogItem when finished building.
  const intrinsic_proto::data_logger::LogItem& Item() const&;
  intrinsic_proto::data_logger::LogItem&& Item() &&
      ABSL_ATTRIBUTE_LIFETIME_BOUND;

 protected:
  Builder() = default;
  intrinsic_proto::data_logger::LogItem item_;
};

// Builder for ErrorReport. An ErrorReport is constructed by calling the
// factory method, followed by optional setters. Finally, the composed
// ErrorReport can be retrieved by calling Report().
//
// Example usage:
// auto report = intrinsic::error::Builder::From(status, "Detection failed")
//     .WithCategory(category)    // optional
//     .WithInstruction(text)     // optional
//     .WithData(image)           // optional
//     .WithData(configs)         // optional
//     .Report();
//
// Note that even if an ErrorReport does not follow all guidelines (see method
// documentation), 'Report' will return the object. Use 'Valid' if you want to
// check whether the generated report follows all guidelines.
class ErrorBuilder : public data_logger::Builder {
 public:
  // Factory for the Builder. 'human_readable_summary' should be a <10 words
  // human readable summary of the error and not empty.
  static ErrorBuilder From(const absl::Status& status,
                           std::string_view human_readable_summary);

  // Optional.
  ErrorBuilder& WithCategory(
      intrinsic_proto::error::ErrorReport::Description::Category category) {
    item_.mutable_payload()
        ->mutable_error_report()
        ->mutable_description()
        ->set_category(category);
    return *this;
  }

  // Optional. Explains how to attempt to resolve the error.
  // You may use this function to add multiple instructions at once.
  ErrorBuilder& WithInstructions(
      const std::vector<std::string>& human_readable_instructions) {
    for (const std::string& i : human_readable_instructions) {
      item_.mutable_payload()
          ->mutable_error_report()
          ->mutable_instructions()
          ->add_items()
          ->set_human_readable(i);
    }
    return *this;
  }

  // Optional. Explains how to attempt to resolve the error. Can be called
  // multiple times to attach multiple alternatives for error resolution.
  ErrorBuilder& WithInstruction(std::string_view human_readable_instruction) {
    item_.mutable_payload()
        ->mutable_error_report()
        ->mutable_instructions()
        ->add_items()
        ->set_human_readable(human_readable_instruction);
    return *this;
  }

  // Optional. Can be called multiple times to attach multiple items.
  template <typename T, typename = std::enable_if_t<
                            std::is_base_of_v<google::protobuf::Message, T>>>
  ErrorBuilder& WithData(const T& proto_message) {
    static_cast<void>(
        item_.mutable_payload()
            ->mutable_error_report()
            ->mutable_data()
            ->add_items()
            ->mutable_data()
            ->PackFrom(proto_message));  // TODO: Handle return value
    return *this;
  }

  // Optional. Can be called multiple times to attach multiple items.
  // This enables to potentially append data, i.e., only if the optional
  // actually carries a value it is appended.
  template <typename T, typename = std::enable_if_t<
                            std::is_base_of_v<google::protobuf::Message, T>>>
  ErrorBuilder& WithOptionalData(const std::optional<T>& opt_proto_message) {
    if (opt_proto_message.has_value()) {
      static_cast<void>(
          item_.mutable_payload()
              ->mutable_error_report()
              ->mutable_data()
              ->add_items()
              ->mutable_data()
              ->PackFrom(
                  opt_proto_message.value()));  // TODO: Handle return value
    }
    return *this;
  }

  // Optional. Can be called multiple times to attach multiple items.
  ErrorBuilder& WithClipsSnapshot(
      const intrinsic_proto::executive::ClipsSnapshot& clips_snapshot) {
    *item_.mutable_payload()
         ->mutable_error_report()
         ->mutable_data()
         ->add_items()
         ->mutable_clips_snapshot() = clips_snapshot;
    return *this;
  }

  // Optional. Can be called multiple times to attach multiple items.
  template <typename T, typename = std::enable_if_t<
                            std::is_base_of_v<google::protobuf::Message, T>>>
  ErrorBuilder& WithData(const absl::StatusOr<T>& status_or_proto) {
    if (status_or_proto.ok()) {
      static_cast<void>(
          item_.mutable_payload()
              ->mutable_error_report()
              ->mutable_data()
              ->add_items()
              ->mutable_data()
              ->PackFrom(
                  status_or_proto.value()));  // TODO: Handle return value
    } else {
      *item_.mutable_payload()
           ->mutable_error_report()
           ->mutable_data()
           ->add_items()
           ->mutable_status() = SaveStatusAsRpcStatus(status_or_proto.status());
    }
    return *this;
  }

  // Returns an error if the resulting ErrorReport is invalid.
  absl::Status Valid() const;

 private:
  explicit ErrorBuilder(const intrinsic_proto::error::ErrorReport& report)
      : Builder() {
    *item_.mutable_payload()->mutable_error_report() = report;
    item_.mutable_metadata()->set_event_source("error_report");
  }
};

}  // namespace intrinsic::data_logger

#endif  // INTRINSIC_LOGGING_LOG_ITEM_BUILDER_H_
