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

#include "intrinsic/hardware/gpio/opcua_gpio_service.h"

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"
#include "intrinsic/hardware/gpio/gpio_service_proto_utils.h"
#include "intrinsic/hardware/gpio/opcua_gpio_data_types.h"
#include "intrinsic/hardware/gpio/opcua_node_map.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.pb.h"
#include "intrinsic/hardware/opcua/opcua_client.h"
#include "intrinsic/hardware/opcua/opcua_raii_types.h"
#include "intrinsic/hardware/opcua/opcua_read_response.h"
#include "intrinsic/hardware/opcua/opcua_write_request.h"
#include "intrinsic/hardware/opcua/opcua_write_response.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/util.h"
#include "open62541/client_highlevel.h"
#include "open62541/common.h"
#include "open62541/types.h"
#include "open62541/types_generated.h"
#include "open62541/types_generated_handling.h"

namespace intrinsic {
namespace {

using ::intrinsic::opcua::OpcuaMultiKeyNodeMap;
using Authentication = ::intrinsic::opcua::OpcuaClient::Authentication;
using OnConnectionError = ::intrinsic::opcua::OpcuaClient::OnConnectionError;

using ScalarSignalValue = intrinsic_proto::gpio::v1::SignalValue;
using ArraySignalValue = std::vector<intrinsic_proto::gpio::v1::SignalValue>;

constexpr absl::Duration kReadPollingDelay = absl::Milliseconds(200);
constexpr absl::Duration kConnectionRetryPeriod = absl::Seconds(1);

// Upon receiving a gRPC call without already having a connection established
// with the OPCUA server, how long to wait for the connection before failing.
const absl::Duration kWaitForConnectionTimeout = 3 * kConnectionRetryPeriod;

// A local representation of an OPC-UA object containing relevant quantities
// and metadata for server function.
struct GpioOpcuaNode {
  // OPC-UA Connection information
  UA_NodeId node_id;

  std::string xml_node_id = "";

  // Node can contain a scalar value or an array for the variant.
  UA_Variant variant;

  std::string pubsub_topic_name;

  // If true, then a session claims this symbol description.
  std::atomic<bool> claimed_by_write_session;

  GpioOpcuaNode() {
    claimed_by_write_session = false;
    UA_Variant_init(&variant);
    UA_NodeId_init(&node_id);
  }
  explicit GpioOpcuaNode(const UA_NodeId& id, absl::string_view xml_id)
      : node_id(id), xml_node_id(xml_id) {
    claimed_by_write_session = false;
    UA_Variant_init(&variant);
  }
  ~GpioOpcuaNode() {
    // De-allocates heap allocated data members
    UA_Variant_clear(&variant);

    // De-allocates heap allocated data members
    UA_NodeId_clear(&node_id);
  }

  void SetScalarValue(const intrinsic_proto::gpio::v1::SignalValue& value) {
    signal_value = value;
  }

  bool HoldsScalar() const {
    return std::holds_alternative<ScalarSignalValue>(signal_value);
  }

  bool HoldsArray() const {
    return std::holds_alternative<ArraySignalValue>(signal_value);
  }

  absl::Status SetScalarValue(const UA_Variant& v) {
    INTR_ASSIGN_OR_RETURN(signal_value,
                          intrinsic::gpio::MakeSignalValueFromVariantValue(v));
    return absl::OkStatus();
  }

  absl::Status SetArrayValue(const UA_Variant& v) {
    INTR_ASSIGN_OR_RETURN(
        signal_value, intrinsic::gpio::MakeArraySignalValueFromVariantValue(v));
    return absl::OkStatus();
  }

  // NOTE: this should **only** be called after verifying that the node holds
  // a scalar value.
  ScalarSignalValue GetScalarValue() const {
    return std::get<ScalarSignalValue>(signal_value);
  }

  // NOTE: this should **only** be called after verifying that the node holds
  // an array value.
  const ArraySignalValue& GetArrayValue() const {
    return std::get<ArraySignalValue>(signal_value);
  }

  absl::StatusOr<ScalarSignalValue> GetValueAtIndex(
      std::optional<int> index) const {
    if (HoldsScalar()) {
      if (index.has_value()) {
        return absl::InvalidArgumentError(
            "Node holds a scalar value but provided index.");
      }
      return GetScalarValue();
    }

    if (HoldsArray()) {
      if (!index.has_value()) {
        return absl::InvalidArgumentError(
            "Node holds an array value but no index provided.");
      }
      if (index < 0 || index >= GetArrayValue().size()) {
        return absl::InvalidArgumentError(
            absl::StrCat("Index ", *index, " out of range [0, ",
                         GetArrayValue().size(), ")."));
      }
      return GetArrayValue()[*index];
    }

    return absl::InternalError("Node holds no value.");
  }

 private:
  // This is private so that access is done via explicit scalar or array
  // methods.
  std::variant<ScalarSignalValue, ArraySignalValue> signal_value;
};

constexpr absl::string_view kSignalIndexSeparator = ".";

// Helper functions to extract node name and index from signal name.
std::pair<std::string, std::optional<int>> GetNodeNameWithIndexFromSignalName(
    const absl::string_view signal_name) {
  std::vector<std::string> s =
      absl::StrSplit(signal_name, kSignalIndexSeparator);

  // If got more than one string, then check if the last string is an index.
  if (s.size() > 1) {
    // Note: we are assuming that index is not negative.
    int index = 0;
    if (absl::SimpleAtoi(s[s.size() - 1], &index)) {
      return std::make_pair(absl::StrJoin(s.begin(), s.end() - 1, ""), index);
    }
  }

  return std::make_pair(std::string(signal_name), std::nullopt);
}

// Helper method for parsing the collection of OPC-UA XML strings containing
// the node identifications.
absl::StatusOr<std::vector<std::unique_ptr<GpioOpcuaNode>>> ParseNodeIdStrings(
    const std::vector<std::string>& node_ids) {
  std::vector<std::unique_ptr<GpioOpcuaNode>> opcua_nodes;
  for (const auto& node_id : node_ids) {
    UA_NodeId id;
    UA_StatusCode status =
        UA_NodeId_parse(&id, UA_STRING(const_cast<char*>(node_id.c_str())));
    if (status != UA_STATUSCODE_GOOD) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid NodeId: ", node_id, ": ", UA_StatusCode_name(status)));
    }
    auto opcua_node = std::make_unique<GpioOpcuaNode>(id, node_id);
    opcua_nodes.push_back(std::move(opcua_node));
  }

