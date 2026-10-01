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

#include "intrinsic/platform/pubsub/pubsub.h"

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/pytypes.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/parse.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/liveliness_query.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/python/gil_aware_pubsub.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"
#include "pybind11/cast.h"
#include "pybind11/native_enum.h"
#include "pybind11_abseil/absl_casters.h"
#include "pybind11_abseil/no_throw_status.h"
#include "pybind11_abseil/status_casters.h"
#include "pybind11_protobuf/native_proto_caster.h"

namespace intrinsic {
namespace pubsub {

namespace {

// WrapXXXXCallback functions wrap the given callback into the code that
// acquires GIL.
//
// The callback passed to the adapter must be able to be copied in a
// separate thread without copying the msg_callback.
// This allows the message callback to capture variables which
// are not possible (or safe) to copy in a separate thread. This is the
// case when the callback captures a python function, since those cannot
// be copied without holding the GIL, and the adapter thread executing the
// callback does not know to acquire the GIL. Using a shared pointer to
// own the adapter callback satisfies these requirements.
template <typename T>
SubscriptionOkCallback<T> WrapSubscriptionOkCallback(
    pybind11::object msg_callback) {
  SubscriptionOkCallback<T> message_callback = {};
  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb = std::move(msg_callback)](const T& msg) {
      pybind11::gil_scoped_acquire gil;
      try {
        // This will create a copy in the py proto caster
        py_msg_cb(msg);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in message callback: " << e.what();
      }
    };
  }
  return message_callback;
}

template <typename T>
SubscriptionOkExpandedCallback<T> WrapSubscriptionOkExpandedCallback(
    pybind11::object msg_callback) {
  SubscriptionOkExpandedCallback<T> message_callback = {};
  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb = std::move(msg_callback)](
                           absl::string_view topic, const T& msg) {
      pybind11::gil_scoped_acquire gil;
      try {
        // This will create a copy in the py proto caster
        py_msg_cb(topic, msg);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in message callback: " << e.what();
      }
    };
  }
  return message_callback;
}

DeletionCallback WrapDeletionCallback(pybind11::object del_callback) {
  DeletionCallback deletion_callback = {};
  if (del_callback && !del_callback.is_none()) {
    deletion_callback = [py_del_cb =
                             std::move(del_callback)](absl::string_view topic) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_del_cb(topic);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in deletion callback: " << e.what();
      }
    };
  }
  return deletion_callback;
}

SubscriptionErrorCallback WrapErrorCallback(pybind11::object err_callback) {
  SubscriptionErrorCallback error_callback = {};
  if (err_callback && !err_callback.is_none()) {
    error_callback = [py_err_cb = std::move(err_callback)](
                         absl::string_view packet, absl::Status error) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_err_cb(packet, pybind11::google::DoNotThrowStatus(error));
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in error callback: " << e.what();
      }
    };
  }
  return error_callback;
}

SubscriptionErrorExpandedCallback WrapErrorExpandedCallback(
    pybind11::object err_callback) {
  SubscriptionErrorExpandedCallback error_callback = {};
  if (err_callback && !err_callback.is_none()) {
    error_callback = [py_err_cb = std::move(err_callback)](
                         absl::string_view topic, absl::string_view packet,
                         absl::Status error) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_err_cb(topic, packet, pybind11::google::DoNotThrowStatus(error));
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in error callback: " << e.what();
      }
    };
  }
  return error_callback;
}

KeyValueCallback WrapKeyValueCallback(pybind11::object cb) {
  if (!cb || cb.is_none()) {
    return [](absl::string_view key,
              std::unique_ptr<google::protobuf::Any> value) {};
  }
  return [py_cb = std::move(cb)](absl::string_view key,
                                 std::unique_ptr<google::protobuf::Any> value) {
    pybind11::gil_scoped_acquire gil;
    try {
      if (value) {
        py_cb(key, *value);
      } else {
        py_cb(key, pybind11::none());
      }
    } catch (const pybind11::error_already_set& e) {
      LOG(ERROR) << "Exception in KeyValueCallback: " << e.what();
    }
  };
}

