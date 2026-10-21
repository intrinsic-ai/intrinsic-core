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

#include "intrinsic/simulation/gazebo/grpc_proxy/grpc_relay.h"

#include <atomic>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/generic/callback_generic_service.h"
#include "grpcpp/generic/generic_stub.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/client_callback.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace simulation {
namespace {
class RelayReactor : public grpc::ServerGenericBidiReactor {
 public:
  RelayReactor(grpc::GenericCallbackServerContext* context,
               grpc::GenericStub* stub,
               const ClientContextFactory& client_context_factory) {
    client_context_ = client_context_factory();
    client_context_->set_deadline(context->deadline());

    stub->PrepareBidiStreamingCall(client_context_.get(), context->method(),
                                   grpc::StubOptions(), &stub_reactor_);

    // Hold the client reactor while RelayReactor callbacks can still initiate
    // operations on stub_reactor_.
    stub_reactor_.AddHold();
    stub_reactor_.StartRead(&response_buffer_);
    StartRead(&request_buffer_);
    stub_reactor_.StartCall();
  }

  // Triggered when we finish reading a REQUEST from the Downstream Client
  void OnReadDone(bool ok) override ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    if (!stub_hold_) {
      return;
    }
    if (!ok) {
      stub_reactor_.StartWritesDone();
      return;
    }
    stub_reactor_.StartWrite(&request_buffer_);
  }

  // Triggered when we finish writing a RESPONSE to the Downstream Client
  void OnWriteDone(bool ok) override ABSL_LOCKS_EXCLUDED(mu_) {
    absl::MutexLock lock(&mu_);
    if (!ok) {
      client_context_->TryCancel();
      ReleaseStubHold();
      MaybeFinish(
          grpc::Status(grpc::StatusCode::ABORTED, "Downstream write failed"));
      return;
    }
    if (stub_hold_) {
      // The response_buffer_ is now free. Read the next response from the
      // Upstream Server.
      stub_reactor_.StartRead(&response_buffer_);
    }
  }

  void OnCancel() override { client_context_->TryCancel(); }

  void OnDone() override {
    // Server side is officially finished.
    DecrementRefCount();
  }

  void MaybeFinish(const grpc::Status& s) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (!finished_) {
      finished_ = true;
      Finish(s);
    }
  }

 private:
  void ReleaseStubHold() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    if (stub_hold_) {
      stub_hold_ = false;
      stub_reactor_.RemoveHold();
    }
  }

  void DecrementRefCount() {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

  // Atomic ref count starts at 2 (one for Server side, one for Client side).
  std::atomic<int> refs_{2};

  absl::Mutex mu_;
  bool stub_hold_ ABSL_GUARDED_BY(mu_) = true;
  bool finished_ ABSL_GUARDED_BY(mu_) = false;

  std::unique_ptr<grpc::ClientContext> client_context_;
  grpc::ByteBuffer request_buffer_;
  grpc::ByteBuffer response_buffer_;

  class StubReactor
      : public grpc::ClientBidiReactor<grpc::ByteBuffer, grpc::ByteBuffer> {
   public:
    explicit StubReactor(RelayReactor* parent) : parent_(parent) {}

    // Triggered when we finish reading a RESPONSE from the Upstream Server.
    void OnReadDone(bool ok) override ABSL_LOCKS_EXCLUDED(parent_->mu_) {
      absl::MutexLock lock(&parent_->mu_);
      if (!ok) {
        parent_->ReleaseStubHold();
        return;
      }
      // Forward the response payload to the downstream client.
      parent_->StartWrite(&parent_->response_buffer_);
    }

    // Triggered when we finish writing a REQUEST to the Upstream Server.
    void OnWriteDone(bool ok) override ABSL_LOCKS_EXCLUDED(parent_->mu_) {
      absl::MutexLock lock(&parent_->mu_);
      if (!ok || !parent_->stub_hold_) {
        return;
      }
      parent_->StartRead(&parent_->request_buffer_);
    }

    void OnDone(const grpc::Status& s) override
        ABSL_LOCKS_EXCLUDED(parent_->mu_) {
      {
        absl::MutexLock lock(&parent_->mu_);
        parent_->MaybeFinish(s);
      }
      parent_->DecrementRefCount();
    }

   private:
    RelayReactor* parent_;
  } stub_reactor_{this};
};

class SyncRelayHandler : public grpc::CallbackGenericService {
 public:
  SyncRelayHandler(std::shared_ptr<grpc::Channel> target_channel,
                   ClientContextFactory client_context_factory)
      : stub_(target_channel),
        client_context_factory_(std::move(client_context_factory)) {}

  grpc::ServerGenericBidiReactor* CreateReactor(
      grpc::GenericCallbackServerContext* context) override {
    return new RelayReactor(context, &stub_, client_context_factory_);
  }

 private:
  grpc::GenericStub stub_;
  ClientContextFactory client_context_factory_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<GrpcRelay>> GrpcRelay::CreateAndStart(
    int port, const ConnectionParams& connection_params,
    absl::Duration connect_timeout) {
  INTR_ASSIGN_OR_RETURN(auto channel, Channel::MakeFromAddress(
                                          connection_params, connect_timeout));
  return CreateAndStart(port, std::move(channel));
}

absl::StatusOr<std::unique_ptr<GrpcRelay>> GrpcRelay::CreateAndStart(
    int port, std::shared_ptr<Channel> channel) {
  auto relay = std::unique_ptr<GrpcRelay>(new GrpcRelay(std::move(channel)));

  LOG(INFO) << "Starting relay server at port " << port;

  grpc::ServerBuilder builder;
  builder.AddListeningPort(absl::StrCat("[::]:", port),
                           grpc::InsecureServerCredentials());
  builder.RegisterCallbackGenericService(relay->generic_service_.get());
  relay->server_ = builder.BuildAndStart();

  if (relay->server_ == nullptr) {
    return AbortedErrorBuilder().LogError() << "Failed to start relay server.";
  }

  return relay;
}

GrpcRelay::GrpcRelay(std::shared_ptr<Channel> channel)
    : channel_(std::move(channel)) {
  generic_service_ = std::make_unique<SyncRelayHandler>(
      channel_->GetChannel(), channel_->GetClientContextFactory());
}

GrpcRelay::~GrpcRelay() { Shutdown(); }

void GrpcRelay::Shutdown() {
  if (server_) {
    constexpr absl::Duration kServerShutdownTimeout = absl::Seconds(2);
    server_->Shutdown(absl::ToChronoTime(absl::Now() + kServerShutdownTimeout));
  }
}

}  // namespace simulation
}  // namespace intrinsic