  return std::move(opcua_nodes);
}

class WriteSession {
 public:
  static absl::StatusOr<std::unique_ptr<WriteSession>> Create(
      const intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          InitialSessionData& initial_session_data,
      const absl::string_view opcua_server_address, const Authentication& auth,
      const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>& opcua_node_map) {
    OpcuaMultiKeyNodeMap<GpioOpcuaNode*> write_session_node_map;
    for (const auto& signal_name : initial_session_data.signal_names()) {
      const auto& [node_name, index] =
          GetNodeNameWithIndexFromSignalName(signal_name);
      auto node = opcua_node_map.Find(node_name);

      if (node == opcua_node_map.end()) {
        return absl::NotFoundError(
            absl::StrCat("Requested signal ", signal_name,
                         " not found in available signals."));
      }

      if (node->second->claimed_by_write_session) {
        return absl::FailedPreconditionError(
            absl::StrCat("Requested signal ", signal_name,
                         " already claimed by another write session."));
      }

      // Use display name to insert the node so that name lookups using both the
      // display name and the string identifier works.
      const auto& display_name = node->first;
      const auto& gpio_node = node->second;
      write_session_node_map.Insert(gpio_node->node_id, display_name,
                                    gpio_node->xml_node_id, gpio_node);
    }
    auto session = std::make_unique<WriteSession>(
        std::move(write_session_node_map), opcua_server_address, auth);

    // We break this out of the constructor so we can return an error if the
    // connection fails.
    {
      absl::MutexLock client_lock(&session->client_mutex_);
      INTR_RETURN_IF_ERROR(session->client_.Connect()).LogError();
    }
    // Now that we have successfully connected, reserve the signals for this
    // write session. This should always succeed since we checked if signals
    // were already claimed above and we are holding the node map mutex.
    session->ClaimSignals();

    return std::move(session);
  }

  void ClaimSignals() {
    for (const auto& node : opcua_node_map_) {
      node.second->claimed_by_write_session = true;
    }
  }

  ~WriteSession() {
    if (keepalive_thread_.joinable()) {
      end_keepalive_thread_.Notify();
      keepalive_thread_.join();
    }

    for (const auto& symbol : opcua_node_map_) {
      symbol.second->claimed_by_write_session = false;
    }
  }

  // Executes the opcua write operation.
  absl::Status ExecuteWriteOperation(
      const ::intrinsic::opcua::WriteRequest& write_request) {
    intrinsic::opcua::WriteResponse write_response;

    // Hold the mutex __only__ for the duration of the write operation.
    {
      absl::MutexLock client_lock(&client_mutex_);
      write_response =
          client_.Write(write_request, OnConnectionError::kTryReconnect);
    }
    // Note that the order of checking the error matters.
    const auto& response = write_response.response;

    // 1. Checks that service result is good.
    if (const auto ret = response.responseHeader.serviceResult;
        ret != UA_STATUSCODE_GOOD) {
      auto error_msg =
          absl::StrCat("Writing to nodes failed with service result: ",
                       UA_StatusCode_name(ret), ".");
      LOG(ERROR) << error_msg;
      return absl::InternalError(error_msg);
    }

    // 2. Now let's check if all the signals were written to.
    if (response.resultsSize != write_request.request.nodesToWriteSize) {
      auto error_msg =
          absl::StrCat("Unable to write to all the nodes. Expected: ",
                       write_request.request.nodesToWriteSize,
                       ". Managed: ", response.resultsSize, ".");
      LOG(ERROR) << error_msg;
      return absl::InternalError(error_msg);
    }

    // 3. Confirms that `results` pointer is not null when it should not. This
    //    should only happen due to a bug in open62541 library. But let's check
    //    it anyway to avoid segfaulting the service.
    if (response.resultsSize > 0 && response.results == nullptr) {
      return absl::InternalError(
          "Unable to confirm that writing to nodes was successful.");
    }

    // 4. Finally, checks the write result for each node and returns the first
    //    error found.
    for (auto i = 0; i < response.resultsSize; ++i) {
      const auto ret = response.results[i];
      if (ret != UA_STATUSCODE_GOOD) {
        auto error_msg =
            absl::StrCat("Writing to nodes failed with service result: ",
                         UA_StatusCode_name(ret));
        LOG(ERROR) << error_msg;
        return absl::InternalError(error_msg);
      }
    }

    return absl::OkStatus();
  }

