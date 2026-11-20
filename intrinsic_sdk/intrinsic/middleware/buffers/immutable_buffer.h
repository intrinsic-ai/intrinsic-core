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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_IMMUTABLE_BUFFER_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_IMMUTABLE_BUFFER_H_

#include <cstddef>
#include <cstdint>

namespace intrinsic {

namespace buffer_internal {
struct BufferControlBlock;
struct PrivateAccessor;
}  // namespace buffer_internal

// Atomic-refcounted handle to a read-only, CPU-accessible byte buffer backed
// by Zenoh shared memory or an aligned heap buffer.
//
// Immutability and alignment:
//   Payload bytes are read-only through this API. When `valid()` is true,
//   `data()` is non-null (even when `size() == 0`) and contiguous. Root
//   buffers are aligned to `kAlignment` (256 bytes) for CPU SIMD and DLPack
//   views; sub-buffers created with `Slice()` preserve `kAlignment` when the
//   source buffer is `kAlignment`-aligned and the slice offset is a multiple
//   of `kAlignment`, and report their effective alignment via `alignment()`.
//   When `valid()` is false, `data()` is `nullptr`.
//
// Lifetime, copy, and move semantics:
//   Copying increments an atomic refcount in O(1) without copying bytes;
//   moving transfers ownership and resets the moved-from instance to the
//   empty, invalid state (`valid() == false`, `data() == nullptr`,
//   `size() == 0`, `is_shm() == false`). Destruction does not require the
//   Python GIL and is safe after the middleware session or subscription is
//   destroyed.
//
// Thread safety:
//   Individual `ImmutableBuffer` instances are thread-compatible (concurrent
//   non-const operations such as assignment on the same variable require
//   external synchronization). Separate `ImmutableBuffer` instances sharing the
//   same underlying buffer may be copied, read, and destroyed concurrently from
//   any thread without synchronization.
class ImmutableBuffer {
 public:
  using value_type = uint8_t;
  using iterator = const uint8_t*;
  using const_iterator = const uint8_t*;

  // Guaranteed byte alignment (256 bytes) for any valid root buffer's `data()`
  // pointer, matching `MutableBuffer::kAlignment`.
  static constexpr size_t kAlignment = 256;

  // Constructs an empty, invalid buffer (`valid() == false`).
  constexpr ImmutableBuffer() noexcept = default;
  ImmutableBuffer(const ImmutableBuffer& other) noexcept;
  ImmutableBuffer& operator=(const ImmutableBuffer& other) noexcept;
  ImmutableBuffer(ImmutableBuffer&& other) noexcept;
  ImmutableBuffer& operator=(ImmutableBuffer&& other) noexcept;
  ~ImmutableBuffer();

  // Returns a zero-copy sub-buffer viewing `[data() + offset, data() + offset +
  // length)` that shares ownership of the underlying allocation with `*this`.
  // Returns an empty, invalid `ImmutableBuffer` (`valid() == false`) if
  // `valid() == false`, `offset > size()`, or `length > size() - offset`.
  // If `*this` is aligned to `kAlignment` and `offset` is a multiple of
  // `kAlignment`, the returned buffer's `alignment()` remains `kAlignment`;
  // otherwise `alignment()` reflects the alignment of `data() + offset`
  // (capped at `kAlignment`).
  [[nodiscard]] ImmutableBuffer Slice(size_t offset,
                                      size_t length) const noexcept;

  // Pointer to immutable payload bytes when `valid() == true`, or `nullptr`
  // when `valid() == false`. Guaranteed to be non-null and contiguous whenever
  // `valid() == true` (including zero-length valid buffers, where
  // `[data(), data() + 0)` may not be dereferenced), with byte alignment given
  // by `alignment()`.
  [[nodiscard]] const uint8_t* data() const noexcept { return data_; }

  // Byte alignment of `data()`, capped at `kAlignment` (256 bytes), or `0` when
  // `valid() == false`. Root buffers always return `kAlignment`; buffers
  // created via `Slice()` return the largest power of two (up to `kAlignment`)
  // that divides the address `data()`.
  [[nodiscard]] size_t alignment() const noexcept;

  // Contiguous range iterators over `[data(), data() + size())`, enabling
  // implicit conversion to `std::span<const uint8_t>` and use with `<ranges>`
  // algorithms.
  [[nodiscard]] const uint8_t* begin() const noexcept { return data_; }
  [[nodiscard]] const uint8_t* end() const noexcept {
    return data_ ? data_ + size_ : nullptr;
  }
  [[nodiscard]] const uint8_t* cbegin() const noexcept { return data_; }
  [[nodiscard]] const uint8_t* cend() const noexcept {
    return data_ ? data_ + size_ : nullptr;
  }

  // Size of the payload in bytes (`0` when `valid() == false`).
  [[nodiscard]] size_t size() const noexcept { return size_; }

  // Returns true when `size() == 0` (including when `valid() == false`).
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  // Returns true if the buffer is backed by shared memory rather than a heap
  // buffer (`false` when `valid() == false`).
  [[nodiscard]] bool is_shm() const noexcept { return is_shm_; }

  // Returns true if this instance references an allocated buffer handle. Named
  // explicitly rather than `operator bool()` so checks are unambiguous relative
  // to `empty()`.
  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(impl_); }

 private:
  friend struct buffer_internal::PrivateAccessor;

  ImmutableBuffer(const uint8_t* data, size_t size,
                  buffer_internal::BufferControlBlock* impl,
                  bool is_shm) noexcept;

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  // Opaque backing state defined in `buffer_internal.h`. In addition to
  // managing CPU-accessible Zenoh SHM and heap allocations, the control block
  // serves as the extension point for future backing types (such as GPU or
  // other device memory handles) without changing the outer class layout.
  buffer_internal::BufferControlBlock* impl_ = nullptr;
  bool is_shm_ = false;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_IMMUTABLE_BUFFER_H_
