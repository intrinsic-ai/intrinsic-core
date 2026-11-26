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

#include "absl/functional/any_invocable.h"
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

// Converts a valid `ImmutableBuffer` into a writable 256-byte-aligned
// `MutableBuffer`. Transfers ownership in O(1) without copying or allocating
// when `buffer` is uniquely owned (`ref_count == 1`), writable, non-SHM, and
// 256-byte-aligned; otherwise allocates a fresh buffer using `allocator`
// (defaulting to `HeapBufferAllocator::Instance()`) and copies
// `[buffer.data(), buffer.data() + buffer.size())`. Resets `buffer` to the
// empty, invalid state on success. Returns `absl::StatusCode::kInvalidArgument`
// if `!buffer.valid()`, or forwards the allocation error if
// `allocator.Allocate(buffer.size())` fails, leaving `buffer` unmodified on
// error.
//
// Lifetime: `allocator` only needs to remain alive for the duration of this
// call; the returned `MutableBuffer` can safely outlive `allocator`. Reclaiming
// a 256-byte-aligned `Slice` in place keeps the full parent allocation alive
// until the returned `MutableBuffer` (or anything later frozen from it) is
// destroyed.
absl::StatusOr<MutableBuffer> UnfreezeOrCopy(
    ImmutableBuffer&& buffer,
    const BufferAllocator& allocator = HeapBufferAllocator::Instance());
// Disallow temporary allocators (for example a stateful pooled allocator) so
// callers cannot accidentally create and immediately destroy a pool on every
// call, which would silently disable caching.
absl::StatusOr<MutableBuffer> UnfreezeOrCopy(
    ImmutableBuffer&& buffer, const BufferAllocator&& allocator) = delete;

// Adopts caller-owned read-only host storage `[data, data + size)` into a
// non-SHM `ImmutableBuffer` without copying payload bytes. `data` does not need
// to be `ImmutableBuffer::kAlignment`-aligned: when `size > 0`, the returned
// buffer preserves `data` and reports its effective alignment (capped at
// `ImmutableBuffer::kAlignment`) via `alignment()`. When `size == 0`, `data`
// may be `nullptr` and the returned buffer points to a 256-byte-aligned
// non-null static dummy byte.
//
// Ownership and `deleter`:
//   `deleter` (if non-null) is invoked exactly once when the final
//   `ImmutableBuffer` reference (including copies and `Slice()` views) drops,
//   or immediately before returning an error on validation or control-block
//   allocation failure so caller-owned storage never leaks. `deleter` may be
//   invoked from any thread that drops the final reference, must not throw,
//   and must not assume the caller holds the Python GIL or that middleware
//   sessions or subscriptions are still alive (matching `ImmutableBuffer`'s
//   destruction guarantees). Adopted buffers are read-only, so
//   `UnfreezeOrCopy()` always copies into a newly allocated writable
//   `MutableBuffer`.
//
// Caller pitfalls:
//   Because C++ function-argument evaluation order is unspecified, do not pass
//   `container.data()` / `container.size()` in the same call expression that
//   moves `container` into `deleter` (moving first can silently adopt 0 bytes).
//   Bind `data` and `size` to local variables before calling
//   `AdoptImmutableBuffer()`, and only adopt move-stable storage (wrap
//   containers with inline/SSO storage, such as `std::string`, in a
//   heap-allocated holder like `std::unique_ptr`).
//
// Returns `absl::StatusCode::kInvalidArgument` if `size > 0` and
// `data == nullptr`, or `absl::StatusCode::kResourceExhausted` if control-block
// allocation fails.
absl::StatusOr<ImmutableBuffer> AdoptImmutableBuffer(
    const void* data, size_t size, absl::AnyInvocable<void() &&> deleter);

}  // namespace intrinsic

#endif  // INTRINSIC_MIDDLEWARE_BUFFERS_BUFFER_FACTORY_H_