OnDoneCallback WrapOnDoneCallback(pybind11::object cb) {
  if (!cb || cb.is_none()) {
    return [](absl::string_view key) {};
  }
  return [py_cb = std::move(cb)](absl::string_view key) {
    pybind11::gil_scoped_acquire gil;
    try {
      py_cb();
    } catch (const pybind11::error_already_set& e) {
      LOG(ERROR) << "Exception in OnDoneCallback: " << e.what();
    }
  };
}

LivelinessCallback WrapLivelinessCallback(pybind11::object msg_callback) {
  LivelinessCallback message_callback = {};
  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb = std::move(msg_callback)](
                           absl::string_view key, bool alive) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_msg_cb(key, alive);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in liveliness callback: " << e.what();
      }
    };
  }
  return message_callback;
}

LivelinessGetCallback WrapLivelinessGetCallback(pybind11::object msg_callback) {
  LivelinessGetCallback message_callback = {};
  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb =
                            std::move(msg_callback)](absl::string_view key) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_msg_cb(key);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in liveliness get callback: " << e.what();
      }
    };
  }
  return message_callback;
}

LivelinessGetCallback WrapLivelinessGetOnDoneCallback(
    pybind11::object msg_callback) {
  LivelinessGetCallback message_callback = {};
  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb =
                            std::move(msg_callback)](absl::string_view key) {
      pybind11::gil_scoped_acquire gil;
      try {
        py_msg_cb(key);
      } catch (const pybind11::error_already_set& e) {
        LOG(ERROR) << "Exception in liveliness get on_done callback: "
                   << e.what();
      }
    };
  }
  return message_callback;
}

absl::StatusOr<Subscription> CreateSubscriptionWithConfig(
    GilAwarePubSub* self, absl::string_view topic, const TopicConfig& config,
    const google::protobuf::Message& exemplar, pybind11::object msg_callback,
    pybind11::object err_callback) {
  return self->CreateSubscription(
      topic, config, exemplar,
      WrapSubscriptionOkCallback<google::protobuf::Message>(msg_callback),
      WrapErrorCallback(err_callback));
}

absl::StatusOr<Subscription> CreateSubscription(
    GilAwarePubSub* self, absl::string_view topic,
    const google::protobuf::Message& exemplar, pybind11::object msg_callback,
    pybind11::object err_callback) {
  return CreateSubscriptionWithConfig(self, topic, TopicConfig{}, exemplar,
                                      std::move(msg_callback),
                                      std::move(err_callback));
}

absl::StatusOr<Subscription> CreateRawSubscription(
    GilAwarePubSub* self, absl::string_view topic, const TopicConfig& config,
    pybind11::object msg_callback) {
  // The callback passed to the adapter must be able to be copied in a
  // separate thread without copying the msg_callback.
  // This allows the message callback to capture variables which
  // are not possible (or safe) to copy in a separate thread. This is the
  // case when the callback captures a python function, since those cannot
  // be copied without holding the GIL, and the adapter thread executing the
  // callback does not know to acquire the GIL. Using a shared pointer to
  // own the adapter callback satisfies these requirements.

  SubscriptionOkCallback<intrinsic_proto::pubsub::PubSubPacket>
      message_callback = {};

  if (msg_callback && !msg_callback.is_none()) {
    message_callback = [py_msg_cb = std::move(msg_callback)](
                           const intrinsic_proto::pubsub::PubSubPacket& msg) {
      pybind11::gil_scoped_acquire gil;
      // This will create a copy in the py proto caster.
      // Note that this callback receives a `PubSubPacket`, but passes the
      // packet's payload to `msg_callback`.
      py_msg_cb(msg.payload());
    };
  }

  return self->CreateSubscription(topic, config, std::move(message_callback));
}

