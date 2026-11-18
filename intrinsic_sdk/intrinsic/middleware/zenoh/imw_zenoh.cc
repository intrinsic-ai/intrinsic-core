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

#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_data_callback_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_get_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_token.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_query_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_queryable_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_reply_context.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using std::string;

namespace intrinsic {

static constexpr char kImwZenohVersion[] = "2.1.3";
static constexpr absl::Duration kDefaultLivelinessGetTimeout =
    absl::Minutes(10);

namespace {

constexpr char kRawOctetStreamEncoding[] =
    "application/octet-stream;intrinsic.raw";

// Schema-qualifies raw unframed payloads
// (`application/octet-stream;intrinsic.raw`) because Zenoh's HTTP REST plugin
// stamps untyped `PUT` payloads from `CloudPubsubConfigManager` (LEB128
// VarInt-prefixed `Any` protos) with bare `application/octet-stream`.
const z_loaned_encoding_t* RawOctetStreamEncoding() {
  static const absl::NoDestructor<z_owned_encoding_t> kOwned([]() {
    z_owned_encoding_t enc;
    const z_result_t res = z_encoding_from_str(&enc, kRawOctetStreamEncoding);
    if (res != Z_OK) {
      LOG(FATAL) << "Failed to initialize Zenoh raw encoding: "
                 << static_cast<int>(res);
    }
    return enc;
  }());
  return z_loan(*kOwned);
}

bool IsValidEncoding(imw_encoding_t encoding) {
  switch (encoding) {
    case IMW_ENCODING_VARINT_PREFIXED_BYTES:
    case IMW_ENCODING_APPLICATION_OCTET_STREAM:
    case IMW_ENCODING_APPLICATION_PROTOBUF:
      return true;
    case IMW_ENCODING_UNKNOWN:
      return false;
  }
  return false;
}

imw_ret_t AttachZenohEncoding(imw_encoding_t encoding,
                              z_owned_encoding_t* out_encoding,
                              bool* out_has_encoding) {
  switch (encoding) {
    case IMW_ENCODING_VARINT_PREFIXED_BYTES:
      *out_has_encoding = false;
      return IMW_OK;
    case IMW_ENCODING_APPLICATION_OCTET_STREAM:
      z_encoding_clone(out_encoding, RawOctetStreamEncoding());
      *out_has_encoding = true;
      return IMW_OK;
    case IMW_ENCODING_APPLICATION_PROTOBUF:
      z_encoding_clone(out_encoding, z_encoding_application_protobuf());
      *out_has_encoding = true;
      return IMW_OK;
    case IMW_ENCODING_UNKNOWN:
      LOG(ERROR) << "Cannot attach read-only IMW_ENCODING_UNKNOWN";
      return IMW_ERROR;
  }
  LOG(ERROR) << "Unknown imw_encoding_t constant: "
             << static_cast<int>(encoding);
  return IMW_ERROR;
}

imw_encoding_t FromZenohEncoding(const z_loaned_encoding_t* encoding) {
  if (encoding == nullptr) {
    return IMW_ENCODING_VARINT_PREFIXED_BYTES;
  }
  if (z_encoding_equals(encoding, RawOctetStreamEncoding())) {
    return IMW_ENCODING_APPLICATION_OCTET_STREAM;
  }
  if (z_encoding_equals(encoding, z_encoding_application_protobuf())) {
    return IMW_ENCODING_APPLICATION_PROTOBUF;
  }
  if (z_encoding_equals(encoding, z_encoding_zenoh_bytes()) ||
      z_encoding_equals(encoding, z_encoding_application_octet_stream())) {
    return IMW_ENCODING_VARINT_PREFIXED_BYTES;
  }
  return IMW_ENCODING_UNKNOWN;
}

imw_message_metadata_t ExtractMessageMetadata(const z_loaned_sample_t* sample) {
  imw_message_metadata_t metadata;
  metadata.kind = (z_sample_kind(sample) == Z_SAMPLE_KIND_DELETE)
                      ? IMW_MESSAGE_KIND_DELETE
                      : IMW_MESSAGE_KIND_PUT;
  metadata.encoding = FromZenohEncoding(z_sample_encoding(sample));
  return metadata;
}

bool SerializePayloadForEncoding(const void* bytes, const size_t bytes_len,
                                 const imw_encoding_t encoding,
                                 const bool raw_override,
                                 z_owned_bytes_t* out_payload) {
  if (bytes_len > 0 && bytes == nullptr) {
    LOG(ERROR) << "Payload pointer is null with non-zero length: " << bytes_len;
    return false;
  }
  if (raw_override || encoding != IMW_ENCODING_VARINT_PREFIXED_BYTES) {
    if (bytes_len == 0) {
      z_bytes_empty(out_payload);
      return true;
    }
    const z_result_t result = z_bytes_copy_from_buf(
        out_payload, static_cast<const uint8_t*>(bytes), bytes_len);
    if (result != Z_OK) {
      LOG(ERROR) << "z_bytes_copy_from_buf failed: "
                 << static_cast<int>(result);
      return false;
    }
    return true;
  }
  static constexpr uint8_t kEmptyByte = 0;
  const uint8_t* ptr =
      (bytes != nullptr) ? static_cast<const uint8_t*>(bytes) : &kEmptyByte;
  const z_result_t result = ze_serialize_buf(out_payload, ptr, bytes_len);
  if (result != Z_OK) {
    LOG(ERROR) << "ze_serialize_buf failed: " << static_cast<int>(result);
    return false;
  }
  return true;
}

bool DecodeMessagePayload(const z_loaned_sample_t* sample,
                          const imw_encoding_t encoding,
                          const bool raw_override, z_owned_slice_t* out_slice) {
  const z_loaned_bytes_t* payload = z_sample_payload(sample);
  const z_result_t result =
      (raw_override || encoding != IMW_ENCODING_VARINT_PREFIXED_BYTES)
          ? z_bytes_to_slice(payload, out_slice)
          : ze_deserialize_slice(payload, out_slice);
  if (result != Z_OK) {
    LOG(ERROR) << "Unable to deserialize message payload ("
               << static_cast<int>(result) << ")";
    return false;
  }
  return true;
}

bool ValidateSetArgs(const z_owned_session_t& session, const char* keyexpr,
                     imw_encoding_t encoding, z_view_keyexpr_t* out_keyexpr) {
  if (!z_internal_check(session)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::set";
    return false;
  }
  if (keyexpr == nullptr || !IsValidEncoding(encoding)) {
    LOG(ERROR) << "Invalid arguments for set";
    return false;
  }
  if (Z_OK != z_view_keyexpr_from_str(out_keyexpr, keyexpr)) {
    LOG(ERROR) << "Invalid keyexpr for set: " << keyexpr;
    return false;
  }
  return true;
}

imw_ret_t PutPayloadWithEncoding(const z_loaned_session_t* session,
                                 const z_loaned_keyexpr_t* view_keyexpr,
                                 z_owned_bytes_t* payload,
                                 imw_encoding_t encoding) {
  z_put_options_t opts;
  z_put_options_default(&opts);
  opts.reliability = Z_RELIABILITY_RELIABLE;
  opts.congestion_control = Z_CONGESTION_CONTROL_BLOCK;
  z_owned_encoding_t owned_encoding;
  bool has_encoding = false;
  if (AttachZenohEncoding(encoding, &owned_encoding, &has_encoding) != IMW_OK) {
    z_drop(z_move(*payload));
    return IMW_ERROR;
  }
  if (has_encoding) {
    opts.encoding = z_move(owned_encoding);
  }
  const int8_t result = z_put(session, view_keyexpr, z_move(*payload), &opts);
  return result == 0 ? IMW_OK : IMW_ERROR;
}

}  // namespace

IMWZenoh::IMWZenoh() {
  z_internal_session_null(&session_);
  memset(&zenoh_id_, 0, sizeof(zenoh_id_));
}

IMWZenoh::~IMWZenoh() {
  if (z_internal_check(session_)) {
    destroy_session();
  }
}

imw_ret_t IMWZenoh::create_session(const char* config) {
  if (z_internal_check(session_)) {
    return IMW_OK;
  }

  // We could create a default config here, but currently I think it's
  // better for users to supply an explicit config. Hopefully that will
  // lead to less confusion about what a "magic" default config is doing.
  if (!config) {
    LOG(ERROR) << "Zenoh config must not be NULL";
    return IMW_ERROR;
  }

  // First, extract IMW-specific config and create config without IMW
  // so that the Zenoh config parser doesn't complain about our extra keys
  std::string config_str(config);
  try {
    // the last parameter is to allow comments in the json
    json j_config = json::parse(config_str, nullptr, true, true);

    // if there is an "imw" key, let's parse it and then erase before
    // handing this JSON to Zenoh, since it will not tolerate "extra" data.
    if (j_config.contains("imw")) {
      configure_from_json(j_config["imw"]);
      j_config.erase("imw");
      config_str = j_config.dump();
    }
  } catch (json::parse_error& e) {
    LOG(ERROR) << "Unable to parse or use imw config. Is it proper JSON, "
                  "without JSON5 extensions? "
               << e.what();
  } catch (std::exception& e) {
    LOG(ERROR) << "Unknown other exception: " << e.what();
  }

  // Now, after removing the imw top-level key, we can pass the config to Zenoh
  z_owned_config_t zenoh_config;
  z_result_t result = zc_config_from_str(&zenoh_config, config_str.c_str());
  if (result < 0) {
    LOG(ERROR) << "Unable to create zenoh config (" << static_cast<int>(result)
               << ") from:\n"
               << config_str;
    return IMW_ERROR;
  }

  result = z_open(&session_, z_move(zenoh_config), nullptr);
  if (result < 0) {
    LOG(ERROR) << "Unable to open Zenoh session";
    return IMW_ERROR;
  }

  if (introspection_enable_) {
    if (!introspection_init()) return IMW_ERROR;
  }

  return IMW_OK;
}

bool IMWZenoh::introspection_init() {
  zenoh_id_ = z_info_zid(z_loan(session_));

  // Store an ASCII version of the Zenoh ID so that we can include it in
  // introspection messages without doing this string-mashing every time
  // through the introspection-transmit loop. The Zenoh ID is 16 bytes
  // long, so this ASCII hex string will always have 32 chars.
  absl::string_view id_bytes((const char*)&zenoh_id_.id[0], 16);
  zenoh_id_str_ = absl::BytesToHexString(id_bytes);

  introspection_keyexpr_ =
      std::string("in/_introspection/sessions/") + zenoh_id_str_;
  if (create_publisher(introspection_keyexpr_.c_str(), "{}") != IMW_OK) {
    LOG(ERROR) << "Could not create introspection publisher!";
    return false;
  }

#ifdef __linux__
  // Read the command line used to invoke this process from /proc
  // so that we can send it along with the introspection messages,
  // for use in creating graphs of the system and other diagnostics
  FILE* cmdline_fd = fopen("/proc/self/cmdline", "r");
  if (cmdline_fd) {
    while (!feof(cmdline_fd)) {
      string token;
      while (true) {
        const int c = fgetc(cmdline_fd);
        if (c == 0 || c == EOF) break;
        token += static_cast<char>(c);
      }
      if (token.size() > 0) cmdline_.push_back(token);
      if (!introspection_transmit_process_args_)
        break;  // always transmit the process name, but args only on request
    }
    fclose(cmdline_fd);
  }
#endif

  // Save the PID so that we don't have to do a syscall every time
  // we send an introspection message, which includes the PID since
  // there are sometimes several copies of the same process image in
  // our clusters and it's nice to have a way to tell them apart.
  pid_ = static_cast<int>(getpid());

  // Because we are often running on Kubernetes pods, even the PID
  // is not totally unique; they are often very small single-digit
  // numbers if running in minimal containers. Let's send the hostname
  // as well, to help debugging. On Linux, HOST_NAME_MAX is 64 bytes.
  // We'll get and save the hostname here to avoid a syscall over and
  // over when sending introspection messages.
  char hostname_buf[HOST_NAME_MAX + 1] = {0};
  if (0 != gethostname(hostname_buf, sizeof(hostname_buf) - 1)) {
    LOG(ERROR) << "Error getting hostname";
    return false;
  }
  // We're guaranteed that there is a null char at the end, since the
  // zero-filled buffer is longer than HOST_NAME_MAX (64 chars).
  hostname_ = std::string(hostname_buf);

  introspection_thread_ =
      std::thread(&IMWZenoh::introspection_thread_func, this);
  {
    auto native_handle = introspection_thread_.native_handle();
    if (int errnum = pthread_setname_np(native_handle, "introspection");
        errnum != 0) {
      LOG(ERROR) << "Failed to set introspection thread name: "
                 << std::strerror(errnum);
    }
  }

  return true;
}

void IMWZenoh::configure_from_json(const json& j) {
  if (j.contains("introspection")) {
    const json& introspection_config = j["introspection"];
    if (introspection_config.contains("enable")) {
      introspection_enable_ = introspection_config["enable"].get<bool>();
    }
    if (introspection_config.contains("transmit_process_args")) {
      introspection_transmit_process_args_ =
          introspection_config["transmit_process_args"].get<bool>();
    }
    if (introspection_config.contains("transmit_interval")) {
      if (!absl::ParseDuration(
              introspection_config["transmit_interval"].get<string>(),
              &introspection_publish_interval_)) {
        LOG(WARNING) << "Unable to parse introspection interval. Using 1s";
      }
    }
  }
}

imw_ret_t IMWZenoh::destroy_session() {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::destroy_session";
    return IMW_ERROR;
  }

