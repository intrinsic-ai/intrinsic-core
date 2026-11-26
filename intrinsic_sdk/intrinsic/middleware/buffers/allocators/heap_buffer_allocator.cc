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

#include "intrinsic_sdk/intrinsic/middleware/buffers/allocators/heap_buffer_allocator.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_internal.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

namespace intrinsic {
namespace {

using ::intrinsic::buffer_internal::BufferControlBlock;
using ::intrinsic::buffer_internal::kBufferAlignment;
using ::intrinsic::buffer_internal::PrivateAccessor;

struct AlignedDeleter {
  void operator()(uint8_t* ptr) const noexcept {
    ::operator delete[](ptr, std::align_val_t{kBufferAlignment});
  }
};

using AlignedBytePtr = std::unique_ptr<uint8_t[], AlignedDeleter>;

class HeapBufferControlBlock final : public BufferControlBlock {
 public:
  explicit HeapBufferControlBlock(AlignedBytePtr storage) noexcept
      : storage_(std::move(storage)) {}

  [[nodiscard]] bool IsWritable() const noexcept override { return true; }

 private:
  AlignedBytePtr storage_;
};

// Shared 256-byte-aligned dummy byte used as the non-null `data()` address for
// valid zero-length (`size() == 0`) buffers without allocating heap storage.
alignas(MutableBuffer::kAlignment) uint8_t kEmptyAlignedByte = 0;

// Allocates a `MutableBuffer::kAlignment`-aligned (256-byte) heap buffer of
// `size` bytes. Returns `nullptr` if allocation fails.
// REQUIRES: `size > 0`.
[[nodiscard]] AlignedBytePtr AllocateAlignedBytes(size_t size) noexcept {
  auto* raw = static_cast<uint8_t*>(::operator new[](
      size, std::align_val_t{MutableBuffer::kAlignment}, std::nothrow));
  return AlignedBytePtr(raw);
}

}  // namespace

const HeapBufferAllocator& HeapBufferAllocator::Instance() noexcept {
  static const HeapBufferAllocator kInstance;
  return kInstance;
}

absl::StatusOr<MutableBuffer> HeapBufferAllocator::Allocate(size_t size) const {
  AlignedBytePtr storage;
  uint8_t* data = &kEmptyAlignedByte;
  if (size > 0) {
    storage = AllocateAlignedBytes(size);
    if (!storage) {
      return absl::ResourceExhaustedError(
          "Failed to allocate aligned heap buffer");
    }
    data = storage.get();
  }
  auto* impl = new (std::nothrow) HeapBufferControlBlock(std::move(storage));
  if (impl == nullptr) {
    return absl::ResourceExhaustedError(
        "Failed to allocate MutableBuffer control block");
  }
  return PrivateAccessor::CreateMutableBuffer(data, size, impl,
                                              /*is_shm=*/false);
}

}  // namespace intrinsic