  absl::Status HandleWriteOperation(
      const intrinsic_proto::gpio::v1::WriteSignalsRequest& request) {
    // First, iterate over all the signals and extract node ids (with optional
    // index) and desired values to write.
    std::vector<UA_NodeId> nodes;
    std::vector<std::pair<std::optional<int>, ScalarSignalValue>> write_values;
    nodes.reserve(request.signal_values().values_size());
    write_values.reserve(request.signal_values().values_size());
    for (const auto& signal : request.signal_values().values()) {
      const auto& [node_name, index] =
          GetNodeNameWithIndexFromSignalName(signal.first);

      // Is this a valid signal?
      auto node_iter = opcua_node_map_.Find(node_name);
      if (node_iter == opcua_node_map_.end()) {
        LOG(INFO) << "WriteSession::HandleWriteOperation(): Signal "
                  << node_name << " unknown.";
        return absl::NotFoundError(
            absl::StrCat("Requested signal ", node_name,
                         " is unknown to this WriteSession."));
      }

      // A write-only signal will have an empty UA_Variant. So, only check
      // that the variant and signal types match for readable signals.
      if (!UA_Variant_isEmpty(&node_iter->second->variant)) {
        // TODO(b/352610980): `VariantTypeToSignalType` should return the
        // correct type independent of storing scalar or array values.
        // For now, create a temporary scalar variant to determine the type.
        UA_Variant scalar_variant;
        UA_Variant_init(&scalar_variant);
        scalar_variant.type = node_iter->second->variant.type;
        scalar_variant.data =
            const_cast<void*>(node_iter->second->variant.data);
        INTR_RETURN_IF_ERROR(intrinsic::gpio::VariantAndSignalTypesMatch(
                                 &scalar_variant, signal.second))
            .LogError();
      }

      LOG(INFO) << "Adding node name: " << node_name;

      nodes.push_back(node_iter->second->node_id);
      write_values.push_back(std::make_pair(index, signal.second));
    }

    // Now that we have processed the incoming request, let's convert this
    // into opcua language.
    ::intrinsic::opcua::WriteRequest write_request(nodes);
    int node_index = 0;
    for (const auto& [index, value] : write_values) {
      if (index.has_value()) {
        INTR_RETURN_IF_ERROR(::intrinsic::gpio::AddArrayValueToRequest(
                                 node_index, value, *index, write_request))
            .LogError();
      } else {
        INTR_RETURN_IF_ERROR(::intrinsic::gpio::AddScalarValueToRequest(
                                 node_index, value, write_request))
            .LogError();
      }
      node_index++;
    }
    return ExecuteWriteOperation(write_request);
  }

  WriteSession(const WriteSession&) = delete;
  WriteSession& operator=(const WriteSession&) = delete;

  WriteSession(OpcuaMultiKeyNodeMap<GpioOpcuaNode*> opcua_node_map,
               const absl::string_view opcua_server_address,
               const Authentication& auth)
      : opcua_node_map_(std::move(opcua_node_map)),
        client_(opcua_server_address, auth) {}

  absl::Status StartKeepAliveThread() {
    keepalive_thread_ =
        Thread([this]() { this->RunKeepAliveLoop().IgnoreError(); });
    return absl::OkStatus();
  }

 private:
  absl::Status RunKeepAliveLoop() {
    while (!end_keepalive_thread_.HasBeenNotified()) {
      absl::Status connection_health;
      {
        absl::MutexLock client_lock(&client_mutex_);
        connection_health = client_.KeepConnectionHealthyOrReconnect();
      }

      if (!connection_health.ok()) {
        LOG(ERROR) << "KeepAliveLoop: connection is unhealthy: "
                   << connection_health.message();
      }
      LOG_EVERY_N_SEC(INFO, 120) << "KeepAliveLoop: connection is healthy.";

      // Throttle.
      absl::SleepFor(absl::Seconds(1));
    }

    return absl::OkStatus();
  }

  OpcuaMultiKeyNodeMap<GpioOpcuaNode*> opcua_node_map_;
  ::intrinsic::opcua::OpcuaClient client_ ABSL_GUARDED_BY(client_mutex_);

  absl::Mutex client_mutex_;

  // Thread to keep the connection with the opcua server alive by processing
  // asynchronous responses, doing internal housekeeping and renewing secure
  // channel, etc.
  // For more details, refer to: https://www.open62541.org/doc/1.3/client.html
  intrinsic::Thread keepalive_thread_;

  absl::Notification end_keepalive_thread_;
};

class OpcuaGPIOService
    : public intrinsic_proto::gpio::v1::GPIOService::Service {
 public:
  ::grpc::Status GetSignalDescriptions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest* request,
      intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse* response)
      override;

  ::grpc::Status ReadSignals(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
      intrinsic_proto::gpio::v1::ReadSignalsResponse* response) override;

  ::grpc::Status WaitForValue(
      ::grpc::ServerContext* context,
      const intrinsic_proto::gpio::v1::WaitForValueRequest* request,
      intrinsic_proto::gpio::v1::WaitForValueResponse* response) override;

  ::grpc::Status OpenWriteSession(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          intrinsic_proto::gpio::v1::OpenWriteSessionResponse,
          intrinsic_proto::gpio::v1::OpenWriteSessionRequest>* stream) override;

  absl::StatusOr<std::reference_wrapper<intrinsic::Publisher>>
  GetOrCreatePublisher(const std::string& topic) {
    absl::MutexLock lock(&mutex_);
    auto it = publishers_.find(topic);
    if (it != publishers_.end()) {
      return std::ref(it->second);
    }

    INTR_ASSIGN_OR_RETURN(auto publisher,
                          pubsub_.CreatePublisher(topic, TopicConfig()),
                          absl::InternalError("Failed to create publisher"));

    std::tie(it, std::ignore) =
        publishers_.emplace(topic, std::move(publisher));
    return std::ref(it->second);
  }

  explicit OpcuaGPIOService(const absl::string_view opcua_server_address,
                            const Authentication& auth,
                            const std::vector<std::string>& node_ids,
                            const std::string& instance_name)
      : opcua_server_address_(opcua_server_address),
        auth_(auth),
        node_ids_(node_ids),
        client_(opcua_server_address, auth),
        instance_name_(instance_name) {
    // Clears out the request so that it is not left uninitialized
    UA_ReadRequest_init(&read_request_);
  }

  bool IsReadable(
      const std::pair<const std::string, GpioOpcuaNode*>& node) const {
    auto signal_description = signal_descriptions_.Find(node.first);
    return signal_description != signal_descriptions_.end() &&
           signal_description->second.can_read();
  }