absl::StatusOr<Subscription> CreateRawKVStoreSubscription(
    KeyValueStore* self, absl::string_view key_expression,
    const TopicConfig& config, pybind11::object value_callback,
    pybind11::object del_callback) {
  return self->CreateSubscription(
      key_expression, config,
      WrapSubscriptionOkExpandedCallback<google::protobuf::Any>(value_callback),
      WrapDeletionCallback(del_callback));
}

absl::StatusOr<Subscription> CreateKVStoreSubscription(
    KeyValueStore* self, absl::string_view key_expression,
    const TopicConfig& config, const google::protobuf::Message& exemplar,
    pybind11::object value_callback, pybind11::object del_callback,
    pybind11::object err_callback) {
  return self->CreateSubscription(
      key_expression, config, exemplar,
      WrapSubscriptionOkExpandedCallback<google::protobuf::Message>(
          value_callback),
      WrapDeletionCallback(del_callback),
      WrapErrorExpandedCallback(err_callback));
}

absl::StatusOr<KeyValueStore> CreateKeyValueStore(
    GilAwarePubSub* self, std::optional<std::string> prefix_override) {
  return self->KeyValueStore(prefix_override);
}

absl::StatusOr<KeyValueStore> CreateReplicationKVStore(GilAwarePubSub* self) {
  return self->KeyValueStore(std::string(intrinsic::kReplicationPrefix));
}

absl::StatusOr<KVQuery> GetAll(KeyValueStore* self, const std::string& key,
                               pybind11::object callback,
                               pybind11::object on_done) {
  return self->GetAll(key, WrapKeyValueCallback(std::move(callback)),
                      WrapOnDoneCallback(std::move(on_done)));
}

absl::StatusOr<google::protobuf::Any> Get(KeyValueStore* self,
                                          const std::string& key, int timeout) {
  return self->Get<google::protobuf::Any>(key, absl::Seconds(timeout));
}

absl::StatusOr<std::vector<std::string>> ListAllKeys(KeyValueStore* self,
                                                     int timeout) {
  return self->ListAllKeys(absl::Seconds(timeout));
}

absl::StatusOr<std::vector<std::string>> ListAllGlobalKeys(KeyValueStore* self,
                                                           int timeout) {
  return self->ListAllGlobalKeys(absl::Seconds(timeout));
}

absl::StatusOr<std::vector<std::string>> ListAllOnpremKeys(
    KeyValueStore* self, const std::string& workcell_name, int timeout) {
  return self->ListAllOnpremKeys(workcell_name, absl::Seconds(timeout));
}

absl::Status AdminCloudCopy(KeyValueStore* self, const std::string& source_key,
                            const std::string& target_key, int timeout) {
  return self->AdminCloudCopy(source_key, target_key, absl::Seconds(timeout));
}

absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Any>>
GetAllSynchronous(KeyValueStore* self, const std::string& keyexpr,
                  int timeout) {
  return self->GetAllSynchronous(keyexpr, absl::Seconds(timeout));
}

absl::StatusOr<std::string> GetWorkcellReplicationNamespace(KeyValueStore* self,
                                                            int timeout) {
  return self->GetWorkcellReplicationNamespace(absl::Seconds(timeout));
}

std::string MakeKey(pybind11::args args) {
  std::vector<std::string> parts;
  parts.reserve(args.size());

  for (const auto& arg : args) {
    parts.push_back(arg.cast<std::string>());
  }

  return KeyValueStore::MakeKeyFromVector(parts);
}

absl::Status DeclareLivelinessToken(GilAwarePubSub* self,
                                    absl::string_view keyexpr) {
  return self->DeclareLivelinessToken(keyexpr);
}

absl::Status DropLivelinessToken(GilAwarePubSub* self,
                                 absl::string_view keyexpr) {
  return self->DropLivelinessToken(keyexpr);
}

absl::StatusOr<LivelinessSubscription> CreateLivelinessSubscription(
    GilAwarePubSub* self, absl::string_view keyexpr,
    bool notify_about_existing_tokens, pybind11::object callback) {
  return self->CreateLivelinessSubscription(
      keyexpr, notify_about_existing_tokens, WrapLivelinessCallback(callback));
}

