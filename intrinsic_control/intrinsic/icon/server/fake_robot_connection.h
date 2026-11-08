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

#ifndef INTRINSIC_ICON_SERVER_FAKE_ROBOT_CONNECTION_H_
#define INTRINSIC_ICON_SERVER_FAKE_ROBOT_CONNECTION_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/operational_state_interface.h"
#include "intrinsic/icon/server/part_collection.h"
#include "intrinsic/icon/server/robot_connection_interface.h"
#include "intrinsic/icon/server/session_interface.h"

namespace intrinsic::icon {

class FakeRobotConnection final : public RobotConnectionInterface {
 public:
  // These constructors create a FakeRobotConnection that uses the given
  // arguments to implement RobotConnectionInterface. See the private ctor below
  // for information on the values for those parts of the interface that aren't
  // explicitly passed in.
  FakeRobotConnection(const intrinsic_proto::icon::v1::ServerConfig& config,
                      const absl::flat_hash_set<std::string>& part_names);
  FakeRobotConnection(const intrinsic_proto::icon::v1::ServerConfig& config,
                      PartCollection part_collection);
  FakeRobotConnection(
      const intrinsic_proto::icon::v1::ServerConfig& config,
      const absl::flat_hash_set<std::string>& part_names,
      absl::flat_hash_map<std::string,
                          intrinsic_proto::icon::v1::ActionSignature>
          action_signatures);
  FakeRobotConnection(const intrinsic_proto::icon::v1::ServerConfig& config,
                      const absl::flat_hash_set<std::string>& part_names,
                      ::intrinsic::icon::CreateSession create_session);
  FakeRobotConnection(
      const intrinsic_proto::icon::v1::ServerConfig& config,
      const absl::flat_hash_set<std::string>& part_names,
      std::shared_ptr<OperationalStateInterface> operational_state);

  absl::StatusOr<std::unique_ptr<SessionInterface>> CreateSession(
      const absl::flat_hash_set<std::string>& part_names, SessionId session_id,
      const RealtimeLogContext& context) override;

  OperationalStateInterface& MutableOperationalStateInterface() override;

  // Returns a map from Action type name to ActionSignature.
  absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::ActionSignature>
  ActionTypeToSignature() const override;
  PartCollection& MutablePartCollection() override;
  const PartCollection& GetPartCollection() const override;
  const intrinsic_proto::icon::v1::ServerConfig& config() const override;

  absl::Status ActionCompatibleWithSlotPartMap(
      absl::string_view action_type_name,
      const SlotPartMap& slot_part_map) const override;

  absl::Status ActionCompatibleWithPart(
      absl::string_view action_type_name,
      absl::string_view part_name) const override;

  void SetSpeedOverride(double new_speed_override) override;

  double GetSpeedOverride() const override;

  void SetLoggingMode(LoggingMode logging_mode) override;

  LoggingMode GetLoggingMode() const override;

  // Returns the fake operational state for all parts.
  absl::StatusOr<absl::flat_hash_map<std::string, OperationalState>>
  GetPartStates() override;

 private:
  // Creates a RobotConnection with the given parameters. All but `config` and
  // `part_names` are optional, and will default to basic fakes.
  //
  // Internally creates a FakePartCollection using `part_names`, whose
  // StatusGetter returns an empty list of statuses. For more advanced
  // PartCollections, please use a MockRobotConnection.
  //
  // If `action_signatures` is nullopt, creates a signature map containing a
  // no-op action ( intrinsic/icon/server/example/noop_action.h )
  // that is compatible with any part.
  //
  // If `create_session` is nullptr, FakeRobotConnection uses
  // FakeSession (
  // intrinsic/icon/server/testing/fake_session.h ).
  // Otherwise, FakeRobotConnection takes ownership of it.
  //
  // If `operational_state` is nullptr, creates an instance of
  // FakeOperationalState (
  // intrinsic/icon/server/testing/fake_operational_state.h ).
  FakeRobotConnection(
      const intrinsic_proto::icon::v1::ServerConfig& config,
      const absl::flat_hash_set<std::string>& part_names,
      std::optional<absl::flat_hash_map<
          std::string, intrinsic_proto::icon::v1::ActionSignature>>
          action_signatures,
      ::intrinsic::icon::CreateSession create_session,
      std::shared_ptr<OperationalStateInterface> operational_state);

  const intrinsic_proto::icon::v1::ServerConfig config_;
  PartCollection part_collection_;
  const absl::flat_hash_map<std::string,
                            intrinsic_proto::icon::v1::ActionSignature>
      action_signatures_;
  absl::Mutex create_session_mutex_;
  ::intrinsic::icon::CreateSession create_session_fn_
      ABSL_GUARDED_BY(create_session_mutex_);
  // operational_state_interface_ is shared to allow direct access to it in
  // tests using the FakeRobotConnection. This is required to change the state
  // to DISABLED.
  std::shared_ptr<OperationalStateInterface> operational_state_interface_;
  mutable absl::Mutex speed_override_mutex_;
  double speed_override_ ABSL_GUARDED_BY(speed_override_mutex_) = 1.0;
  mutable absl::Mutex logging_mode_mutex_;
  LoggingMode logging_mode_ ABSL_GUARDED_BY(logging_mode_mutex_) =
      LoggingMode::kThrottled;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_SERVER_FAKE_ROBOT_CONNECTION_H_