  // Let's be nice and close all our pending publishers and subscriptions
  {
    if (introspection_enable_) {
      // First, let's shut down introspection. This will mark the introspection
      // publisher for destruction, which we'll handle in the next block.
      // We must do this before locking the publishers mutex, so that the
      // introspection thread can finish its current work (if any) to exit.
      introspection_thread_exit_requested_.Notify();
      if (introspection_thread_.joinable()) {
        introspection_thread_.join();
      }
    }

    absl::MutexLock lock(&publishers_mutex_);

    // In case we created publishers immediately before shutting down, we
    // may have some new publishers sitting in the new_publishers list.
    // Let's transfer them to the "normal" publishers list first before
    // wiping them all out.
    {
      absl::MutexLock lock(&new_publishers_mutex_);
      for (auto it = new_publishers_.begin(); it != new_publishers_.end();
           ++it) {
        publishers_.push_back(*it);
      }
      new_publishers_.clear();
    }

    for (auto it = publishers_.begin(); it != publishers_.end(); ++it) {
      (*it)->marked_for_deletion_.store(true);
    }
    destroy_publishers_marked_for_deletion();
  }

  {
    absl::MutexLock lock(&subscriptions_mutex_);
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
      (*it)->clear_callbacks();
    }
  }
  destroy_empty_subscriptions();

  {
    std::list<std::shared_ptr<IMWZenohLivelinessSubscription>>
        subs_to_undeclare;
    {
      absl::MutexLock lock(&liveliness_subscriptions_mutex_);
      subs_to_undeclare.swap(liveliness_subscriptions_);
    }
    for (auto& sub : subs_to_undeclare) {
      sub->disable();
      z_undeclare_subscriber(z_move(sub->get_zenoh_sub()));
    }
  }

  {
    std::vector<std::unique_ptr<IMWZenohQueryable>> queryables_to_undeclare;
    {
      absl::MutexLock lock(&queryables_mutex_);
      for (auto it = queryables_.begin(); it != queryables_.end(); ++it) {
        queryables_to_undeclare.push_back(std::move(*it));
      }
      queryables_.clear();
    }
    for (auto& q : queryables_to_undeclare) {
      z_undeclare_queryable(z_move(q->get_zenoh_queryable()));
    }
  }

  {
    absl::MutexLock lock(&liveliness_tokens_mutex_);
    for (auto& [keyexpr, token] : liveliness_tokens_) {
      z_liveliness_token_drop(z_move(token->get_token()));
    }
    liveliness_tokens_.clear();
  }

  z_drop(z_move(session_));
  return IMW_OK;
}