  absl::Status ConfigureOpcuaNodes() {
    absl::MutexLock node_map_lock(&node_map_mutex_);
    client_mutex_.AssertHeld();
    INTR_ASSIGN_OR_RETURN(auto opcua_nodes, ParseNodeIdStrings(node_ids_));

    // Check the node types on the server.
    for (auto& node : opcua_nodes) {
      ::intrinsic::opcua::LocalizedText display_name;
      // Get the display name for internal use.
      UA_StatusCode ua_status = UA_Client_readDisplayNameAttribute(
          client_.Handle(), node->node_id, &display_name.text);
      if (ua_status != UA_STATUSCODE_GOOD) {
        return absl::InvalidArgumentError(
            absl::StrCat("Reading display name for node failed. Check if the "
                         "display names failed, with error: ",
                         UA_StatusCode_name(ua_status)));
      }

      // The returned data is contained in a pointer to a custom byte type
      // array. Convert to a standardized null terminated string.
      const auto node_name = std::string(display_name.GetString());

      // Gets human readable description of the node.
      ::intrinsic::opcua::LocalizedText description;
      ua_status = UA_Client_readDescriptionAttribute(
          client_.Handle(), node->node_id, &description.text);
      if (ua_status != UA_STATUSCODE_GOOD) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Reading description for the node ", node_name,
            " failed, with error: ", UA_StatusCode_name(ua_status)));
      }
      // The returned data is contained in a pointer to a
      // custom byte type array. Convert to a standardized null terminated
      // string.
      const auto node_description = std::string(description.GetString());

      // Verify the access level for the nodes.
      unsigned char access_level;
      ua_status = UA_Client_readAccessLevelAttribute(
          client_.Handle(), node->node_id, &access_level);
      if (ua_status != UA_STATUSCODE_GOOD) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Reading access level for node ", node_name,
            " failed, with error: ", UA_StatusCode_name(ua_status)));
      }

      const bool can_read = (access_level & UA_ACCESSLEVELMASK_READ) != 0;
      const bool can_write = (access_level & UA_ACCESSLEVELMASK_WRITE) != 0;

      // Reading attributes will fail for write-only variables.
      if (can_read) {
        ua_status = UA_Client_readValueAttribute(client_.Handle(),
                                                 node->node_id, &node->variant);
        if (ua_status != UA_STATUSCODE_GOOD) {
          return absl::InvalidArgumentError(absl::StrCat(
              "Reading node ", node_name,
              " failed, with error: ", UA_StatusCode_name(ua_status)));
        }
      }
      // Leave the variant uninitialized for write-only signals.

      // Build the signal description
      intrinsic_proto::gpio::v1::SignalDescription signal_description;

      // Default/fallback value.
      auto signal_type =
          intrinsic_proto::gpio::v1::SignalType::SIGNAL_TYPE_UNKNOWN;

      // The signal type for a write-only signal will remain unknown. The type
      // will eventually be determined when a value is written to the signal
      // and the type is inferred from the value to be written.
      if (can_read) {
        // TODO(b/352610980): `VariantTypeToSignalType` should return the
        // correct type independent of storing scalar or array values.
        // For now, create a temporary scalar variant to determine the type.
        UA_Variant scalar_variant;
        UA_Variant_init(&scalar_variant);
        scalar_variant.type = node->variant.type;
        scalar_variant.data = const_cast<void*>(node->variant.data);

        INTR_ASSIGN_OR_RETURN(
            signal_type,
            intrinsic::gpio::VariantTypeToSignalType(&scalar_variant),
            _.LogError() << "Variant type failure for node: " << node_name);
      }

      // Build the pubsub topic.
      node->pubsub_topic_name = GetTopicNameForSignalName(node_name);

      signal_description.set_can_read(can_read);
      signal_description.set_can_write(can_write);
      signal_description.set_can_force(false);
      signal_description.set_signal_name(node_name);
      signal_description.set_type(signal_type);
      signal_description.set_pubsub_topic_name(node->pubsub_topic_name);
      signal_description.set_description(node_description);
      signal_description.add_alternate_signal_names(node->xml_node_id);
      if (node->node_id.identifierType == UA_NODEIDTYPE_STRING) {
        signal_description.add_alternate_signal_names(
            intrinsic::opcua::ToString(node->node_id.identifier.string));
      }

      if (can_read && !UA_Variant_isScalar(&node->variant)) {
        const auto vec =
            ::intrinsic::gpio::MakeArraySignalValueFromVariantValue(
                node->variant);
        if (vec.ok()) {
          LOG(INFO) << absl::StrFormat("Array backed node %s has %d elements",
                                       node_name, vec->size());
        }
      }

      signal_descriptions_.Insert(node->node_id, node_name, node->xml_node_id,
                                  signal_description);

      LOG(INFO) << "Adding " << node_name << " to tracked nodes.";
      opcua_node_map_.Insert(node->node_id, node_name, node->xml_node_id,
                             node.get());
      opcua_nodes_.push_back(std::move(node));
    }

    {
      // For convenience, keep track of the readable nodes.
      for (const auto& gpio_opcua_node : opcua_node_map_) {
        if (IsReadable(gpio_opcua_node)) {
          readable_node_map_.Insert(
              gpio_opcua_node.second->node_id, gpio_opcua_node.first,
              gpio_opcua_node.second->xml_node_id, gpio_opcua_node.second);
        }
      }

      // The read_request for ReadAllSignals should be static for the lifetime
      // of the server. Generate it here to prevent unnneccessary locks on the
      // node map mutex later on.
      UA_ReadRequest_init(&read_request_);

      const int num_readable_nodes = readable_node_map_.Size();
      read_request_.nodesToRead = static_cast<UA_ReadValueId*>(
          UA_Array_new(num_readable_nodes, &UA_TYPES[UA_TYPES_READVALUEID]));
      read_request_.nodesToReadSize = num_readable_nodes;

      // Only include the readable nodes in the read request, which will
      // be used in the polling loop to periodically read and report values.
      int i = 0;
      for (const auto& gpio_opcua_node : readable_node_map_) {
        UA_ReadValueId_init(&read_request_.nodesToRead[i]);
        read_request_.nodesToRead[i].attributeId = UA_ATTRIBUTEID_VALUE;
        // Note about non-owning pointer: this makes nodeId point to the
        // node_id (which is already managed by smart pointer).
        read_request_.nodesToRead[i].nodeId = gpio_opcua_node.second->node_id;
        i++;
      }
    }

    return absl::OkStatus();
  }

  absl::Status Initialize() {
    // The OPCUA GPIO service is allowed to come up without a connection to the
    // OPCUA server. If the initial connection attempt fails, a thread is
    // started that will periodically retry the connection.
    absl::Status status = EstablishConnection();
    if (status.ok()) {
      return absl::OkStatus();
    } else {
      LOG(WARNING) << "Failed to connect to the OPCUA server, starting a "
                      "connection thread.";
      return StartConnectionThread();
    }
  }

  absl::Status StartPollingThread() {
    polling_thread_ =
        Thread([this]() { this->RunPollingLoop().IgnoreError(); });
    return absl::OkStatus();
  }

  absl::Status StartConnectionThread() {
    connection_thread_ =
        Thread([this]() { this->RunConnectionLoop().IgnoreError(); });
    return absl::OkStatus();
  }

  ~OpcuaGPIOService() override {
    if (polling_thread_.joinable()) {
      end_polling_thread_.Notify();
      polling_thread_.join();
    }

    if (connection_thread_.joinable()) {
      end_connection_thread_.Notify();
      connection_thread_.join();
    }

    if (read_request_.nodesToReadSize > 0 && read_request_.nodesToRead) {
      // This is a bit tricky. Only free up the array and not the individual
      // elements since they point to node_ids that are managed by smart
      // pointer. Hence, using `UA_ReadRequest_delete` would result in double
      // free error.
      free(read_request_.nodesToRead);
    }
  }

 private:
  std::string GetTopicNameForSignalName(absl::string_view signal_name) const {
    return absl::StrCat(kTopicNamePrefix, "/", instance_name_, "/",
                        absl::StrReplaceAll(signal_name, {{".", "_"}}));
  }

  absl::Status PublishSignals() {
    absl::MutexLock lock(&node_map_mutex_);
    for (const auto& node : opcua_node_map_) {
      if (node.second->pubsub_topic_name.empty()) {
        continue;
      }
      // For now, we only support publishing single value signals to avoid
      // flooding the system with potentially large arrays.
      if (!node.second->HoldsScalar()) {
        continue;
      }

      INTR_ASSIGN_OR_RETURN(
          intrinsic::Publisher & pub,
          GetOrCreatePublisher(node.second->pubsub_topic_name),
          absl::InternalError("Could not create publisher"));
      INTR_RETURN_IF_ERROR(pub.Publish(node.second->GetScalarValue()));
    }
    return absl::OkStatus();
  }

  absl::Status RunPollingLoop();
  absl::Status RunConnectionLoop();
  absl::Status ReadAllSignals();
  absl::Status EstablishConnection();

  // The ConnectionEstablished method allows the caller to confirm whether the
  // server connection has been established before continuing with a gRPC call
  // that requires communication with the OPCUA server.
  absl::Status ConnectionEstablished();

  struct SignalReadCallback {
    // Called on every read of signal values until `success_notification` has
    // been notified.
    //
    // If `success_notification` is not null, then it will be notified after
    // `callback_impl` returns true.
    std::function<bool(const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>&)>
        callback_impl;

    absl::Notification* success_notification;
  };

  // Cache the opcua address for lockless access. It also does not change during
  // runtime.
  const std::string opcua_server_address_;
  const Authentication auth_;
  const std::vector<std::string> node_ids_;
  ::intrinsic::opcua::OpcuaClient client_ ABSL_GUARDED_BY(client_mutex_);
  intrinsic::Thread polling_thread_;
  absl::Notification end_polling_thread_;

  // The connection thread is started if the initial connection to the OPCUA
  // server is not successful. Once started, it periodically retries the
  // connection until successful and then exits.
  intrinsic::Thread connection_thread_;
  absl::Notification end_connection_thread_;
  absl::Notification connection_initialized_;

  static constexpr absl::string_view kTopicNamePrefix = "/equipment/gpio";
  const std::string instance_name_;
  absl::Mutex client_mutex_;
  absl::Mutex signal_read_callback_mutex_ ABSL_ACQUIRED_BEFORE(node_map_mutex_);
  absl::Mutex node_map_mutex_ ABSL_ACQUIRED_BEFORE(client_mutex_);

  // Container to keep track of all the gpio opcua nodes. Other containers refer
  // to entries in this container by pointer. This does not result in dangling
  // pointers since they all share the same lifetime (of this class object).
  std::vector<std::unique_ptr<GpioOpcuaNode>> opcua_nodes_;
  absl::flat_hash_set<SignalReadCallback*> signal_read_callbacks_
      ABSL_GUARDED_BY(signal_read_callback_mutex_);

  // The opcua_node_map_ contains all nodes, both readable and non-readable,
  // while the readable_node_map_ contains only the readable nodes. The latter
  // is a convenience which avoids having to filter the node map on the fly
  // from multiple places, helping to simplify the code.
  OpcuaMultiKeyNodeMap<GpioOpcuaNode*> opcua_node_map_
      ABSL_GUARDED_BY(node_map_mutex_);
  OpcuaMultiKeyNodeMap<GpioOpcuaNode*> readable_node_map_
      ABSL_GUARDED_BY(node_map_mutex_);

  // Only accessed via const functions, no need for mutex.
  OpcuaMultiKeyNodeMap<intrinsic_proto::gpio::v1::SignalDescription>
      signal_descriptions_;
  intrinsic::PubSub pubsub_;
  absl::Mutex mutex_;
  absl::node_hash_map<std::string, intrinsic::Publisher> publishers_;
  UA_ReadRequest read_request_;
};

