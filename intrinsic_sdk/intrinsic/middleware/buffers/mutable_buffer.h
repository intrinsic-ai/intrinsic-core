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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_MUTABLE_BUFFER_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_MUTABLE_BUFFER_H_

#include <cstddef>
#include <cstdint>

namespace intrinsic {

namespace buffer_internal {
struct BufferControlBlock;
struct PrivateAccessor;
}  // namespace buffer_internal

// Move-only writable, CPU-accessible byte buffer backed by Zenoh shared memory
// or an aligned heap fallback.
//
// Alignment:
//   When `valid()` is true, `data()` is non-null (even when `size() == 0`) and
//   aligned to `kAlignment` (256 bytes) for CPU SIMD and DLPack views. When
//   `valid()` is false, `data()` is `nullptr`. Because 256 bytes is smaller
//   than the OS page size, this alignment is preserved across processes when
//   shared-memory segments are mapped via `mmap`.
//
// Lifetime and move semantics:
//   Moving from a `MutableBuffer` transfers ownership in O(1) without copying
//   payload bytes and resets the moved-from instance to the empty, invalid
//   state (`valid() == false`, `data() == nullptr`, `size() == 0`,
//   `is_shm() == false`). Destroying an unpublished `MutableBuffer` releases
//   its underlying shared-memory chunk or heap allocation; a valid buffer
//   remains safe to destroy even after the middleware session is finalized.
//
// Thread safety:
//   `MutableBuffer` is thread-compatible. Concurrent access to the same
//   `MutableBuffer` instance or its writable payload requires external
//   synchronization.
class MutableBuffer {
 public:
  using value_type = uint8_t;
  using iterator = uint8_t*;
  using const_iterator = const uint8_t*;

  // Guaranteed minimum byte alignment (256 bytes) for any valid buffer's
  // `data()` pointer, covering cache lines, CPU SIMD, and DLPack views.
  static constexpr size_t kAlignment = 256;

  // Constructs an empty, invalid buffer (`valid() == false`).
  constexpr MutableBuffer() noexcept = default;
  MutableBuffer(const MutableBuffer&) = delete;
  MutableBuffer& operator=(const MutableBuffer&) = delete;
  MutableBuffer(MutableBuffer&& other) noexcept;
  MutableBuffer& operator=(MutableBuffer&& other) noexcept;
  ~MutableBuffer();

  // Pointer to writable payload bytes when `valid() == true`, or `nullptr` when
  // `valid() == false`. Guaranteed to be non-null and aligned to at least
  // `kAlignment` bytes whenever `valid() == true` (including zero-length valid
  // buffers, where `[data(), data() + 0)` may not be dereferenced).
  [[nodiscard]] uint8_t* data() noexcept { return data_; }
  [[nodiscard]] const uint8_t* data() const noexcept { return data_; }

  // Byte alignment of `data()` (`kAlignment` when `valid() == true`, or `0`
  // when `valid() == false`).
  [[nodiscard]] size_t alignment() const noexcept {
    return data_ ? kAlignment : 0;
  }

  // Contiguous range iterators over `[data(), data() + size())`, enabling
  // implicit conversion to `std::span` and use with `<ranges>` algorithms.
  [[nodiscard]] uint8_t* begin() noexcept { return data_; }
  [[nodiscard]] uint8_t* end() noexcept {
    return data_ ? data_ + size_ : nullptr;
  }
  [[nodiscard]] const uint8_t* begin() const noexcept { return data_; }
  [[nodiscard]] const uint8_t* end() const noexcept {
    return data_ ? data_ + size_ : nullptr;
  }
  [[nodiscard]] const uint8_t* cbegin() const noexcept { return data_; }
  [[nodiscard]] const uint8_t* cend() const noexcept {
    return data_ ? data_ + size_ : nullptr;
  }

  // Size of the writable payload in bytes (`0` when `valid() == false`).
  [[nodiscard]] size_t size() const noexcept { return size_; }

  // Returns true when `size() == 0` (including when `valid() == false`).
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  // Returns true if the buffer is backed by shared memory rather than the heap
  // fallback (`false` when `valid() == false`).
  [[nodiscard]] bool is_shm() const noexcept { return is_shm_; }

  // Returns true if this instance owns an allocated buffer handle. Named
  // explicitly rather than `operator bool()` so checks are unambiguous relative
  // to `empty()`.
  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(impl_); }

 private:
  friend struct buffer_internal::PrivateAccessor;

  MutableBuffer(uint8_t* data, size_t size,
                buffer_internal::BufferControlBlock* impl,
                bool is_shm) noexcept;

  // Transfers ownership of the internal representation out of this buffer,
  // leaving this buffer empty and invalid. Used by the middleware publish
  // implementation so Zenoh can take ownership of the underlying memory
  // without copying or freeing it when MutableBuffer is destroyed.
  [[nodiscard]] buffer_internal::BufferControlBlock* ReleaseImpl() noexcept;

  uint8_t* data_ = nullptr;
  size_t size_ = 0;
  // Opaque backing state defined in `buffer_internal.h`. In addition to
  // managing CPU-accessible Zenoh SHM and heap allocations, the control block
  // serves as the extension point for future backing types (such as GPU or
  // other device memory handles) without changing the outer class layout.
  buffer_internal::BufferControlBlock* impl_ = nullptr;
  bool is_shm_ = false;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_MUTABLE_BUFFER_H_