imw_ret_t IMWZenoh::create_publisher(const char* keyexpr, const char* qos) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::create_publisher";
    return IMW_ERROR;
  }

  z_owned_keyexpr_t zenoh_keyexpr;
  z_result_t result = z_keyexpr_from_str(&zenoh_keyexpr, keyexpr);
  if (result < 0) {
    LOG(ERROR) << "Unable to create publisher keyexpr ("
               << static_cast<int>(result) << ") from: " << keyexpr;
    return IMW_ERROR;
  }

  z_owned_publisher_t pub;
  result = z_declare_publisher(z_loan(session_), &pub, z_loan(zenoh_keyexpr),
                               nullptr);
  if (result < 0) {
    LOG(ERROR) << "z_declare_publisher failed: (" << static_cast<int>(result)
               << "): " << keyexpr;
    return IMW_ERROR;
  }
  absl::MutexLock lock(&new_publishers_mutex_);
  new_publishers_.emplace_back(
      std::make_shared<IMWZenohPublisher>(keyexpr, pub, zenoh_keyexpr));
  return IMW_OK;
}

imw_ret_t IMWZenoh::destroy_publisher(const char* keyexpr) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::destroy_publisher";
    return IMW_ERROR;
  }

  // to avoid a race with IMWZenoh::publish, instead of directly deleting
  // the publisher here, instead we will just mark it for deletion, and then
  // it will actually be deleted in the in IMWZenoh::publish()
  const string keyexpr_s(keyexpr);

  {
    absl::MutexLock lock(&publishers_mutex_);
    for (auto it = publishers_.begin(); it != publishers_.end(); ++it) {
      if ((*it)->get_keyexpr() != keyexpr_s) continue;
      if ((*it)->marked_for_deletion_.exchange(true)) continue;
      return IMW_OK;
    }

    absl::MutexLock new_lock(&new_publishers_mutex_);
    for (auto it = new_publishers_.begin(); it != new_publishers_.end(); ++it) {
      if ((*it)->get_keyexpr() != keyexpr_s) continue;
      if ((*it)->marked_for_deletion_.exchange(true)) continue;
      return IMW_OK;
    }
  }

  // If we get here, we didn't find a matching publisher. Print a sad message.
  LOG(ERROR) << "No publisher exists for keyexpr " << keyexpr;
  return IMW_ERROR;
}

std::shared_ptr<IMWZenohPublisher>
IMWZenoh::resolve_pending_publishers_and_get_matching(
    const std::string& keyexpr) {
  // first add any new publishers, if any exist
  {
    absl::MutexLock lock(&new_publishers_mutex_);
    for (auto it = new_publishers_.begin(); it != new_publishers_.end(); ++it) {
      publishers_.push_back(*it);
    }
    new_publishers_.clear();
  }

  destroy_publishers_marked_for_deletion();

  // now see if we can find the publisher we actually wanted
  for (auto it = publishers_.begin(); it != publishers_.end(); ++it) {
    if ((*it)->get_keyexpr() != keyexpr) continue;
    if ((*it)->marked_for_deletion_.load()) continue;

    // If we get here, we're on the iterator for the requested publisher
    return *it;
  }

  return nullptr;
}