::grpc::Status OpcuaGPIOService::GetSignalDescriptions(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::GetSignalDescriptionsRequest* request,
    intrinsic_proto::gpio::v1::GetSignalDescriptionsResponse* response) {
  INTR_RETURN_IF_ERROR_GRPC(ConnectionEstablished()).LogError();
  for (const auto& signal_description : signal_descriptions_) {
    *response->add_signal_descriptions() = signal_description.second;
  }
  return grpc::Status::OK;
}

absl::Status OpcuaGPIOService::ReadAllSignals() {
  ::intrinsic::opcua::ReadResponse read_response;
  {
    absl::MutexLock lock(&client_mutex_);
    read_response =
        client_.Read(read_request_, OnConnectionError::kTryReconnect);
  }

  if (const auto status = read_response.response.responseHeader.serviceResult;
      status != UA_STATUSCODE_GOOD) {
    return absl::InternalError(
        absl::StrCat("Reading signals failed with opcua status code: ",
                     UA_StatusCode_name(status), "."));
  }

  if (read_response.response.resultsSize != read_request_.nodesToReadSize) {
    return absl::InternalError("Unable to read all signals from UA server.");
  }

  // Gather and return the signal values.
  int i = 0;
  absl::MutexLock node_lock(&node_map_mutex_);
  for (const auto& readable_node : readable_node_map_) {
    const UA_Variant& v = read_response.response.results[i].value;
    i++;
    if (UA_Variant_isScalar(&v)) {
      INTR_RETURN_IF_ERROR(readable_node.second->SetScalarValue(v));
    } else if (!UA_Variant_isEmpty(&v)) {
      // Array!
      INTR_RETURN_IF_ERROR(readable_node.second->SetArrayValue(v));
    }
  }
  return absl::OkStatus();
}

