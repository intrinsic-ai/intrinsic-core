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

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
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

namespace {

std::atomic<bool> g_shutdown_requested = false;
static_assert(std::atomic<bool>::is_always_lock_free);

}  // namespace

namespace intrinsic {

namespace {

using ::nlohmann::json;

class ZenohIntrospector {
 public:
  ZenohIntrospector() {
    introspection_callback_fptr_ = std::make_unique<imw_callback_functor_t>(
        [this](const char* keyexpr, const void* blob, const size_t blob_len) {
          this->introspection_callback(keyexpr, blob, blob_len);
        });
  }
  ZenohIntrospector(const ZenohIntrospector&) = delete;
  ZenohIntrospector& operator=(const ZenohIntrospector&) = delete;
  ZenohIntrospector(ZenohIntrospector&&) = delete;
  ZenohIntrospector& operator=(ZenohIntrospector&&) = delete;

  ~ZenohIntrospector() {
    if (zenoh_init_complete_) {
      Zenoh().imw_fini();
    }
  }

  bool init() {
    std::string config_flag = absl::GetFlag(FLAGS_zenoh_config);
    std::string config;
    if (config_flag.empty()) {
      config = intrinsic::GetZenohPeerConfig();
    } else {
      absl::Status status =
          file::GetContents(config_flag, &config, file::Defaults());
      if (!status.ok())
        LOG(FATAL) << "Couldn't read the config file: " << config_flag;
    }

    if (config.empty()) {
      LOG(FATAL) << "Empty Zenoh config";
    }
    const imw_ret_t init_ret = Zenoh().imw_init(config.c_str());
    if (init_ret != IMW_OK) {
      LOG(FATAL) << "Error from imw_init with config " << config;
    }
    zenoh_init_complete_ = true;

    const imw_ret_t sub_ret = Zenoh().imw_create_subscription(
        "in/_introspection/sessions/*", introspection_callback_s, "{}",
        introspection_callback_fptr_.get());
    if (sub_ret != IMW_OK) {
      LOG(ERROR) << "Could not create Zenoh introspection subscriber";
      return false;
    }
    return true;
  }

 private:
  bool zenoh_init_complete_ = false;
  std::unique_ptr<imw_callback_functor_t> introspection_callback_fptr_;

  template <typename ValueType>
  ValueType extract_value(json& container, absl::string_view key,
                          ValueType default_value) {
    if (container.contains(key)) {
      return container[key].get<ValueType>();
    }

    return default_value;
  }

  void introspection_callback(const char* keyexpr, const void* blob,
                              const size_t blob_len) {
    const std::string json_payload(reinterpret_cast<const char*>(blob),
                                   blob_len);
    json j = json::parse(json_payload, nullptr, false);
    if (j.is_discarded()) {
      LOG(ERROR) << "JSON parse error";
      return;
    }

    std::string j_hostname = extract_value<std::string>(j, "hostname", "");
    std::string j_zid = extract_value<std::string>(j, "zenoh_id", "");
    uint64_t num_publishers = 0;
    uint64_t num_subscribers = 0;

    if (j.contains("publishers") && !j["publishers"].is_null()) {
      json j_pubs = j["publishers"];
      num_publishers = j_pubs.size();
      for (json::iterator it = j_pubs.begin(); it != j_pubs.end(); ++it) {
        std::string j_pub_name = extract_value<std::string>(*it, "name", "");
        uint64_t j_pub_messages = extract_value<uint64_t>(*it, "messages", 0);
        uint64_t j_pub_bytes = extract_value<uint64_t>(*it, "bytes", 0);

        zenoh_introspection::RecordTopicMetrics(
            /*direction=*/"pub", j_pub_name, j_hostname,
            static_cast<int64_t>(j_pub_bytes),
            static_cast<int64_t>(j_pub_messages));
      }
    }

    // Recording the number of publishers outside of the `if` statement above to
    // make sure that we record clients with zero publishers.
    zenoh_introspection::RecordNumPublishers(
        j_hostname, static_cast<int64_t>(num_publishers));

    if (j.contains("subscriptions") && !j["subscriptions"].is_null()) {
      json j_subs = j["subscriptions"];
      num_subscribers = j_subs.size();
      for (json::iterator it = j_subs.begin(); it != j_subs.end(); ++it) {
        std::string j_sub_name = extract_value<std::string>(*it, "name", "");
        uint64_t j_sub_messages = extract_value<uint64_t>(*it, "messages", 0);
        uint64_t j_sub_bytes = extract_value<uint64_t>(*it, "bytes", 0);

        zenoh_introspection::RecordTopicMetrics(
            /*direction=*/"sub", j_sub_name, j_hostname,
            static_cast<int64_t>(j_sub_bytes),
            static_cast<int64_t>(j_sub_messages));
      }
    }

    // Recording the number of subscribers outside of the `if` statement above
    // to make sure that we record clients with zero subscribers.
    zenoh_introspection::RecordNumSubscribers(
        j_hostname, static_cast<int64_t>(num_subscribers));
  }
  static void introspection_callback_s(const char* keyexpr, const void* blob,
                                       size_t blob_len, void* fptr) {
    (*static_cast<imw_callback_functor_t*>(fptr))(keyexpr, blob, blob_len);
  }
};

absl::Status MainImpl() {
  ZenohIntrospector zenoh_introspector;
  if (!zenoh_introspector.init()) {
    return absl::FailedPreconditionError("Init failed");
  }

  std::signal(SIGINT, [](int) {
    g_shutdown_requested = true;
    g_shutdown_requested.notify_all();
  });

  std::signal(SIGTERM, [](int) {
    g_shutdown_requested = true;
    g_shutdown_requested.notify_all();
  });

  g_shutdown_requested.wait(false);

  LOG(INFO) << "zenoh introspection is cleanly shutting down";
  return absl::OkStatus();
}

}  // namespace

}  // namespace intrinsic

int main(int argc, char** argv) {
  InitIntrinsic(argv[0], argc, argv);
  intrinsic::OpenCensusPlugin open_census(
      /*service_name=*/"zenoh_introspection", /*use_otel_metrics=*/true);
  intrinsic::zenoh_introspection::RegisterZenohIntrospectionMetrics();
  QCHECK_OK(intrinsic::MainImpl());
  return 0;
}
