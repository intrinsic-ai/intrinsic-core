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

#include "intrinsic/storage/content_addressable_storage/cpp/client_helpers.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/crc/crc32c.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "riegeli/bytes/pullable_reader.h"
#include "riegeli/bytes/string_reader.h"
#include "riegeli/bytes/string_writer.h"

namespace intrinsic {

using ::intrinsic_proto::content_addressable_storage::v1::ChecksummedData;
using ::intrinsic_proto::content_addressable_storage::v1::
    ContentAddressableStorageService;
using ::intrinsic_proto::content_addressable_storage::v1::CreateRequest;
using ::intrinsic_proto::content_addressable_storage::v1::CreateResponse;
using ::intrinsic_proto::content_addressable_storage::v1::GetRangeRequest;
using ::intrinsic_proto::content_addressable_storage::v1::GetRangeResponse;
using ::intrinsic_proto::content_addressable_storage::v1::GetRequest;
using ::intrinsic_proto::content_addressable_storage::v1::GetResponse;
using ::intrinsic_proto::content_addressable_storage::v1::StatRequest;
using ::intrinsic_proto::content_addressable_storage::v1::StatResponse;

namespace {

bool IsRetriableStatusCode(absl::StatusCode code) {
  switch (code) {
    case absl::StatusCode::kUnavailable:
    case absl::StatusCode::kResourceExhausted:
    case absl::StatusCode::kDeadlineExceeded:
    case absl::StatusCode::kAborted:
    case absl::StatusCode::kDataLoss:
      return true;
    default:
      return false;
  }
}

absl::Status VerifyChecksum(const ChecksummedData& data,
                            std::string_view object_id, int64_t offset) {
  if (!data.has_crc32c()) {
    return absl::OkStatus();
  }
  const uint32_t actual_crc =
      static_cast<uint32_t>(absl::ComputeCrc32c(data.content()));
  if (actual_crc != data.crc32c()) {
    return absl::DataLossError(absl::StrFormat(
        "CRC32C checksum mismatch for '%s' at offset %d: client computed "
        "0x%08x, server reported 0x%08x",
        object_id, offset, actual_crc, data.crc32c()));
  }
  return absl::OkStatus();
}

std::unique_ptr<::grpc::ClientContext> MakeClientContext(
    const std::function<std::unique_ptr<::grpc::ClientContext>()>& factory) {
  if (factory != nullptr) {
    if (auto ctx = factory(); ctx != nullptr) {
      return ctx;
    }
  }
  return std::make_unique<::grpc::ClientContext>();
}

// Owns the ClientContext and ClientReader for a single RPC attempt.
// Destroying or resetting this object automatically cancels the context, drains
// Finish(), and tears down the stream and context in the required order.
class StreamAttempt {
 public:
  virtual ~StreamAttempt() = default;

  // Reads the next non-empty verified chunk at `expected_offset`, updating
  // `total_object_size` if reported by the server. Returns `std::nullopt` on
  // clean stream EOF.
  virtual absl::StatusOr<std::optional<std::string>> NextChunk(
      int64_t expected_offset, std::optional<int64_t>& total_object_size) = 0;

  // Cancels any still-open stream and returns the final status from `Finish()`.
  virtual absl::Status Close() = 0;
};

class GetRangeStreamAttempt final : public StreamAttempt {
 public:
  static absl::StatusOr<std::unique_ptr<StreamAttempt>> Create(
      ContentAddressableStorageService::StubInterface* cas_stub,
      std::string_view object_id, int64_t read_offset,
      const std::function<std::unique_ptr<::grpc::ClientContext>()>&
          context_factory) {
    auto context = MakeClientContext(context_factory);
    GetRangeRequest request;
    request.set_object_id(object_id);
    request.set_read_offset(read_offset);
    auto stream = cas_stub->GetRange(context.get(), request);
    if (stream == nullptr) {
      return absl::UnavailableError("Failed to create CAS GetRange stream.");
    }
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(new GetRangeStreamAttempt(
        object_id, std::move(context), std::move(stream)));
  }

  ~GetRangeStreamAttempt() override { (void)Close(); }

  absl::Status Close() override {
    if (stream_ == nullptr) {
      return absl::OkStatus();
    }
    context_->TryCancel();
    const absl::Status status = ToAbslStatus(stream_->Finish());
    stream_.reset();
    context_.reset();
    return status;
  }