absl::Status OpcuaGPIOService::ConnectionEstablished() {
  const absl::Time deadline = absl::Now() + kWaitForConnectionTimeout;
  if (connection_initialized_.WaitForNotificationWithDeadline(deadline)) {
    return absl::OkStatus();
  } else {
    return absl::InternalError("Not yet connected to the OPCUA server.");
  }
}

::grpc::Status OpcuaGPIOService::ReadSignals(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::ReadSignalsRequest* request,
    intrinsic_proto::gpio::v1::ReadSignalsResponse* response) {
  INTR_RETURN_IF_ERROR_GRPC(ConnectionEstablished()).LogError();
  {
    // Check that requested signals exist and are readable. The signals are
    // invariant for the lifecycle of the server.
    for (const auto& signal_name : request->signal_names()) {
      auto [node_name, index] = GetNodeNameWithIndexFromSignalName(signal_name);
      const auto signal_description = signal_descriptions_.Find(node_name);
      if (signal_description == signal_descriptions_.end()) {
        return ToGrpcStatus(absl::NotFoundError(
            absl::StrCat("Requested signal '", signal_name, "' is unknown.")));
      }

      if (!signal_description->second.can_read()) {
        constexpr absl::string_view kFormatString =
            "Cannot read write-only signal '%s'";
        return ToGrpcStatus(absl::PermissionDeniedError(
            absl::StrFormat(kFormatString, signal_name)));
      }
    }
  }
  auto callback_impl =
      [request,
       response](const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>& node_map) {
        for (const auto& signal_name : request->signal_names()) {
          auto [node_name, index] =
              GetNodeNameWithIndexFromSignalName(signal_name);

          auto node = node_map.Find(node_name);

          // In practice, this should never happen since the signal names are
          // validated before the callback is registered.
          if (node == node_map.end()) {
            continue;
          }
          auto value = node->second->GetValueAtIndex(index);
          if (!value.ok()) {
            LOG(WARNING) << "Failed to get value for signal " << signal_name
                         << ": " << value.status();
            continue;
          }
          response->mutable_signal_values()->mutable_values()->insert(
              {signal_name, *value});
        }
        return true;
      };

  absl::Notification success_notification;

  SignalReadCallback signal_read_callback{
      .callback_impl = callback_impl,
      .success_notification = &success_notification};
  {
    absl::MutexLock l(&signal_read_callback_mutex_);
    signal_read_callbacks_.insert(&signal_read_callback);
  }

  const absl::Time deadline =
      grpc::TimeFromGprTimespec(context->raw_deadline());
  bool wait_result =
      success_notification.WaitForNotificationWithDeadline(deadline);

  {
    absl::MutexLock l(&signal_read_callback_mutex_);
    signal_read_callbacks_.erase(&signal_read_callback);
  }

  if (!wait_result) {
    return ToGrpcStatus(absl::DeadlineExceededError(""));
  }
  return grpc::Status::OK;
}

intrinsic_proto::gpio::v1::SignalValueSet CollectObservedValues(
    const intrinsic_proto::gpio::v1::SignalValueSet& condition,
    const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>& observed_value_map) {
  intrinsic_proto::gpio::v1::SignalValueSet observed_values;
  for (const auto& signal_condition : condition.values()) {
    auto [node_name, index] =
        GetNodeNameWithIndexFromSignalName(signal_condition.first);
    auto measured_value = observed_value_map.Find(node_name);
    if (measured_value == observed_value_map.end()) {
      LOG(WARNING) << "Did not find signal `" << signal_condition.first
                   << "` in the set of observed values";
      continue;
    }

    const auto value = measured_value->second->GetValueAtIndex(index);
    if (value.ok()) {
      observed_values.mutable_values()->insert(
          {signal_condition.first, *value});
    }
  }
  return observed_values;
}

