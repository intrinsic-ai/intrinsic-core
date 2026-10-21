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

#ifndef INTRINSIC_ICON_UTILS_INSPECTION_PUBLISHER_H_
#define INTRINSIC_ICON_UTILS_INSPECTION_PUBLISHER_H_

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"
#include "intrinsic/assets/services/proto/v1/service_inspection.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

// Convenience wrapper around a PubSub publisher for inspection data.
class InspectionPublisher {
 public:
  InspectionPublisher() = delete;
  InspectionPublisher(const InspectionPublisher&) = delete;
  InspectionPublisher& operator=(const InspectionPublisher&) = delete;
  InspectionPublisher(InspectionPublisher&&) = default;
  InspectionPublisher& operator=(InspectionPublisher&&) = default;

  // Creates an inspection publisher for the given service name and service
  // inspection topic. The service name is should be the name of the service
  // instance. The service inspection topic is the topic name to which the
  // inspection data is published and is provided by the RuntimeContext proto
  // (intrinsic/resources/proto/runtime_context.proto).
  static absl::StatusOr<InspectionPublisher> Create(
      absl::string_view service_name,
      absl::string_view service_inspection_topic) {
    if (service_name.empty()) {
      return absl::InvalidArgumentError(
          "Cannot create inspection publisher with empty service name.");
    }
    if (service_inspection_topic.empty()) {
      return absl::InvalidArgumentError(
          "Cannot create inspection publisher with empty service inspection "
          "topic name.");
    }
    PubSub pub_sub(service_name);
    INTR_ASSIGN_OR_RETURN(
        auto inspection_publisher,
        pub_sub.CreatePublisher(service_inspection_topic, TopicConfig()));
    return InspectionPublisher(std::move(pub_sub),
                               std::move(inspection_publisher));
  }

  // Publishes the given message to the inspection topic. Wraps the given
  // message in a ServiceInspectionData proto.
  absl::Status Publish(const google::protobuf::Message& message,
                       absl::Time timestamp = absl::Now()) const {
    intrinsic_proto::services::v1::ServiceInspectionData inspection_data;
    if (!inspection_data.mutable_data()->PackFrom(message)) {
      return absl::InternalError(
          "Failed to pack message into ServiceInspectionData.");
    }
    INTR_RETURN_IF_ERROR(intrinsic::FromAbslTime(
        timestamp, inspection_data.mutable_timestamp()));
    return inspection_publisher_.Publish(inspection_data);
  }

  absl::string_view GetTopicName() const {
    return inspection_publisher_.TopicName();
  }

 private:
  explicit InspectionPublisher(PubSub&& pub_sub,
                               Publisher&& inspection_publisher)
      : pub_sub_(std::move(pub_sub)),
        inspection_publisher_(std::move(inspection_publisher)) {}
  PubSub pub_sub_;
  Publisher inspection_publisher_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_UTILS_INSPECTION_PUBLISHER_H_