// If a Zenoh session subscribes and publishes the same topic, Zenoh
// will invoke the callbacks in a stack of publish-callback-publish-callback
// and so on. This makes it tricky to avoid deadlocking while also being
// safe for various orderings of create/destroy publishers and publish()
// calls overlapping from different threads. The strategy implemented here
// is to delay processing the destroy() calls until this function, when
// we can be sure they are not overlapping with calls to publish() from
// other threads, and if we just happen to be trying to publish() to
// a publisher that was just removed in a different thread's publish()
// call stack, it is OK because we are saving a shared_ptr to the publisher
// so that its refcount will not go to zero until we exit this function.
// Along similar lines, we only add new publishers in this function, to
// avoid having to lock the publishers mutex in the create_publisher()
// function, which could also find a way to cause a deadlock in the call
// stack.
imw_ret_t IMWZenoh::publish(const char* keyexpr, const void* bytes,
                            const size_t bytes_len) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::publish()";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);
  std::shared_ptr<IMWZenohPublisher> publisher;
  {
    absl::MutexLock lock(&publishers_mutex_);
    publisher = resolve_pending_publishers_and_get_matching(keyexpr_s);
    if (publisher) {
      publisher->record_message_size(bytes_len);
    }
  }
  if (!publisher) {
    LOG(ERROR) << "No publisher exists for keyexpr " << keyexpr;
    return IMW_ERROR;
  }

  z_publisher_put_options_t options;
  z_publisher_put_options_default(&options);

  z_owned_bytes_t payload;
  ze_serialize_buf(&payload, static_cast<const uint8_t*>(bytes), bytes_len);
  z_publisher_put(z_loan(publisher->get_zenoh_pub()), z_move(payload),
                  &options);
  return IMW_OK;
}

imw_ret_t IMWZenoh::publisher_has_matching_subscribers(const char* keyexpr,
                                                       bool* has_matching) {
  if (!z_internal_check(session_)) {
    LOG(ERROR)
        << "Invalid session in IMWZenoh::publisher_has_matching_subscribers()";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);
  std::shared_ptr<IMWZenohPublisher> publisher;
  {
    absl::MutexLock lock(&publishers_mutex_);
    publisher = resolve_pending_publishers_and_get_matching(keyexpr);
  }
  if (!publisher) {
    LOG(ERROR) << "No publisher exists for keyexpr " << keyexpr;
    return IMW_ERROR;
  }

  z_matching_status_t matching_status;
  z_result_t result = z_publisher_get_matching_status(
      z_loan(publisher->get_zenoh_pub()), &matching_status);
  if (result < 0) {
    LOG(ERROR) << "Unable to get matching status for publisher with keyexpr "
               << keyexpr_s;
    return IMW_ERROR;
  }
  *has_matching = matching_status.matching;

  return IMW_OK;
}

imw_ret_t IMWZenoh::create_subscription(const char* keyexpr,
                                        imw_subscription_callback_fn* callback,
                                        const char* qos, void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::create_subscription";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);

  absl::MutexLock lock(&subscriptions_mutex_);

  // First iterate through our existing subscribers to see if this is a
  // repeat subscription that we should add to an existing subscription
  for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
    if ((*it)->get_keyexpr() == keyexpr_s) {
      (*it)->add_callback(callback, user_context);
      return IMW_OK;
    }
  }

  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "unable to create key expression from: " << keyexpr_s;
    return IMW_ERROR;
  }

  auto sub =
      std::make_shared<IMWZenohSubscription>(keyexpr_s, callback, user_context);

  z_owned_closure_sample_t closure;
  z_closure_sample(&closure, IMWZenoh::static_data_callback,
                   IMWZenoh::static_closure_drop,
                   new IMWZenohDataCallbackContext(sub));
  z_subscriber_options_t sub_opts;
  z_subscriber_options_default(&sub_opts);

  z_owned_subscriber_t zenoh_sub;
  z_result_t result =
      z_declare_subscriber(z_loan(session_), &zenoh_sub, z_loan(view_keyexpr),
                           z_move(closure), &sub_opts);

  if (result < 0) {
    LOG(ERROR) << "z_declare_subscriber failed for " << keyexpr_s;
    return IMW_ERROR;
  }

  sub->set_zenoh_sub(zenoh_sub);
  subscriptions_.push_back(std::move(sub));

  return IMW_OK;
}

imw_ret_t IMWZenoh::create_liveliness_subscription(
    const char* keyexpr, imw_liveliness_callback_fn* callback,
    bool notify_about_existing_tokens, void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::create_liveliness_subscription";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);

  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "unable to create key expression from: " << keyexpr_s;
    return IMW_ERROR;
  }

  // Construct the subscription object before calling
  // z_liveliness_declare_subscriber() and without holding
  // liveliness_subscriptions_mutex_, because in Zenoh 1.10+ declaring a
  // liveliness subscriber with history=true synchronously invokes the callback
  // for existing local tokens before z_liveliness_declare_subscriber() returns.
  auto sub = std::make_shared<IMWZenohLivelinessSubscription>(
      keyexpr_s, callback, user_context);

  z_owned_closure_sample_t closure;
  z_closure_sample(&closure, IMWZenoh::static_liveliness_callback,
                   IMWZenoh::static_liveliness_closure_drop,
                   new LivelinessCallbackContext{sub});
  z_liveliness_subscriber_options_t sub_opts;
  z_liveliness_subscriber_options_default(&sub_opts);
  sub_opts.history = notify_about_existing_tokens;

  z_owned_subscriber_t zenoh_sub;
  z_result_t result = z_liveliness_declare_subscriber(
      z_loan(session_), &zenoh_sub, z_loan(view_keyexpr), z_move(closure),
      &sub_opts);

  if (result < 0) {
    LOG(ERROR) << "z_liveliness_declare_subscriber failed for " << keyexpr_s;
    return IMW_ERROR;
  }

  sub->set_zenoh_sub(zenoh_sub);
  {
    absl::MutexLock lock(&liveliness_subscriptions_mutex_);
    liveliness_subscriptions_.push_back(std::move(sub));
  }

  return IMW_OK;
}

imw_ret_t IMWZenoh::destroy_subscription(const char* keyexpr,
                                         imw_subscription_callback_fn* callback,
                                         const void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::destroy_subscription";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);
  std::shared_ptr<IMWZenohSubscription> sub_to_undeclare;
  bool found = false;
  {
    absl::MutexLock lock(&subscriptions_mutex_);
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
      if ((*it)->get_keyexpr() != keyexpr_s) continue;

      found = true;
      // We've found the subscription for the requested keyexpr, so we need to
      // remove this callback, and potentially also undeclare the Zenoh
      // subscriber if there are no additional callbacks left
      if (!(*it)->remove_callback(callback, user_context)) {
        LOG(ERROR) << "Could not remove callback for " << keyexpr_s;
        return IMW_ERROR;
      }
      if ((*it)->is_empty()) {
        sub_to_undeclare = *it;
        subscriptions_.erase(it);
      }
      break;
    }
  }

  if (sub_to_undeclare != nullptr) {
    z_undeclare_subscriber(z_move(sub_to_undeclare->get_zenoh_sub()));
    return IMW_OK;
  }
  if (found) {
    return IMW_OK;
  }

  // If we get here, that means we didn't find a matching subscriber. Bad.
  LOG(ERROR) << "Could not find a subscription for " << keyexpr_s;
  return IMW_ERROR;
}