  absl::StatusOr<std::optional<std::string>> NextChunk(
      int64_t expected_offset,
      std::optional<int64_t>& total_object_size) override {
    while (stream_ != nullptr) {
      GetRangeResponse response;
      if (!stream_->Read(&response)) {
        absl::Status finish_status = ToAbslStatus(stream_->Finish());
        stream_.reset();
        context_.reset();
        if (!finish_status.ok()) {
          return finish_status;
        }
        return std::nullopt;
      }

      if (response.chunk_offset() != expected_offset) {
        return absl::InternalError(absl::StrFormat(
            "Corrupt stream for '%s': expected chunk at offset %d, but "
            "received %d",
            object_id_, expected_offset, response.chunk_offset()));
      }
      if (response.total_object_size() > 0) {
        total_object_size = response.total_object_size();
      }
      INTR_RETURN_IF_ERROR(VerifyChecksum(response.checksummed_data(),
                                          object_id_, expected_offset));
      std::string chunk =
          std::move(*response.mutable_checksummed_data()->mutable_content());
      if (!chunk.empty()) {
        return chunk;
      }
    }
    return std::nullopt;
  }

 private:
  explicit GetRangeStreamAttempt(
      std::string_view object_id,
      std::unique_ptr<::grpc::ClientContext> context,
      std::unique_ptr<::grpc::ClientReaderInterface<GetRangeResponse>> stream)
      : object_id_(object_id),
        context_(std::move(context)),
        stream_(std::move(stream)) {}

  std::string_view object_id_;
  // `context_` MUST outlive `stream_`.
  std::unique_ptr<::grpc::ClientContext> context_;
  std::unique_ptr<::grpc::ClientReaderInterface<GetRangeResponse>> stream_;
};

class GetStreamAttempt final : public StreamAttempt {
 public:
  static absl::StatusOr<std::unique_ptr<StreamAttempt>> Create(
      ContentAddressableStorageService::StubInterface* cas_stub,
      std::string_view object_id, int64_t read_offset,
      const std::function<std::unique_ptr<::grpc::ClientContext>()>&
          context_factory,
      std::optional<int64_t>& total_object_size) {
    if (read_offset != 0) {
      return absl::FailedPreconditionError(absl::StrFormat(
          "Cannot open CAS Get stream at non-zero offset %d.", read_offset));
    }
    if (!total_object_size.has_value()) {
      auto stat_context = MakeClientContext(context_factory);
      StatRequest stat_req;
      stat_req.set_object_id(object_id);
      StatResponse stat_resp;
      INTR_RETURN_IF_ERROR(ToAbslStatus(
          cas_stub->Stat(stat_context.get(), stat_req, &stat_resp)));
      if (stat_resp.size() > 0) {
        total_object_size = stat_resp.size();
      }
    }
    auto context = MakeClientContext(context_factory);
    GetRequest request;
    request.set_object_id(object_id);
    auto stream = cas_stub->Get(context.get(), request);
    if (stream == nullptr) {
      return absl::UnavailableError("Failed to create CAS Get stream.");
    }
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(
        new GetStreamAttempt(object_id, std::move(context), std::move(stream)));
  }

  ~GetStreamAttempt() override { (void)Close(); }

  absl::Status Close() override {
    if (stream_ == nullptr) {
      return absl::OkStatus();
    }
    context_->TryCancel();
    const absl::Status status = ToAbslStatus(stream_->Finish());
    stream_.reset();
    context_.reset();
    return status;
  }

  absl::StatusOr<std::optional<std::string>> NextChunk(
      int64_t expected_offset,
      std::optional<int64_t>& /*total_object_size*/) override {
    while (stream_ != nullptr) {
      GetResponse response;
      if (!stream_->Read(&response)) {
        const absl::Status finish_status = ToAbslStatus(stream_->Finish());
        stream_.reset();
        context_.reset();
        if (!finish_status.ok()) {
          return finish_status;
        }
        return std::nullopt;
      }

      INTR_RETURN_IF_ERROR(VerifyChecksum(response.checksummed_data(),
                                          object_id_, expected_offset));
      std::string chunk =
          std::move(*response.mutable_checksummed_data()->mutable_content());
      if (!chunk.empty()) {
        return chunk;
      }
    }
    return std::nullopt;
  }

 private:
  explicit GetStreamAttempt(
      std::string_view object_id,
      std::unique_ptr<::grpc::ClientContext> context,
      std::unique_ptr<::grpc::ClientReaderInterface<GetResponse>> stream)
      : object_id_(object_id),
        context_(std::move(context)),
        stream_(std::move(stream)) {}

