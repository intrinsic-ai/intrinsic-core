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
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

#include "absl/functional/any_invocable.h"
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

namespace {

// Shared 256-byte-aligned dummy byte used as the non-null `data()` address for
// valid zero-length (`size() == 0`) adopted buffers.
alignas(ImmutableBuffer::kAlignment) constexpr uint8_t kEmptyAdoptedByte = 0;

class AdoptedImmutableControlBlock final
    : public buffer_internal::BufferControlBlock {
 public:
  AdoptedImmutableControlBlock() noexcept = default;

  ~AdoptedImmutableControlBlock() override {
    if (deleter_) {
      std::move(deleter_)();
    }
  }

  void SetDeleter(absl::AnyInvocable<void() &&> deleter) noexcept {
    deleter_ = std::move(deleter);
  }

  // Adopted `const void*` storage may reside in read-only memory or be backed
  // by a caller container that expects immutable access; never allow
  // `TryUnfreeze()` to `const_cast` it into a writable `MutableBuffer`.
  [[nodiscard]] bool IsWritable() const noexcept override { return false; }

 private:
  absl::AnyInvocable<void() &&> deleter_;
};

}  // namespace

absl::StatusOr<ImmutableBuffer> AdoptImmutableBuffer(
    const void* data, size_t size, absl::AnyInvocable<void() &&> deleter) {
  if (size > 0 && data == nullptr) {
    if (deleter) {
      std::move(deleter)();
    }
    return absl::InvalidArgumentError(
        "Cannot adopt non-empty ImmutableBuffer from null data pointer");
  }
  auto* impl = new (std::nothrow) AdoptedImmutableControlBlock();
  if (impl == nullptr) {
    if (deleter) {
      std::move(deleter)();
    }
    return absl::ResourceExhaustedError(
        "Failed to allocate ImmutableBuffer control block");
  }
  impl->SetDeleter(std::move(deleter));
  const uint8_t* const ptr =
      (size > 0) ? static_cast<const uint8_t*>(data) : &kEmptyAdoptedByte;
  return buffer_internal::PrivateAccessor::CreateAdoptedImmutableBuffer(
      ptr, size, impl);
}

}  // namespace intrinsic
