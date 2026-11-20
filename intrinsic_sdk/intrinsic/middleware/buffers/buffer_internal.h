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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_INTERNAL_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_INTERNAL_H_

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace intrinsic {

class ImmutableBuffer;
class MutableBuffer;

namespace buffer_internal {

inline constexpr size_t kBufferAlignment = 256;

// Refcounted polymorphic backing-storage control block shared by
// `MutableBuffer` (`ref_count_ == 1` while uniquely owned) and
// `ImmutableBuffer` (shared via atomic `Retain()` / `Release()`). Concrete
// backing subclasses (such as the host heap control block or future Zenoh
// shared-memory and adopted-buffer control blocks) override the virtual
// destructor so `Release()` frees backing-specific resources without
// reallocating a control block on freeze or exposing backend headers here.
struct BufferControlBlock {
  BufferControlBlock(const BufferControlBlock&) = delete;
  BufferControlBlock& operator=(const BufferControlBlock&) = delete;
  virtual ~BufferControlBlock() = default;

  void Retain() noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }

  void Release() noexcept {
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete this;
    }
  }

 protected:
  constexpr BufferControlBlock() noexcept = default;

 private:
  std::atomic<size_t> ref_count_{1};
};

struct PrivateAccessor {
  // Constructs a `MutableBuffer` owning `impl` and viewing `[data, data +
  // size)`.
  // REQUIRES: `impl != nullptr`, `data != nullptr`, and `data` is aligned to at
  // least `MutableBuffer::kAlignment`.
  static MutableBuffer CreateMutableBuffer(uint8_t* data, size_t size,
                                           BufferControlBlock* impl,
                                           bool is_shm) noexcept;

  // Transfers ownership of a valid `MutableBuffer`'s control block and view
  // metadata into a new `ImmutableBuffer` in O(1) without heap allocation,
  // resetting `buffer` to the empty, invalid state.
  // REQUIRES: `buffer.valid() == true`.
  static ImmutableBuffer Freeze(MutableBuffer&& buffer) noexcept;
};

}  // namespace buffer_internal
}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_INTERNAL_H_