::grpc::Status OpcuaGPIOService::WaitForValue(
    ::grpc::ServerContext* context,
    const intrinsic_proto::gpio::v1::WaitForValueRequest* request,
    intrinsic_proto::gpio::v1::WaitForValueResponse* response) {
  INTR_RETURN_IF_ERROR_GRPC(ConnectionEstablished()).LogError();
  absl::Notification success_notification;
  const absl::Time deadline =
      grpc::TimeFromGprTimespec(context->raw_deadline());
  std::function<bool(const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>&)>
      callback_impl;
  if (request->has_all_of()) {
    // Check that requested signals exist. it's invariant for the lifecycle of
    // the server.
    for (const auto& signal_condition : request->all_of().values()) {
      auto [node_name, index] =
          GetNodeNameWithIndexFromSignalName(signal_condition.first);
      if (!signal_descriptions_.Contains(node_name)) {
        return ToGrpcStatus(absl::NotFoundError(absl::StrCat(
            "Requested signal '", signal_condition.first, "' is unknown.")));
      }
    }

    callback_impl =
        [condition = request->all_of(), response](
            const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>& observed_value_map) {
          // Check that the values are as expected.
          for (const auto& signal_condition : condition.values()) {
            const auto [node_name, index] =
                GetNodeNameWithIndexFromSignalName(signal_condition.first);
            const auto& measured_value = observed_value_map.Find(node_name);
            if (measured_value == observed_value_map.cend()) {
              continue;
            }

            const auto value = measured_value->second->GetValueAtIndex(index);
            if (!value.ok()) {
              LOG(WARNING) << "Failed to get value for signal "
                           << signal_condition.first << ": " << value.status();
              continue;
            }

            if (!intrinsic::gpio::SignalValuesAreApproxEqual(
                    *value, signal_condition.second)) {
              return false;
            }
          }

          // Populate the response, returning the observed values and the
          // time of observation.
          *response->mutable_event_time() = intrinsic::GetCurrentTimeProto();
          *response->mutable_values() =
              CollectObservedValues(condition, observed_value_map);
          return true;
        };
  } else if (request->has_any_of()) {
    // Check that requested signals exist. it's invariant for the lifecycle of
    // the server.
    for (const auto& signal_condition : request->any_of().values()) {
      auto [node_name, index] =
          GetNodeNameWithIndexFromSignalName(signal_condition.first);
      if (!signal_descriptions_.Contains(node_name)) {
        return ToGrpcStatus(absl::NotFoundError(absl::StrCat(
            "Requested signal '", signal_condition.first, "' is unknown.")));
      }
    }

    callback_impl =
        [condition = request->any_of(), response](
            const OpcuaMultiKeyNodeMap<GpioOpcuaNode*>& observed_value_map) {
          // Check that the values are as expected.
          for (const auto& signal_condition : condition.values()) {
            const auto [node_name, index] =
                GetNodeNameWithIndexFromSignalName(signal_condition.first);
            const auto& measured_value = observed_value_map.Find(node_name);
            if (measured_value == observed_value_map.cend()) {
              continue;
            }

            const auto value = measured_value->second->GetValueAtIndex(index);
            if (!value.ok()) {
              LOG(WARNING) << "Failed to get value for signal "
                           << signal_condition.first << ": " << value.status();
              continue;
            }

            if (intrinsic::gpio::SignalValuesAreApproxEqual(
                    *value, signal_condition.second)) {
              // Populate the response, returning the observed values and the
              // time of observation.
              *response->mutable_event_time() =
                  intrinsic::GetCurrentTimeProto();
              *response->mutable_values() =
                  CollectObservedValues(condition, observed_value_map);
              return true;
            }
          }

          return false;
        };

  } else {
    return ToGrpcStatus(absl::UnimplementedError(
        "Opcua GPIO server currently only supports all_of or any_of "
        "WaitForSignalValue conditions."));
  }

  SignalReadCallback signal_read_callback{
      .callback_impl = callback_impl,
      .success_notification = &success_notification};
  {
    absl::MutexLock l(&signal_read_callback_mutex_);
    signal_read_callbacks_.insert(&signal_read_callback);
  }
  bool notified = WaitForNotificationWithDeadlineAndInterrupt(
      success_notification, deadline,
      [&context]() -> bool { return context->IsCancelled(); });

  {
    absl::MutexLock l(&signal_read_callback_mutex_);
    signal_read_callbacks_.erase(&signal_read_callback);
  }

  if (!notified) {
    if (context->IsCancelled()) {
      return ToGrpcStatus(absl::CancelledError(
          "Cancelled while waiting for condition to be met."));
    }
    return ToGrpcStatus(absl::DeadlineExceededError(
        "Deadline exceed waiting for condition to be met."));
  }
  return grpc::Status::OK;
}

absl::Status OpcuaGPIOService::RunPollingLoop() {
  while (!end_polling_thread_.HasBeenNotified()) {
    // Logs errors but does not return to not stop the polling thread due to
    // potentially intermittent errors.
    const auto read_status = ReadAllSignals();
    if (read_status.ok()) {
      {
        absl::MutexLock l(&signal_read_callback_mutex_);
        absl::MutexLock node_lock(&node_map_mutex_);
        for (const auto& signal_read_callback : signal_read_callbacks_) {
          if (signal_read_callback->success_notification &&
              signal_read_callback->success_notification->HasBeenNotified()) {
            continue;
          }
          bool result = signal_read_callback->callback_impl(opcua_node_map_);
          if (result && signal_read_callback->success_notification) {
            signal_read_callback->success_notification->Notify();
          }
        }
      }

      const auto pub_status = PublishSignals();
      if (!pub_status.ok()) {
        LOG(ERROR) << pub_status.message();
      }
    } else {
      LOG(ERROR) << read_status.message();
    }

    absl::SleepFor(kReadPollingDelay);
  }
  return absl::OkStatus();
}