absl::StatusOr<LivelinessQuery> LivelinessGet(GilAwarePubSub* self,
                                              absl::string_view keyexpr,
                                              pybind11::object callback,
                                              pybind11::object on_done) {
  return self->LivelinessGet(keyexpr, WrapLivelinessGetCallback(callback),
                             WrapLivelinessGetOnDoneCallback(on_done));
}

absl::StatusOr<std::vector<std::string>> LivelinessGetAllSynchronous(
    GilAwarePubSub* self, absl::string_view keyexpr) {
  return self->LivelinessGetAllSynchronous(keyexpr);
}

void ParseCommandLine(const std::vector<std::string>& args) {
  std::vector<char*> argv_c;
  for (const auto& arg : args) {
    argv_c.push_back(const_cast<char*>(arg.c_str()));
  }
  int argc_c = argv_c.size();
  char** argv_c_ptr = argv_c.data();

  absl::ParseCommandLine(argc_c, argv_c_ptr);
}

struct PySubscriptionDeleter {
  void operator()(Subscription* s) {
    // To avoid deadlock, the call to Zenoh.imw_destroy_subscription() needs
    // to happen with the GIL released. Otherwise, the GIL and the internal
    // callback mutex are potentially locked in opposite order by this thread
    // and the Zenoh callback thread pool, which can deadlock, especially on
    // high-frequency topics.
    {
      pybind11::gil_scoped_release release_gil;
      s->Unsubscribe();
    }

    // The Python GIL will be re-acquired now that the previous scoped_release
    // has disappeared. With the re-acquired GIL, we can safely delete the
    // subscription_data_ struct in Subscription, which contains the Python
    // callback object. A deadlock can no longer occur, because a message
    // callback will no longer occur because the remainder of the destruction
    // call chain is holding the GIL.
    delete s;
  }
};

struct PyLivelinessSubscriptionDeleter {
  void operator()(LivelinessSubscription* s) {
    // To avoid deadlock, the call to Zenoh.imw_destroy_subscription() needs
    // to happen with the GIL released. Otherwise, the GIL and the internal
    // callback mutex are potentially locked in opposite order by this thread
    // and the Zenoh callback thread pool, which can deadlock, especially on
    // high-frequency topics.
    {
      pybind11::gil_scoped_release release_gil;
      s->Unsubscribe();
    }

    // The Python GIL will be re-acquired now that the previous scoped_release
    // has disappeared. With the re-acquired GIL, we can safely delete the
    // subscription_data_ struct in Subscription, which contains the Python
    // callback object. A deadlock can no longer occur, because a message
    // callback will no longer occur because the remainder of the destruction
    // call chain is holding the GIL.
    delete s;
  }
};

template <typename T>
struct PyReleaseGilDeleter {
  void operator()(T* p) const {
    pybind11::gil_scoped_release release_gil;
    delete p;
  }
};

}  // namespace