imw_ret_t IMWZenoh::destroy_liveliness_subscription(
    const char* keyexpr, imw_liveliness_callback_fn* callback,
    const void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR)
        << "Invalid session in IMWZenoh::destroy_liveliness_subscription";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);
  std::shared_ptr<IMWZenohLivelinessSubscription> sub_to_undeclare;
  {
    absl::MutexLock lock(&liveliness_subscriptions_mutex_);
    for (auto it = liveliness_subscriptions_.begin();
         it != liveliness_subscriptions_.end(); ++it) {
      if ((*it)->get_keyexpr() == keyexpr_s &&
          (*it)->get_callback() == callback &&
          (*it)->get_user_context() == user_context) {
        sub_to_undeclare = std::move(*it);
        liveliness_subscriptions_.erase(it);
        break;
      }
    }
  }

  if (sub_to_undeclare != nullptr) {
    sub_to_undeclare->disable();
    z_undeclare_subscriber(z_move(sub_to_undeclare->get_zenoh_sub()));
    return IMW_OK;
  }

  // If we get here, that means we didn't find a matching subscriber. Bad.
  LOG(ERROR) << "Could not find a liveliness subscription for " << keyexpr_s;
  return IMW_ERROR;
}

void IMWZenoh::data_callback(IMWZenohSubscription& sub,
                             const z_loaned_sample_t* sample) {
  // Zenoh allows wildcards in subscription key expressions (`in/**`), so the
  // incoming sample's key expression (`in/FOO_42/BAR/BAZ`) can differ from the
  // registered subscription key expression. Pass the incoming sample's key
  // expression to the user callbacks on `sub`.
  z_view_string_t sample_keyexpr;
  z_keyexpr_as_view_string(z_sample_keyexpr(sample), &sample_keyexpr);
  const string sample_keyexpr_str(
      std::string_view(z_string_data(z_loan(sample_keyexpr)),
                       z_string_len(z_loan(sample_keyexpr))));

  const imw_message_metadata_t metadata = ExtractMessageMetadata(sample);
  if (metadata.kind == IMW_MESSAGE_KIND_DELETE) {
    sub.invoke_callbacks(sample_keyexpr_str.c_str(), nullptr, 0, metadata);
    return;
  }
  z_owned_slice_t slice;
  if (!DecodeMessagePayload(sample, metadata.encoding, /*raw_override=*/false,
                            &slice)) {
    LOG(ERROR) << "Unable to deserialize slice for sample_keyexpr "
               << sample_keyexpr_str;
    return;
  }
  sub.invoke_callbacks(sample_keyexpr_str.c_str(), z_slice_data(z_loan(slice)),
                       z_slice_len(z_loan(slice)), metadata);
  z_drop(z_move(slice));
}

void IMWZenoh::liveliness_get_callback(z_loaned_reply_t* reply,
                                       imw_liveliness_get_callback_fn* callback,
                                       void* untyped_context) {
  if (!z_reply_is_ok(reply)) {
    LOG(ERROR) << "IMWZenoh::liveliness_get_callback() received an error";
    return;
  }

  const z_loaned_sample_t* sample = z_reply_ok(reply);
  z_view_string_t reply_keyexpr;
  z_keyexpr_as_view_string(z_sample_keyexpr(sample), &reply_keyexpr);
  const std::string reply_keyexpr_str(
      std::string_view(z_string_data(z_loan(reply_keyexpr)),
                       z_string_len(z_loan(reply_keyexpr))));
  callback(reply_keyexpr_str.c_str(), untyped_context);
}

void IMWZenoh::destroy_publishers_marked_for_deletion() {
  // This function assumes that publishers_mutex_ is already locked!
  // remove any publishers marked for deletion
  for (auto it = publishers_.begin(); it != publishers_.end();) {
    if (!(*it)->marked_for_deletion_.load()) {
      ++it;
    } else if (it->use_count() > 1) {
      // Skip deletion if shared_ptr is held by others (e.g. in publish)
      ++it;
    } else {
      z_undeclare_publisher(z_move((*it)->get_zenoh_pub()));
      it = publishers_.erase(it);
    }
  }
}

void IMWZenoh::introspection_thread_func() {
  // todo(anyone): create a way to provide a human-readable name to pass
  // through the config JSON when creating this Zenoh session. A challenge is
  // that the IMWZenoh instance is shared between all usages in the same
  // process, so it probably makes sense to extract the process image name
  // and PID to use as the key name, rather than anything specific to the
  // instance of the IMWZenoh handler, so there isn't a race for who opens
  // it first and sets the name.

  // Maybe there is a better API to use for pacing a thread we can cancel, but
  // if we use a mutex like this, we can use AwaitWithDeadline() which seems
  // pretty great. This is from ToTW #111
  absl::Time next_publish_time(absl::Now() + introspection_publish_interval_);
  while (!introspection_thread_exit_requested_.WaitForNotificationWithDeadline(
      next_publish_time)) {
    introspection_collect_and_publish();
    next_publish_time += introspection_publish_interval_;

    // if we are behind schedule, skip forward as needed
    while (next_publish_time < absl::Now()) {
      next_publish_time += introspection_publish_interval_;
    }
  }

  destroy_publisher(introspection_keyexpr_.c_str());
}

// A single introspection cycle: collect and publish statistics
//
// This function is called periodically from introspection_thread_func()
void IMWZenoh::introspection_collect_and_publish() {
  timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts)) {
    LOG(ERROR) << "clock_gettime failed!";
    return;
  }

  json json_time = {
      {"sec", ts.tv_sec},
      {"nsec", ts.tv_nsec},
  };
  json json_publishers;
  {
    // enter a scope for the publishers_mutex_ lock
    absl::MutexLock lock(&publishers_mutex_);
    for (auto it = publishers_.begin(); it != publishers_.end(); ++it) {
      // don't publish introspection statistics about introspection topics
      if ((*it)->get_keyexpr().rfind("in/_introspection/", 0) == 0) continue;

      json publisher_statistics = {
          {"name", (*it)->get_keyexpr()},
          {"messages", (*it)->get_n_messages()},
          {"bytes", (*it)->get_n_bytes()},
      };
      json_publishers.push_back(publisher_statistics);
    }
  }
  json json_subscriptions;
  std::vector<std::shared_ptr<IMWZenohSubscription>> subscriptions_copy;
  {
    absl::MutexLock lock(&subscriptions_mutex_);
    subscriptions_copy.assign(subscriptions_.begin(), subscriptions_.end());
  }
  for (const auto& sub : subscriptions_copy) {
    // don't publish introspection statistics about introspection topics
    if (sub->get_keyexpr().rfind("in/_introspection/", 0) == 0) continue;

    IMWZenohSubscription::Statistics s = sub->get_statistics();
    json json_subscription = {
        {"name", sub->get_keyexpr()},
        {"messages", s.n_messages},
        {"bytes", s.n_bytes},
    };
    json_subscriptions.push_back(json_subscription);
  }
  json json_introspection = {
      {"publishers", json_publishers},
      {"subscriptions", json_subscriptions},
      {"time", json_time},
      {"zenoh_id", zenoh_id_str_},
      {"command", cmdline_},
      {"pid", pid_},
      {"hostname", hostname_},
  };
  string s = json_introspection.dump(4);
  // We publish a null char in case anybody tries to parse the JSON
  // without appending their own null to avoid :boom:
  publish(introspection_keyexpr_.c_str(), s.c_str(), s.size() + 1);
}

