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

#ifndef INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_FACTORY_H_
#define INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_FACTORY_H_

#include <cstddef>

#include "absl/status/statusor.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/allocators/buffer_allocator.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/allocators/heap_buffer_allocator.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

namespace intrinsic {

// Allocates a writable 256-byte-aligned buffer of `size` uninitialized bytes
// using `allocator` (defaulting to `HeapBufferAllocator::Instance()`). Returns
// `absl::StatusCode::kResourceExhausted` if allocation fails.
absl::StatusOr<MutableBuffer> AllocateMutableBuffer(
    size_t size,
    const BufferAllocator& allocator = HeapBufferAllocator::Instance());

// Allocates a read-only 256-byte-aligned buffer of `size` bytes using
// `allocator` (defaulting to `HeapBufferAllocator::Instance()`) and copies
// `[data, data + size)` into it. Returns `absl::StatusCode::kInvalidArgument`
// if `size > 0` and `data == nullptr`, or forwards the allocation error if
// `allocator.Allocate(size)` fails.
absl::StatusOr<ImmutableBuffer> CopyToImmutableBuffer(
    const void* data, size_t size,
    const BufferAllocator& allocator = HeapBufferAllocator::Instance());

// Transfers ownership of a valid `MutableBuffer`'s underlying allocation into
// a new `ImmutableBuffer` in O(1) without copying payload bytes or allocating
// a new control block, resetting `buffer` to the empty, invalid state. Returns
// `absl::StatusCode::kInvalidArgument` if `!buffer.valid()`, leaving `buffer`
// unmodified.
absl::StatusOr<ImmutableBuffer> FreezeMutableBuffer(MutableBuffer&& buffer);

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_FACTORY_H_