PYBIND11_MODULE(pubsub, m) {
  pybind11::google::ImportStatusModule();
  pybind11_protobuf::ImportNativeProtoCasters();

  pybind11::enum_<TopicConfig::TopicQoS>(m, "TopicQoS")
      .value("HighReliability", TopicConfig::TopicQoS::HighReliability)
      .value("Sensor", TopicConfig::TopicQoS::Sensor)
      .export_values();

  pybind11::class_<TopicConfig>(m, "TopicConfig")
      .def(pybind11::init<>())
      .def_readwrite("topic_qos", &TopicConfig::topic_qos);

  pybind11::class_<LivelinessQuery>(m, "LivelinessQuery");

  pybind11::class_<
      GilAwarePubSub,
      std::unique_ptr<GilAwarePubSub, PyReleaseGilDeleter<GilAwarePubSub>>>(
      m, "PubSub")
      .def(pybind11::init<>())
      .def(pybind11::init<std::string_view>(),
           pybind11::arg("participant_name"))
      .def(pybind11::init<std::string_view, std::string_view>(),
           pybind11::arg("participant_name"), pybind11::arg("config"))
      .def_static("DestroySessionWhenUnused", &PubSub::DestroySessionWhenUnused,
                  pybind11::call_guard<pybind11::gil_scoped_release>())
      // Cast required for overloaded methods:
      // https://pybind11.readthedocs.io/en/stable/classes.html#overloaded-methods
      .def("CreatePublisher", &GilAwarePubSub::CreatePublisher,
           pybind11::arg("topic"), pybind11::arg("config") = TopicConfig{})
      .def("CreateSubscription", &CreateRawSubscription, pybind11::arg("topic"),
           pybind11::arg("config"), pybind11::arg("msg_callback") = nullptr)
      .def("CreateSubscription", &CreateSubscriptionWithConfig,
           pybind11::arg("topic"), pybind11::arg("config"),
           pybind11::arg("exemplar"), pybind11::arg("msg_callback") = nullptr,
           pybind11::arg("error_callback") = nullptr)
      .def("CreateSubscription", &CreateSubscription, pybind11::arg("topic"),
           pybind11::arg("exemplar"), pybind11::arg("msg_callback") = nullptr,
           pybind11::arg("error_callback") = nullptr)
      .def("KeyValueStore", &CreateKeyValueStore,
           pybind11::arg("prefix_override") = std::nullopt)
      .def("ReplicationKeyValueStore", &CreateReplicationKVStore)
      .def("DeclareLivelinessToken", &DeclareLivelinessToken)
      .def("DropLivelinessToken", &DropLivelinessToken)
      .def("CreateLivelinessSubscription", &CreateLivelinessSubscription)
      .def("LivelinessGet", &LivelinessGet, pybind11::arg("keyexpr"),
           pybind11::arg("callback"), pybind11::arg("on_done"))
      .def("LivelinessGetAllSynchronous", &LivelinessGetAllSynchronous,
           pybind11::arg("keyexpr"));

  pybind11::class_<Publisher,
                   std::unique_ptr<Publisher, PyReleaseGilDeleter<Publisher>>>(
      m, "Publisher")
      .def("Publish",
           static_cast<absl::Status (Publisher::*)(
               const google::protobuf::Message&) const>(&Publisher::Publish),
           pybind11::arg("message"))
      .def("TopicName", &Publisher::TopicName)
      .def("HasMatchingSubscribers", &Publisher::HasMatchingSubscribers);

  pybind11::class_<KVQuery>(m, "KVQuery");

  pybind11::class_<KeyValueStore::SetWithVerificationOptions> set_options(
      m, "SetWithVerificationOptions");

  pybind11::native_enum<
      KeyValueStore::SetWithVerificationOptions::VerificationMode>(
      set_options, "VerificationMode", "enum.Enum")
      .value("FIRST_REPLY", KeyValueStore::SetWithVerificationOptions::
                                VerificationMode::kFirstReply)
      .value("HIGH_CONSISTENCY", KeyValueStore::SetWithVerificationOptions::
                                     VerificationMode::kHighConsistency)
      .finalize();

  set_options.def(pybind11::init<>())
      .def_readwrite("mode", &KeyValueStore::SetWithVerificationOptions::mode)
      .def_property(
          "timeout",
          [](const KeyValueStore::SetWithVerificationOptions& opt) {
            return absl::ToDoubleSeconds(opt.timeout);
          },
          [](KeyValueStore::SetWithVerificationOptions& opt, double s) {
            opt.timeout = absl::Seconds(s);
          });

  pybind11::class_<KeyValueStore>(m, "KeyValueStore")
      .def("Set",
           static_cast<absl::Status (KeyValueStore::*)(
               absl::string_view, const google::protobuf::Message&,
               std::optional<bool>)>(
               &KeyValueStore::Set<const google::protobuf::Message&>),
           pybind11::arg("key"), pybind11::arg("value"),
           pybind11::arg("high_consistency"))
      .def("Set",
           static_cast<absl::Status (KeyValueStore::*)(
               absl::string_view, const google::protobuf::Message&)>(
               &KeyValueStore::Set<const google::protobuf::Message&>),
           pybind11::arg("key"), pybind11::arg("value"))
      .def("SetWithVerification",
           static_cast<absl::Status (KeyValueStore::*)(
               absl::string_view, const google::protobuf::Message&,
               const KeyValueStore::SetWithVerificationOptions&)>(
               &KeyValueStore::SetWithVerification<
                   const google::protobuf::Message&>),
           pybind11::arg("key"), pybind11::arg("value"),
           pybind11::arg("options") =
               KeyValueStore::SetWithVerificationOptions())
      .def("Get", &Get, pybind11::arg("key"), pybind11::arg("timeout") = 10)
      .def("GetAll", &GetAll)
      .def("GetAllSynchronous", &GetAllSynchronous, pybind11::arg("keyexpr"),
           pybind11::arg("timeout") = 10)
      .def("ListAllKeys", &ListAllKeys, pybind11::arg("timeout") = 10)
      .def("ListAllGlobalKeys", &ListAllGlobalKeys,
           pybind11::arg("timeout") = 10)
      .def("ListAllOnpremKeys", &ListAllOnpremKeys,
           pybind11::arg("workcell_name"), pybind11::arg("timeout") = 10)
      .def("Delete", &KeyValueStore::Delete, pybind11::arg("key"))
      .def("AdminCloudCopy", &AdminCloudCopy, pybind11::arg("source_key"),
           pybind11::arg("target_key"), pybind11::arg("timeout") = 10)
      .def("CreateSubscription", &CreateRawKVStoreSubscription,
           pybind11::arg("key_expression"), pybind11::arg("config"),
           pybind11::arg("value_callback") = nullptr,
           pybind11::arg("del_callback") = nullptr)
      .def("CreateSubscription", &CreateKVStoreSubscription,
           pybind11::arg("key_expression"), pybind11::arg("config"),
           pybind11::arg("exemplar"), pybind11::arg("value_callback") = nullptr,
           pybind11::arg("del_callback") = nullptr,
           pybind11::arg("err_callback") = nullptr)
      .def("GetWorkcellReplicationNamespace", &GetWorkcellReplicationNamespace,
           pybind11::arg("timeout") = 10)
      .def("GetGlobalReplicationNamespace",
           &KeyValueStore::GetGlobalReplicationNamespace)
      .def_static("MakeKey", &MakeKey);

  // The python GIL does not need to be locked during the entire destructor
  // of this class. Instead, the custom deleter provided during its
  // construction will acquire the GIL only during the deletion of the
  // SubscriptionData object, which holds the Python callback.
  pybind11::class_<Subscription,
                   std::unique_ptr<Subscription, PySubscriptionDeleter>>(
      m, "Subscription")
      .def("TopicName", &Subscription::TopicName)
      .def("Unsubscribe", &Subscription::Unsubscribe,
           pybind11::call_guard<pybind11::gil_scoped_release>());

  // The python GIL does not need to be locked during the entire destructor
  // of this class. Instead, the custom deleter provided during its
  // construction will acquire the GIL only during the deletion of the
  // SubscriptionData object, which holds the Python callback.
  pybind11::class_<
      LivelinessSubscription,
      std::unique_ptr<LivelinessSubscription, PyLivelinessSubscriptionDeleter>>(
      m, "LivelinessSubscription")
      .def("KeyExpression", &LivelinessSubscription::KeyExpression)
      .def("Unsubscribe", &LivelinessSubscription::Unsubscribe,
           pybind11::call_guard<pybind11::gil_scoped_release>());

  // Helper function for passing command line flags from Python code
  // to C++. Can be used in Python tests which start their own Zenoh routers,
  // and need to pass their URLs to C++ code.
  m.def("parse_command_line", &ParseCommandLine);
  m.def("imw_is_initialized", []() { return Zenoh().imw_is_initialized(); });
}

}  // namespace pubsub
}  // namespace intrinsic