int IMWZenoh::keyexpr_includes(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return -128;

  z_owned_keyexpr_t left_keyexpr;
  z_result_t result = z_keyexpr_from_str(&left_keyexpr, left);
  if (result < 0) return result;

  z_owned_keyexpr_t right_keyexpr;
  result = z_keyexpr_from_str(&right_keyexpr, right);
  if (result < 0) {
    z_keyexpr_drop(z_move(left_keyexpr));
    return result;
  }

  const bool includes =
      z_keyexpr_includes(z_loan(left_keyexpr), z_loan(right_keyexpr));

  z_keyexpr_drop(z_move(left_keyexpr));
  z_keyexpr_drop(z_move(right_keyexpr));

  return (includes ? 0 : 1);
}

int IMWZenoh::keyexpr_intersects(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return -128;

  z_owned_keyexpr_t left_keyexpr;
  z_owned_keyexpr_t right_keyexpr;

  z_result_t result = z_keyexpr_from_str(&left_keyexpr, left);
  if (result < 0) return result;

  result = z_keyexpr_from_str(&right_keyexpr, right);
  if (result < 0) {
    z_keyexpr_drop(z_move(left_keyexpr));
    return result;
  }

  const bool intersects =
      z_keyexpr_intersects(z_loan(left_keyexpr), z_loan(right_keyexpr));

  z_keyexpr_drop(z_move(left_keyexpr));
  z_keyexpr_drop(z_move(right_keyexpr));

  return (intersects ? 0 : 1);
}

int IMWZenoh::keyexpr_is_canon(const char* keyexpr) {
  if (!keyexpr) return -128;

  const int result =
      static_cast<int>(z_keyexpr_is_canon(keyexpr, strlen(keyexpr)));
  return result;
}

void IMWZenoh::query_callback(const char* keyexpr,
                              imw_query_callback_fn* user_callback,
                              z_loaned_reply_t* reply, void* user_context,
                              const imw_query_options_t* options) {
  if (!z_reply_is_ok(reply)) {
    LOG(ERROR) << "IMWZenoh::query_callback() received an error in reply from: "
               << keyexpr;
    return;
  }
  const z_loaned_sample_t* sample = z_reply_ok(reply);
  z_view_string_t reply_keyexpr;
  z_keyexpr_as_view_string(z_sample_keyexpr(sample), &reply_keyexpr);
  const std::string reply_keyexpr_str(
      std::string_view(z_string_data(z_loan(reply_keyexpr)),
                       z_string_len(z_loan(reply_keyexpr))));
  z_owned_slice_t slice;
  z_result_t result = Z_OK;
  if (options->call_ros_service) {
    result = z_bytes_to_slice(z_sample_payload(sample), &slice);
  } else {
    result = ze_deserialize_slice(z_sample_payload(sample), &slice);
  }
  if (Z_OK != result) {
    LOG(ERROR) << "Unable to deserialize reply slice for reply keyexpr "
               << reply_keyexpr_str;
  } else {
    user_callback(reply_keyexpr_str.c_str(), z_slice_data(z_loan(slice)),
                  z_slice_len(z_loan(slice)), user_context);
  }
  z_drop(z_move(slice));
}

imw_ret_t IMWZenoh::query(const char* keyexpr, imw_query_callback_fn* callback,
                          imw_query_on_done_callback_fn* on_done,
                          const void* query_payload,
                          const size_t query_payload_len, void* user_context,
                          imw_query_options_t* options) {
  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "Invalid keyexpr for query: " << keyexpr;
    return IMW_ERROR;
  }

  z_owned_closure_reply_t reply_closure;
  z_closure_reply(&reply_closure, static_query_callback, static_query_drop,
                  new IMWZenohQueryContext(this, keyexpr, callback, on_done,
                                           user_context, options));
  z_get_options_t opts;
  z_get_options_default(&opts);

  // NOTE: This payload structure _must_ be at the same scoping level as the
  // z_get_options_t that it is "moved" into, because z_move() doesn't really
  // move in the same way that std::move() does. It is more of an indication
  // that ownership will be taken by a function consuming it. So, an
  // expression like opts.payload = z_move(payload) doesn't actually move the
  // payload into the opts structure. The payload must stay in-scope until
  // z_get() is called.
  z_owned_bytes_t payload;

  if (query_payload && query_payload_len) {
    if ((options != nullptr) && options->call_ros_service) {
      z_bytes_copy_from_buf(&payload,
                            static_cast<const uint8_t*>(query_payload),
                            query_payload_len);
    } else {
      ze_serialize_buf(&payload, static_cast<const uint8_t*>(query_payload),
                       query_payload_len);
    }
    opts.payload = z_move(payload);
  }

  opts.timeout_ms = 0;
  opts.target = Z_QUERY_TARGET_ALL;
  opts.consolidation = z_query_consolidation_none();

  // NOTE: The attachment structure _must_ be at the same scoping level as the
  // z_get_options_t instance `opts` that it is "moved" into, because z_move()
  // doesn't really move in the same way that std::move() does. It is more of
  // an indication that ownership will be taken by a function consuming it. So,
  // an expression like "opts.attachment = z_move(attachment)" doesn't actually
  // move the attachment into the opts structure. The payload must stay
  // in-scope until z_get() is called. This attachment is only used when
  // calling ROS services, but we must always leave it at this scoping level
  // for when the options->call_ros_service branch is taken.
  z_owned_bytes_t attachment;

  if (options != nullptr) {
    opts.timeout_ms = options->timeout_ms;
    if (options->call_ros_service) {
      // When calling a ROS service that is running rmw_zenoh, the incoming
      // requests are expected to have an attachment that provides various
      // metadata. These are used by ROS rmw_zenoh services to disambiguate
      // between multiple pending clients when the service is very busy, and
      // to collect statistics.
      //
      // At time of writing, the authoritative source for the structure of
      // the attachment is the source code of rmw_zenoh. Specifically,
      // rmw_zenoh_cpp/detail/attachment_helpers.cpp which can be viewed at
      // https://github.com/ros2/rmw_zenoh/blob/rolling/rmw_zenoh_cpp/src/detail/attachment_helpers.cpp
      // The serialization in this function must match the layout provided by
      // that class, as shown in AttachmentData::serialize_to_zbytes()

      ze_owned_serializer_t serializer;
      ze_serializer_empty(&serializer);

      // Although not strictly necessary for current usage, we should
      // eventually create a sequence_number counter which increments every
      // time a call is made to a specific key.
      ze_serializer_serialize_str(z_loan_mut(serializer), "sequence_number");
      ze_serializer_serialize_int64(z_loan_mut(serializer), 0);  // placeholder

      // Althought not strictly necessary for current usage, we should
      // eventually poll the system time here, and provide it in the correct
      // format.
      ze_serializer_serialize_str(z_loan_mut(serializer), "source_timestamp");
      ze_serializer_serialize_int64(z_loan_mut(serializer), 0);  // placeholder

      ze_serializer_serialize_str(z_loan_mut(serializer), "source_gid");
      ze_serializer_serialize_buf(z_loan_mut(serializer), &zenoh_id_.id[0],
                                  sizeof(zenoh_id_));

      ze_serializer_finish(z_move(serializer), &attachment);
      opts.attachment = z_move(attachment);
    }
  }

  const z_result_t result = z_get(z_loan(session_), z_loan(view_keyexpr), "",
                                  z_move(reply_closure), &opts);
  if (result < 0) {
    LOG(ERROR) << "z_get() returned an error";
    return IMW_ERROR;
  }
  return IMW_OK;
}

