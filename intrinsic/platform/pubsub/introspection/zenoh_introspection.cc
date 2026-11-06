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

#include <signal.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/platform/pubsub/introspection/metrics.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_config.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"
#include "intrinsic/stats/opencensus.h"
#include "nlohmann/json.hpp"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

ABSL_FLAG(std::string, zenoh_config, "",
          "JSON config file for Zenoh. If not provided, defaults to the config "
          "provided by GetZenohPeerConfig()");

namespace intrinsic {

namespace {

using ::nlohmann::json;

constexpr char kIntrospectionKeyExpr[] = "in/_introspection/sessions/*";

template <typename ValueType>
ValueType ExtractValue(const json& container, absl::string_view key,
                       ValueType default_value) {
  if (auto it = container.find(key); it != container.end()) {
    if (const auto* val = it->get_ptr<const ValueType*>(); val != nullptr) {
      return *val;
    }
    LOG(ERROR) << "Type mismatch for key '" << key << "'";
  }
  return default_value;
}

int64_t RecordEndpointMetrics(const json& j, absl::string_view key,
                              absl::string_view direction,
                              absl::string_view hostname) {
  auto it = j.find(key);
  if (it == j.end() || !it->is_array()) {
    return 0;
  }
  for (const json& entry : *it) {
    zenoh_introspection::RecordTopicMetrics(
        direction, ExtractValue<std::string>(entry, "name", ""), hostname,
        static_cast<int64_t>(ExtractValue<uint64_t>(entry, "bytes", 0)),
        static_cast<int64_t>(ExtractValue<uint64_t>(entry, "messages", 0)));
  }
  return static_cast<int64_t>(it->size());
}

void IntrospectionCallback(const char* /*keyexpr*/, const void* blob,
                           const size_t blob_len, void* /*user_context*/) {
  if (blob == nullptr || blob_len == 0) {
    LOG(ERROR) << "Received null or empty payload";
    return;
  }
  json j =
      json::parse(absl::string_view(static_cast<const char*>(blob), blob_len),
                  nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    LOG(ERROR) << "JSON parse error or payload is not an object";
    return;
  }

  const std::string j_hostname = ExtractValue<std::string>(j, "hostname", "");
  // Always record counts so sessions with zero publishers or subscribers are
  // still reported.
  zenoh_introspection::RecordNumPublishers(
      j_hostname, RecordEndpointMetrics(j, "publishers", "pub", j_hostname));
  zenoh_introspection::RecordNumSubscribers(
      j_hostname, RecordEndpointMetrics(j, "subscriptions", "sub", j_hostname));
}

std::string LoadZenohConfig() {
  std::string config_flag = absl::GetFlag(FLAGS_zenoh_config);
  std::string config;
  if (config_flag.empty()) {
    config = GetZenohPeerConfig();
  } else {
    if (absl::Status status =
            file::GetContents(config_flag, &config, file::Defaults());
        !status.ok()) {
      LOG(FATAL) << "Couldn't read the config file " << config_flag << ": "
                 << status;
    }
  }
  if (config.empty()) {
    LOG(FATAL) << "Empty Zenoh config";
  }
  return config;
}

}  // namespace

}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);

  // InitIntrinsic initializes logging, handles --sleep, and spawns no threads.
  // Block SIGINT and SIGTERM before any background threads are created so all
  // threads inherit the signal mask and only sigwait() below handles them.
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &sigset, nullptr) != 0) {
    LOG(FATAL) << "Failed to block SIGINT and SIGTERM";
  }

  intrinsic::OpenCensusPlugin open_census(
      /*service_name=*/"zenoh_introspection", /*use_otel_metrics=*/true);
  intrinsic::zenoh_introspection::RegisterZenohIntrospectionMetrics();

  const std::string config = intrinsic::LoadZenohConfig();
  if (intrinsic::Zenoh().imw_init(config.c_str()) != intrinsic::IMW_OK) {
    LOG(FATAL) << "Error from imw_init with config " << config;
  }

  if (intrinsic::Zenoh().imw_create_subscription(
          intrinsic::kIntrospectionKeyExpr, intrinsic::IntrospectionCallback,
          "{}", nullptr) != intrinsic::IMW_OK) {
    LOG(ERROR) << "Could not create Zenoh introspection subscriber";
    intrinsic::Zenoh().imw_destroy_session_when_unused();
    intrinsic::Zenoh().imw_fini();
    return EXIT_FAILURE;
  }

  int sig = 0;
  if (const int err = sigwait(&sigset, &sig); err != 0) {
    LOG(ERROR) << "sigwait failed with error: " << err;
  }

  if (intrinsic::Zenoh().imw_destroy_subscription(
          intrinsic::kIntrospectionKeyExpr, intrinsic::IntrospectionCallback,
          nullptr) != intrinsic::IMW_OK) {
    LOG(ERROR) << "Could not destroy Zenoh introspection subscriber";
  }
  intrinsic::Zenoh().imw_destroy_session_when_unused();
  intrinsic::Zenoh().imw_fini();
  LOG(INFO) << "zenoh introspection is cleanly shutting down";
  return EXIT_SUCCESS;
}
