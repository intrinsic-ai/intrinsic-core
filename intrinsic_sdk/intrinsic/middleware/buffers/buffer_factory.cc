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

#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_factory.h"

#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/allocators/buffer_allocator.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_internal.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::StatusOr<MutableBuffer> AllocateMutableBuffer(
    size_t size, const BufferAllocator& allocator) {
  return allocator.Allocate(size);
}

absl::StatusOr<ImmutableBuffer> CopyToImmutableBuffer(
    const void* data, size_t size, const BufferAllocator& allocator) {
  if (size > 0 && data == nullptr) {
    return absl::InvalidArgumentError(
        "Cannot copy non-empty ImmutableBuffer from null data pointer");
  }
  INTR_ASSIGN_OR_RETURN(MutableBuffer buffer,
                        AllocateMutableBuffer(size, allocator));
  if (size > 0) {
    std::memcpy(buffer.data(), data, size);
  }
  return FreezeMutableBuffer(std::move(buffer));
}

absl::StatusOr<ImmutableBuffer> FreezeMutableBuffer(MutableBuffer&& buffer) {
  if (!buffer.valid()) {
    return absl::InvalidArgumentError("Cannot freeze an invalid MutableBuffer");
  }
  return buffer_internal::PrivateAccessor::Freeze(std::move(buffer));
}

absl::StatusOr<MutableBuffer> UnfreezeOrCopy(ImmutableBuffer&& buffer,
                                             const BufferAllocator& allocator) {
  if (!buffer.valid()) {
    return absl::InvalidArgumentError(
        "Cannot unfreeze or copy an invalid ImmutableBuffer");
  }
  if (std::optional<MutableBuffer> unfrozen =
          buffer_internal::PrivateAccessor::TryUnfreeze(buffer);
      unfrozen.has_value()) {
    return *std::move(unfrozen);
  }
  INTR_ASSIGN_OR_RETURN(MutableBuffer copy,
                        AllocateMutableBuffer(buffer.size(), allocator));
  if (buffer.size() > 0) {
    std::memcpy(copy.data(), buffer.data(), buffer.size());
  }
  buffer = ImmutableBuffer();
  return copy;
}

}  // namespace intrinsic