absl::Status OpcuaGPIOService::EstablishConnection() {
  absl::MutexLock lock(&client_mutex_);
  absl::Status status = client_.Connect();
  if (status.ok()) {
    LOG(INFO) << "Connected, configuring the nodes.";
    INTR_RETURN_IF_ERROR(ConfigureOpcuaNodes()).LogError();
    LOG(INFO) << "Starting the polling thread.";
    INTR_RETURN_IF_ERROR(StartPollingThread()).LogError();
    connection_initialized_.Notify();
    return absl::OkStatus();
  }
  return status;
}

absl::Status OpcuaGPIOService::RunConnectionLoop() {
  while (!end_connection_thread_.HasBeenNotified()) {
    LOG(INFO) << "Retrying connection to the OPCUA server";
    absl::Status status = EstablishConnection();
    if (status.ok()) {
      return absl::OkStatus();
    }
    absl::SleepFor(kConnectionRetryPeriod);
  }

  return absl::OkStatus();
}

::grpc::Status OpcuaGPIOService::OpenWriteSession(
    ::grpc::ServerContext* context,
    ::grpc::ServerReaderWriter<
        intrinsic_proto::gpio::v1::OpenWriteSessionResponse,
        intrinsic_proto::gpio::v1::OpenWriteSessionRequest>* stream) {
  INTR_RETURN_IF_ERROR_GRPC(ConnectionEstablished()).LogError();
  intrinsic_proto::gpio::v1::OpenWriteSessionRequest request;
  if (!stream->Read(&request)) {
    return ToGrpcStatus(absl::AbortedError("Failed to read initial request."));
  }
  if (!request.has_initial_session_data()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "Initial OpenWriteSessionRequest is missing initial_session_data."));
  }

  LOG(INFO) << "OpenWriteSession for signals "
            << absl::StrJoin(request.initial_session_data().signal_names(),
                             ", ");

  absl::StatusOr<std::unique_ptr<WriteSession>> session;
  {
    absl::MutexLock map_lock(&node_map_mutex_);
    session =
        WriteSession::Create(request.initial_session_data(),
                             opcua_server_address_, auth_, opcua_node_map_);
  }

  intrinsic_proto::gpio::v1::OpenWriteSessionResponse initial_resp;
  *initial_resp.mutable_status() = SaveStatusAsRpcStatus(session.status());

  if (!stream->Write(initial_resp)) {
    return ToGrpcStatus(
        absl::AbortedError("Failed to write initial response to the client."));
  }

  if (!session.ok()) {
    LOG(ERROR) << "Initial session error: " << session.status();
    return ToGrpcStatus(session.status());
  }

  // Starts a thread to keep the connection alive.
  INTR_RETURN_IF_ERROR_GRPC(session->get()->StartKeepAliveThread()).LogError();

  while (stream->Read(&request)) {
    LOG(INFO) << "Received request " << request;
    if (request.has_initial_session_data()) {
      return ToGrpcStatus(absl::FailedPreconditionError(
          "Received initial session data when Session is already "
          "initialized."));
    }

    absl::Status status_result;
    switch (request.action_request_case()) {
      case intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          ActionRequestCase::kWriteSignals:
        status_result =
            session->get()->HandleWriteOperation(request.write_signals());
        break;
      case intrinsic_proto::gpio::v1::OpenWriteSessionRequest::
          ActionRequestCase::ACTION_REQUEST_NOT_SET:
        status_result = absl::InvalidArgumentError("No request was provided.");
        break;
      default:
        status_result = absl::UnimplementedError("Request type not handled.");
    }

    if (!status_result.ok()) {
      LOG(ERROR) << "Session error: " << status_result;
    }

    intrinsic_proto::gpio::v1::OpenWriteSessionResponse resp;
    (*resp.mutable_status()) = SaveStatusAsRpcStatus(status_result);
    if (!stream->Write(resp)) {
      return ToGrpcStatus(absl::AbortedError(
          "Failed to write streaming response to the client. Assuming the "
          "client is dead and ending session."));
    }

    // Closes the write session on aborted error, as required by
    // `OpenWriteSession` API.
    // intrinsic/hardware/gpio/v1/gpio_service.proto;l=71-72;rcl=559549436
    if (resp.status().code() == grpc::StatusCode::ABORTED) {
      LOG(INFO) << "Closing the write stream as session was aborted.";
      return grpc::Status::OK;
    }
  }
  return grpc::Status::OK;
}

}  // namespace

absl::StatusOr<std::unique_ptr<intrinsic_proto::gpio::v1::GPIOService::Service>>
MakeOpcuaGPIOService(
    const std::string& instance_name,
    const intrinsic_proto::gpio::OpcuaGpioServiceConfig& config) {
  if (instance_name.empty()) {
    return absl::InvalidArgumentError("Instance name cannot be empty.");
  }

  const auto& opcua_server_address = config.opcua_server_address();
  std::vector<std::string> node_ids;
  node_ids.reserve(config.opcua_nodes().node_id_size());
  for (const auto& node_id : config.opcua_nodes().node_id()) {
    node_ids.push_back(node_id);
  }

  LOG(INFO) << "Making the OPCUA GPIO service";
  auto service = std::make_unique<OpcuaGPIOService>(
      opcua_server_address,
      Authentication{
          .username = config.username(),
          .password = config.password(),
          .cert_file = config.cert_file(),
          .private_key_file = config.private_key_file(),
          .application_uri = config.application_uri(),
          .trusted_certificates_filepaths = std::vector<std::string>(
              config.trusted_certificate_filepaths().begin(),
              config.trusted_certificate_filepaths().end()),
      },
      node_ids, instance_name);

  LOG(INFO) << "Attempting to connect to the OPCUA server at "
            << opcua_server_address;
  INTR_RETURN_IF_ERROR(service->Initialize()).LogError();

  return std::move(service);
}
}  // namespace intrinsic
