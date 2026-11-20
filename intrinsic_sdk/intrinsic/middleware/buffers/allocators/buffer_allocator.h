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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_BUFFER_ALLOCATOR_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_BUFFER_ALLOCATOR_H_

#include <cstddef>

#include "absl/status/statusor.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

namespace intrinsic {

// Closed base class for backing-storage allocators that produce writable
// `MutableBuffer` instances.
//
// Only in-tree allocator types declared as friends of `BufferAllocator` may
// inherit from it so the middleware controls all supported buffer lifecycles
// and `BufferControlBlock` representations.
//
// Thread safety:
//   All `BufferAllocator` implementations must be thread-safe (`Allocate` may
//   be called concurrently from multiple threads without external
//   synchronization).
class BufferAllocator {
 public:
  BufferAllocator(const BufferAllocator&) = delete;
  BufferAllocator& operator=(const BufferAllocator&) = delete;

  // Allocates a writable `MutableBuffer::kAlignment`-aligned (256-byte) buffer
  // of `size` uninitialized bytes. Returns
  // `absl::StatusCode::kResourceExhausted` if allocation fails.
  [[nodiscard]] virtual absl::StatusOr<MutableBuffer> Allocate(
      size_t size) const = 0;

 protected:
  ~BufferAllocator() = default;

 private:
  friend class HeapBufferAllocator;

  constexpr BufferAllocator() noexcept = default;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_BUFFER_ALLOCATOR_H_