imw_ret_t IMWZenoh::set(const char* keyexpr, const void* bytes,
                        const size_t bytes_len, imw_encoding_t encoding) {
  z_view_keyexpr_t view_keyexpr;
  if (!ValidateSetArgs(session_, keyexpr, encoding, &view_keyexpr)) {
    return IMW_ERROR;
  }

  z_owned_bytes_t payload;
  if (!SerializePayloadForEncoding(bytes, bytes_len, encoding,
                                   /*raw_override=*/false, &payload)) {
    return IMW_ERROR;
  }

  return PutPayloadWithEncoding(z_loan(session_), z_loan(view_keyexpr),
                                &payload, encoding);
}

imw_ret_t IMWZenoh::delete_keyexpr(const char* keyexpr) {
  if (keyexpr == nullptr) {
    return IMW_ERROR;
  }

  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "Invalid keyexpr for set: " << keyexpr;
    return IMW_ERROR;
  }

  z_result_t result = z_delete(z_loan(session_), z_loan(view_keyexpr), nullptr);
  return result == 0 ? IMW_OK : IMW_ERROR;
}

imw_ret_t IMWZenoh::create_queryable(const char* keyexpr,
                                     imw_queryable_callback_fn* callback,
                                     void* user_context,
                                     imw_queryable_options_t* options) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::create_queryable()";
    return IMW_ERROR;
  }

  const string keyexpr_s(keyexpr);

  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "Invalid keyexpr for set: " << keyexpr;
    return IMW_ERROR;
  }

  z_owned_closure_query_t closure;
  z_closure_query(&closure, static_queryable_callback, static_queryable_drop,
                  new IMWZenohQueryableContext(this, keyexpr_s));

  z_owned_queryable_t queryable;
  const z_result_t result =
      z_declare_queryable(z_loan(session_), &queryable, z_loan(view_keyexpr),
                          z_move(closure), nullptr);
  if (result < 0) {
    LOG(ERROR) << "z_declare_queryable failed for " << keyexpr_s;
    return IMW_ERROR;
  }

  absl::MutexLock lock(&queryables_mutex_);
  queryables_.emplace_back(std::make_unique<IMWZenohQueryable>(
      keyexpr_s, callback, queryable, user_context, options));

  return IMW_OK;
}

void IMWZenoh::queryable_callback(const std::string& queryable_keyexpr,
                                  const z_loaned_query_t* query) {
  // This is a bit confusing, but since Zenoh allows wildcards in queryable
  // key expressions (keyexpr), it is possible that the keyexpr used to
  // create this queryable is different from the keyexpr of the query itself.
  z_view_string_t query_keyexpr_view_string;
  z_keyexpr_as_view_string(z_query_keyexpr(query), &query_keyexpr_view_string);

  const std::string query_keyexpr_str(
      std::string_view(z_string_data(z_loan(query_keyexpr_view_string)),
                       z_string_len(z_loan(query_keyexpr_view_string))));

  absl::MutexLock lock(&queryables_mutex_);

  for (auto it = queryables_.begin(); it != queryables_.end(); ++it) {
    if ((*it)->get_keyexpr() == queryable_keyexpr) {
      z_owned_slice_t slice;
      if (z_query_payload(query) != nullptr) {
        // Due to unfortunate historical reasons, payloads were originally
        // copied into and out of Zenoh using ze_serialize_buf() and
        // ze_deserialize_slice().  This works fine, but adds a VarInt field
        // (1-8 bytes, depending on the value) before the byte block begins.
        // The only penalty is the extra few bytes of that VarInt, so it was
        // left as-is to avoid potential incompatibility and regressions with
        // already-deployed services. However, when interoperating with
        // rmw_zenoh ROS services and clients, the rmw_zenoh side does not
        // insert (or remove) the VarInt at the beginning of the payload. As a
        // result, for deserialization to start on the correct byte, we must
        // not write the VarInt. This is, of course, what we meant to do
        // originally for intra-Flowstate transport, and will ideally switch to
        // at some point.  But for the time being, we stick with the
        // VarInt-prefixed serialization for intra-Flowstate, and use no prefix
        // for ROS rmw_zenoh interop, via the functions z_bytes_copy_from_buf()
        // and z_bytes_to_slice().
        if ((*it)->get_options().is_ros_service) {
          z_bytes_to_slice(z_query_payload(query), &slice);
        } else {
          ze_deserialize_slice(z_query_payload(query), &slice);
        }
      } else {
        z_slice_empty(&slice);
      }
      (*it)->invoke(query_keyexpr_str.c_str(), z_slice_data(z_loan(slice)),
                    z_slice_len(z_loan(slice)), query);
      z_drop(z_move(slice));
    }
  }
}