  std::string_view object_id_;
  // `context_` MUST outlive `stream_`.
  std::unique_ptr<::grpc::ClientContext> context_;
  std::unique_ptr<::grpc::ClientReaderInterface<GetResponse>> stream_;
};

class ContentAddressableStorageResumableReader final
    : public ::riegeli::PullableReader {
 public:
  static absl::StatusOr<std::unique_ptr<::riegeli::Reader>> Create(
      std::string_view object_id, ResumableDownloadOptions options) {
    if (options.cas_stub == nullptr) {
      return absl::InvalidArgumentError("cas_stub must not be null.");
    }
    if (options.read_offset < 0) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Invalid read_offset %d; must be >= 0.", options.read_offset));
    }
    if (options.max_retries.has_value() && *options.max_retries < 0) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Invalid max_retries %d; must be >= 0 or std::nullopt.",
          *options.max_retries));
    }
    if (options.initial_backoff < absl::ZeroDuration() ||
        options.initial_backoff == absl::InfiniteDuration() ||
        options.max_backoff < absl::ZeroDuration() ||
        options.max_backoff == absl::InfiniteDuration() ||
        !std::isfinite(options.backoff_multiplier) ||
        options.backoff_multiplier <= 0.0) {
      return absl::InvalidArgumentError(
          "Backoff durations must be finite and >= 0, and backoff_multiplier "
          "must be finite and > 0.");
    }
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(new ContentAddressableStorageResumableReader(
        object_id, std::move(options)));
  }

  ContentAddressableStorageResumableReader(
      const ContentAddressableStorageResumableReader&) = delete;
  ContentAddressableStorageResumableReader& operator=(
      const ContentAddressableStorageResumableReader&) = delete;
  ContentAddressableStorageResumableReader(
      ContentAddressableStorageResumableReader&&) = delete;
  ContentAddressableStorageResumableReader& operator=(
      ContentAddressableStorageResumableReader&&) = delete;

 private:
  explicit ContentAddressableStorageResumableReader(
      std::string_view object_id, ResumableDownloadOptions options)
      : object_id_(object_id), options_(std::move(options)) {
    set_limit_pos(static_cast<::riegeli::Position>(options_.read_offset));
  }

  void Done() override {
    ::riegeli::PullableReader::Done();
    if (active_attempt_ != nullptr) {
      const absl::Status status = active_attempt_->Close();
      active_attempt_.reset();
      if (!status.ok() && !absl::IsCancelled(status)) {
        Fail(status);
      }
    }
    chunk_.clear();
    chunk_.shrink_to_fit();
  }

  bool PullBehindScratch(std::size_t /*recommended_length*/) override {
    set_buffer();
    if (!ok() || eof_) {
      return false;
    }

    int consecutive_retries = 0;
    absl::Duration current_backoff =
        std::min(options_.initial_backoff, options_.max_backoff);
    std::optional<absl::BitGen> bitgen;

    while (true) {
      const absl::Status status = ReceiveNextChunk();
      if (status.ok()) {
        return available() > 0;
      }

      active_attempt_.reset();

      const bool out_of_retries = options_.max_retries.has_value() &&
                                  consecutive_retries >= *options_.max_retries;
      if (out_of_retries || !CanRetry(status)) {
        return Fail(status);
      }

      consecutive_retries++;
      LOG(WARNING) << absl::StrFormat(
          "Transient error during CAS download of %s at offset %d: %s. "
          "Retrying (%d) in up to %v...",
          object_id_, limit_pos(), status.ToString(), consecutive_retries,
          current_backoff);

      if (!bitgen.has_value()) {
        bitgen.emplace();
      }
      absl::SleepFor(absl::Uniform(*bitgen, 0.0, 1.0) * current_backoff);
      current_backoff = std::min(current_backoff * options_.backoff_multiplier,
                                 options_.max_backoff);
    }
  }

  [[nodiscard]] bool CanRetry(const absl::Status& status) const {
    // `Get` always streams from byte 0, so it can only be retried before any
    // bytes have been handed out to the caller.
    return IsRetriableStatusCode(status.code()) &&
           (!use_get_ || limit_pos() == 0);
  }

  absl::Status ReceiveNextChunk() {
    while (true) {
      if (active_attempt_ == nullptr) {
        INTR_ASSIGN_OR_RETURN(
            active_attempt_,
            use_get_ ? GetStreamAttempt::Create(
                           options_.cas_stub, object_id_,
                           static_cast<int64_t>(limit_pos()),
                           options_.context_factory, total_object_size_)
                     : GetRangeStreamAttempt::Create(
                           options_.cas_stub, object_id_,
                           static_cast<int64_t>(limit_pos()),
                           options_.context_factory));
      }

      absl::StatusOr<std::optional<std::string>> next =
          active_attempt_->NextChunk(static_cast<int64_t>(limit_pos()),
                                     total_object_size_);
      if (!next.ok()) {
        active_attempt_.reset();
        if (next.status().code() == absl::StatusCode::kUnimplemented &&
            !use_get_ && limit_pos() == 0) {
          LOG(INFO) << "GetRange is not implemented by the CAS server; falling "
                       "back to Get for '"
                    << object_id_ << "'.";
          use_get_ = true;
          continue;
        }
        if (!total_object_size_.has_value() ||
            static_cast<int64_t>(limit_pos()) < *total_object_size_) {
          return next.status();
        }
        next = std::nullopt;
      }

      if (next->has_value()) {
        chunk_ = *std::move(*next);
        set_buffer(chunk_.data(), chunk_.size());
        move_limit_pos(chunk_.size());
        return absl::OkStatus();
      }

      // Stream finished cleanly: verify we received all expected bytes.
      if (total_object_size_.has_value() &&
          static_cast<int64_t>(limit_pos()) < *total_object_size_) {
        return absl::DataLossError(absl::StrFormat(
            "Incomplete download for '%s': stream ended at offset %d, expected "
            "%d.",
            object_id_, limit_pos(), *total_object_size_));
      }
      eof_ = true;
      return absl::OkStatus();
    }
  }

  std::string object_id_;
  ResumableDownloadOptions options_;
  std::unique_ptr<StreamAttempt> active_attempt_;

  std::string chunk_;
  std::optional<int64_t> total_object_size_;
  bool use_get_ = false;
  bool eof_ = false;
};

}  // namespace

