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
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "grpcpp/client_context.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.grpc.pb.h"
#include "intrinsic/storage/content_addressable_storage/proto/cas_service.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "riegeli/bytes/reader.h"
#include "riegeli/bytes/string_reader.h"
#include "riegeli/bytes/string_writer.h"
#include "riegeli/bytes/writer.h"

namespace intrinsic {

using ::intrinsic_proto::content_addressable_storage::v1::CreateRequest;
using ::intrinsic_proto::content_addressable_storage::v1::CreateResponse;
using ::intrinsic_proto::content_addressable_storage::v1::GetRequest;
using ::intrinsic_proto::content_addressable_storage::v1::GetResponse;

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