imw_ret_t IMWZenoh::destroy_queryable(const char* keyexpr,
                                      imw_queryable_callback_fn* callback,
                                      void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::destroy_queryable";
    return IMW_ERROR;
  }
  const string keyexpr_s(keyexpr);
  std::unique_ptr<IMWZenohQueryable> queryable_to_undeclare;
  {
    absl::MutexLock lock(&queryables_mutex_);
    for (auto it = queryables_.begin(); it != queryables_.end(); ++it) {
      if (((*it)->get_keyexpr() == keyexpr_s) &&
          ((*it)->get_callback() == callback) &&
          ((*it)->get_user_context() == user_context)) {
        queryable_to_undeclare = std::move(*it);
        queryables_.erase(it);
        break;
      }
    }
  }

  if (queryable_to_undeclare != nullptr) {
    z_undeclare_queryable(
        z_move(queryable_to_undeclare->get_zenoh_queryable()));
    return IMW_OK;
  }

  // If we get here, we didn't find a matching queryable.
  LOG(ERROR) << "Could not find a queryable for " << keyexpr_s;
  return IMW_ERROR;
}

imw_ret_t IMWZenoh::queryable_reply(const void* untyped_reply_context,
                                    const char* reply_keyexpr,
                                    const void* reply_bytes,
                                    const size_t reply_bytes_len) {
  const IMWZenohReplyContext* reply_context =
      static_cast<const IMWZenohReplyContext*>(untyped_reply_context);
  z_query_reply_options_t options;
  z_query_reply_options_default(&options);

  z_view_keyexpr_t reply_view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&reply_view_keyexpr, reply_keyexpr)) {
    LOG(ERROR) << "unable to create key expression from: " << reply_keyexpr;
    return IMW_ERROR;
  }

  // Due to unfortunate historical reasons, payloads were originally copied
  // into and out of Zenoh using ze_serialize_buf() and ze_deserialize_slice().
  // This works fine, but adds a VarInt field (1-8 bytes, depending on the
  // value) before the byte block begins.  The only penalty is the extra few
  // bytes of that VarInt, so it was left as-is to avoid potential
  // incompatibility and regressions with already-deployed services. However,
  // when interoperating with rmw_zenoh ROS services and clients, the rmw_zenoh
  // side does not insert (or remove) the VarInt at the beginning of the
  // payload. As a result, for deserialization to start on the correct byte, we
  // must not write the VarInt. This is, of course, what we meant to do
  // originally for intra-Flowstate transport, and will ideally switch to at
  // some point.  But for the time being, we stick with the VarInt-prefixed
  // serialization for intra-Flowstate, and use no prefix for ROS rmw_zenoh
  // interop, via the functions z_bytes_copy_from_buf() and z_bytes_to_slice().
  z_owned_bytes_t reply_payload;
  if ((reply_context->options_ != nullptr) &&
      reply_context->options_->is_ros_service) {
    z_bytes_copy_from_buf(&reply_payload,
                          static_cast<const uint8_t*>(reply_bytes),
                          reply_bytes_len);
  } else {
    ze_serialize_buf(&reply_payload, static_cast<const uint8_t*>(reply_bytes),
                     reply_bytes_len);
  }
  const int8_t result =
      z_query_reply(reply_context->query_, z_loan(reply_view_keyexpr),
                    z_move(reply_payload), &options);

  if (result)
    return IMW_ERROR;
  else
    return IMW_OK;
}

imw_ret_t IMWZenoh::declare_liveliness_token(const char* keyexpr) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::declare_liveliness_token";
    return IMW_ERROR;
  }

  std::string keyexpr_str(keyexpr);

  absl::MutexLock lock(&liveliness_tokens_mutex_);
  auto it = liveliness_tokens_.find(keyexpr_str);
  if (it != liveliness_tokens_.end()) {
    // Found existing token. Returning the "already exists" error.
    return IMW_ALREADY_EXISTS;
  }

  z_owned_keyexpr_t zenoh_keyexpr;
  z_result_t result = z_keyexpr_from_str(&zenoh_keyexpr, keyexpr);
  if (result < 0) {
    LOG(ERROR) << "Unable to create publisher keyexpr ("
               << static_cast<int>(result) << ") from: " << keyexpr;
    return IMW_ERROR;
  }

  z_liveliness_token_options_t opts;
  z_liveliness_token_options_default(&opts);

  z_owned_liveliness_token_t token;

  result = z_liveliness_declare_token(z_loan(session_), &token,
                                      z_loan(zenoh_keyexpr), &opts);
  if (result < 0) {
    LOG(ERROR) << "z_liveliness_declare_token failed: ("
               << static_cast<int>(result) << "): " << keyexpr;
    return IMW_ERROR;
  }

  liveliness_tokens_.emplace(keyexpr_str,
                             std::make_unique<IMWZenohLivelinessToken>(token));
  return IMW_OK;
}

imw_ret_t IMWZenoh::drop_liveliness_token(const char* keyexpr) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::declare_liveliness_token";
    return IMW_ERROR;
  }

  std::string keyexpr_str(keyexpr);

  absl::MutexLock lock(&liveliness_tokens_mutex_);
  auto it = liveliness_tokens_.find(keyexpr_str);
  if (it == liveliness_tokens_.end()) {
    LOG(WARNING) << "No liveliness tokens for " << keyexpr;
    return IMW_UNDEFINED;
  }

  // The token has zero references. Removing it.
  z_liveliness_token_drop(z_move(it->second->get_token()));
  liveliness_tokens_.erase(it);

  return IMW_OK;
}

imw_ret_t IMWZenoh::liveliness_get(
    const char* keyexpr, imw_liveliness_get_callback_fn* callback,
    imw_liveliness_get_on_done_callback_fn* on_done, void* user_context) {
  if (!z_internal_check(session_)) {
    LOG(ERROR) << "Invalid session in IMWZenoh::liveliness_get";
    return IMW_ERROR;
  }

  z_view_keyexpr_t view_keyexpr;
  if (Z_OK != z_view_keyexpr_from_str(&view_keyexpr, keyexpr)) {
    LOG(ERROR) << "Invalid keyexpr for liveliness_get: " << keyexpr;
    return IMW_ERROR;
  }

  z_owned_closure_reply_t reply_closure;
  z_closure_reply(&reply_closure, static_liveliness_get_callback,
                  static_liveliness_get_drop,
                  new IMWLivelinessGetContext(this, keyexpr, callback, on_done,
                                              user_context));
  z_liveliness_get_options_t opts;
  z_liveliness_get_options_default(&opts);
  opts.timeout_ms = absl::ToInt64Milliseconds(kDefaultLivelinessGetTimeout);

  const z_result_t result = z_liveliness_get(
      z_loan(session_), z_loan(view_keyexpr), z_move(reply_closure), &opts);
  if (result < 0) {
    LOG(ERROR) << "z_liveliness_get() returned an error";
    return IMW_ERROR;
  }
  return IMW_OK;
}

const char* const IMWZenoh::version() { return kImwZenohVersion; }

}  // namespace intrinsic