::absl::StatusOr<std::unique_ptr<::riegeli::Reader>>
ContentAddressableStorageGetResumableReader(std::string_view object_id,
                                            ResumableDownloadOptions options) {
  return ContentAddressableStorageResumableReader::Create(object_id,
                                                          std::move(options));
}

::absl::Status ContentAddressableStorageGet(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    std::string_view object_id, ::riegeli::Writer* writer) {
  GetRequest request;
  request.set_object_id(object_id);

  auto stream = cas_stub->Get(context, request);
  GetResponse response;
  while (stream->Read(&response)) {
    auto& content = response.checksummed_data().content();
    bool ok = writer->Write(content);
    if (!ok) {
      LOG(ERROR) << "Failed to write to writer: " << writer->status();
    }
    // TODO(b/289500064): Add checksum check.
  }
  bool ok = writer->Flush();
  if (!ok) {
    LOG(ERROR) << "Failed to flush writer: " << writer->status();
  }
  return ToAbslStatus(stream->Finish());
}

::absl::StatusOr<std::string> ContentAddressableStorageCreate(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    ::riegeli::Reader* reader, std::size_t chunk_size) {
  if (chunk_size == 0) {
    return absl::InvalidArgumentError("chunk_size must be greater than 0");
  }

  CreateResponse response;
  auto stream = cas_stub->Create(context, &response);

  CreateRequest request;
  while (true) {
    std::size_t bytes_read = 0;
    const bool read_ok = reader->Read(
        chunk_size, *request.mutable_checksummed_data()->mutable_content(),
        &bytes_read);
    if (!read_ok && bytes_read == 0) {
      break;
    }
    // TODO(b/289500064): Add checksumming.

    if (!stream->Write(request)) {
      return ToAbslStatus(stream->Finish());
    }
    request.Clear();
  }
  if (!reader->ok()) {
    context->TryCancel();
    stream->Finish();
    return reader->status();
  }
  if (!stream->WritesDone()) {
    return ToAbslStatus(stream->Finish());
  }
  INTR_RETURN_IF_ERROR(ToAbslStatus(stream->Finish()));
  return response.object_id();
}

::absl::StatusOr<std::string> ContentAddressableStorageGet(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    std::string_view object_id) {
  std::string content;
  ::riegeli::StringWriter writer(&content);
  INTR_RETURN_IF_ERROR(
      ContentAddressableStorageGet(context, cas_stub, object_id, &writer))
      << "Failed to get object " << object_id;
  if (!writer.Close()) {
    LOG(ERROR) << "Failed to close writer: " << writer.status();
    return writer.status();
  }
  return content;
}

::absl::StatusOr<std::string> ContentAddressableStorageCreate(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    ::absl::string_view content, std::size_t chunk_size) {
  ::riegeli::StringReader reader(content);
  INTR_ASSIGN_OR_RETURN(auto res, ContentAddressableStorageCreate(
                                      context, cas_stub, &reader, chunk_size));
  if (!reader.Close()) {
    LOG(ERROR) << "Failed to close reader: " << reader.status();
    return reader.status();
  }
  return res;
}
}  // namespace intrinsic
