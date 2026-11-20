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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_HEAP_BUFFER_ALLOCATOR_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_HEAP_BUFFER_ALLOCATOR_H_

#include <cstddef>

#include "absl/status/statusor.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/allocators/buffer_allocator.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

namespace intrinsic {

// Stateless, thread-safe allocator that allocates 256-byte-aligned host heap
// buffers (`is_shm() == false`).
class HeapBufferAllocator final : public BufferAllocator {
 public:
  // Returns the process-wide default `HeapBufferAllocator` instance.
  [[nodiscard]] static const HeapBufferAllocator& Instance() noexcept;

  constexpr HeapBufferAllocator() noexcept = default;

  [[nodiscard]] absl::StatusOr<MutableBuffer> Allocate(
      size_t size) const override;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_ALLOCATORS_HEAP_BUFFER_ALLOCATOR_H_
