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
#include <optional>

namespace intrinsic {

class ImmutableBuffer;
class MutableBuffer;

namespace buffer_internal {

inline constexpr size_t kBufferAlignment = 256;

// Refcounted polymorphic backing-storage control block shared by
// `MutableBuffer` (`ref_count_ == 1` while uniquely owned) and
// `ImmutableBuffer` (shared via atomic `Retain()` / `Release()`). Concrete
// backing subclasses (such as the host heap control block, the adopted-buffer
// control block, or future Zenoh shared-memory control blocks) override the
// virtual destructor so `Release()` frees backing-specific resources without
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

  // Returns true if this control block currently has a single owner. Uses
  // `memory_order_acquire` to synchronize with any other thread's prior
  // `Release()` (`memory_order_acq_rel`) before reclaiming mutable ownership.
  [[nodiscard]] bool IsUnique() const noexcept {
    return ref_count_.load(std::memory_order_acquire) == 1;
  }

  // Returns true if the underlying storage is writable and may be reclaimed as
  // a `MutableBuffer` via `PrivateAccessor::TryUnfreeze()`. Pure virtual so
  // every concrete subclass explicitly declares writability (for example, true
  // for heap/pooled allocators and false for read-only adopted `const void*`
  // storage so `TryUnfreeze()` never casts away constness).
  [[nodiscard]] virtual bool IsWritable() const noexcept = 0;

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

  // Transfers ownership of a uniquely-owned, writable, non-SHM,
  // 256-byte-aligned `ImmutableBuffer` back into a writable `MutableBuffer` in
  // O(1) without copying payload bytes. Resets `buffer` to the empty, invalid
  // state on success; returns `std::nullopt` leaving `buffer` unmodified
  // otherwise.
  [[nodiscard]] static std::optional<MutableBuffer> TryUnfreeze(
      ImmutableBuffer& buffer) noexcept;

  // Constructs a non-SHM `ImmutableBuffer` owning `impl` and viewing `[data,
  // data + size)` without requiring `ImmutableBuffer::kAlignment` alignment.
  // REQUIRES: `impl != nullptr` and `data != nullptr`.
  static ImmutableBuffer CreateAdoptedImmutableBuffer(
      const uint8_t* data, size_t size, BufferControlBlock* impl) noexcept;
};

}  // namespace buffer_internal
}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_INTERNAL_H_
