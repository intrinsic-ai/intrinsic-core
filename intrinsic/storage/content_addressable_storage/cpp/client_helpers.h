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

#ifndef INTRINSIC_STORAGE_CONTENT_ADDRESSABLE_STORAGE_CLIENT_HELPERS_H_
#define INTRINSIC_STORAGE_CONTENT_ADDRESSABLE_STORAGE_CLIENT_HELPERS_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/writer.h"

namespace intrinsic {

// Recommended size of content to be included into one stream request.
//
// Why 1 MiB?
// https://cloud.google.com/blog/products/gcp/optimizing-your-cloud-storage-performance-google-cloud-performance-atlas
// suggests that 1 MB+ is a good chunk size.
inline constexpr std::size_t kContentAddressableStorageDefaultUploadChunkSize =
    1 * 1024 * 1024;

// Configuration options for resumable CAS downloads.
struct ResumableDownloadOptions {
  // Stub for the content-addressable storage service. Must not be null and must
  // outlive the reader.
  ::intrinsic_proto::content_addressable_storage::v1::
      ContentAddressableStorageService::StubInterface* cas_stub = nullptr;

  // Starting byte offset in the CAS object. Must be >= 0.
  int64_t read_offset = 0;

  // Maximum consecutive retries per chunk before failing. Must be >= 0 (`0`
  // disables retries; `std::nullopt` retries indefinitely with backoff).
  std::optional<int> max_retries = 5;

  // Initial backoff duration between retries. Must be finite and >= 0.
  absl::Duration initial_backoff = absl::Milliseconds(100);

  // Maximum backoff duration between retries. Must be finite and >= 0.
  absl::Duration max_backoff = absl::Seconds(2);

  // Exponential backoff multiplier. Must be finite and > 0.
  double backoff_multiplier = 1.5;

  // Optional factory invoked on each RPC attempt to create a fresh
  // `ClientContext` (e.g., when per-RPC metadata, deadlines, or credentials
  // must be attached on every retry).
  std::function<std::unique_ptr<::grpc::ClientContext>()> context_factory =
      nullptr;
};

// Returns a pull-based Riegeli Reader that streams `object_id` from
// Content-Addressable Storage (CAS) with automatic chunk-level resumption on
// transient gRPC errors, or `InvalidArgumentError` if `options` are invalid.
//
// The underlying gRPC stream is opened lazily on the first read operation.
// On transient failures (or premature stream truncation), the reader re-opens
// `GetRange` from the current byte offset (`pos()`). The retry budget applies
// per chunk and resets whenever forward progress is made.
//
// If the server does not implement `GetRange` (`kUnimplemented`) and the reader
// is at byte offset 0, it transparently falls back to `Get` without consuming a
// retry attempt.
//
// Compatible with all Riegeli utilities (`riegeli::ReadAll`,
// `riegeli::CopyAll`, `riegeli::ReadLine`, `riegeli::ReaderIStream`).
::absl::StatusOr<std::unique_ptr<::riegeli::Reader>>
ContentAddressableStorageGetResumableReader(
    std::string_view object_id, ResumableDownloadOptions options = {});

// Retrieves data from the content-addressable storage (CAS) by its object ID.
// Any errors returned from the CAS service are passed through, noticeably
// `::absl::StatusCode::kNotFound` if the object is not present in the CAS or
// `::absl::StatusCode::kInvalidArgument` if the object ID is invalid.
// The `sink` may not be concurrently used by other threads. Data is appended
// to the `sink` in chunks which size is determined by the CAS service.
::absl::Status ContentAddressableStorageGet(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    std::string_view object_id, ::riegeli::Writer* writer);

// Retrieves data from the content-addressable storage (CAS) by its object ID.
// Any errors returned from the CAS service are passed through, noticeably
// `::absl::StatusCode::kNotFound` if the object is not present in the CAS or
// `::absl::StatusCode::kInvalidArgument` if the object ID is invalid.
::absl::StatusOr<std::string> ContentAddressableStorageGet(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    std::string_view object_id);

// Uploads data to the content-addressable storage. Returns its object ID on
// success. This function is safe to call if the same data is already in CAS.
// The `source` may not be concurrently used by other threads.
::absl::StatusOr<std::string> ContentAddressableStorageCreate(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    ::riegeli::Reader* reader,
    std::size_t chunk_size = kContentAddressableStorageDefaultUploadChunkSize);

// Uploads data to the content-addressable storage. Returns its object ID on
// success. This function is safe to call if the same data is already in CAS.
::absl::StatusOr<std::string> ContentAddressableStorageCreate(
    ::grpc::ClientContext* context,
    ::intrinsic_proto::content_addressable_storage::v1::
        ContentAddressableStorageService::StubInterface* cas_stub,
    ::absl::string_view content,
    std::size_t chunk_size = kContentAddressableStorageDefaultUploadChunkSize);

}  // namespace intrinsic

#endif  // INTRINSIC_STORAGE_CONTENT_ADDRESSABLE_STORAGE_CLIENT_HELPERS_H_
