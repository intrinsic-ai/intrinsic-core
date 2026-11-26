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

#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_internal.h"

namespace intrinsic {

static_assert(MutableBuffer::kAlignment == buffer_internal::kBufferAlignment,
              "MutableBuffer::kAlignment must match kBufferAlignment.");

MutableBuffer::MutableBuffer(MutableBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      impl_(std::exchange(other.impl_, nullptr)),
      is_shm_(std::exchange(other.is_shm_, false)) {}

MutableBuffer& MutableBuffer::operator=(MutableBuffer&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  buffer_internal::BufferControlBlock* const old_impl = impl_;
  data_ = std::exchange(other.data_, nullptr);
  size_ = std::exchange(other.size_, 0);
  impl_ = std::exchange(other.impl_, nullptr);
  is_shm_ = std::exchange(other.is_shm_, false);
  if (old_impl != nullptr) {
    old_impl->Release();
  }
  return *this;
}

MutableBuffer::~MutableBuffer() {
  if (impl_ != nullptr) {
    impl_->Release();
  }
}

MutableBuffer::MutableBuffer(uint8_t* data, size_t size,
                             buffer_internal::BufferControlBlock* impl,
                             bool is_shm) noexcept
    : data_(data), size_(size), impl_(impl), is_shm_(is_shm) {}

buffer_internal::BufferControlBlock* MutableBuffer::ReleaseImpl() noexcept {
  data_ = nullptr;
  size_ = 0;
  is_shm_ = false;
  return std::exchange(impl_, nullptr);
}

namespace buffer_internal {

MutableBuffer PrivateAccessor::CreateMutableBuffer(uint8_t* data, size_t size,
                                                   BufferControlBlock* impl,
                                                   bool is_shm) noexcept {
  return MutableBuffer(data, size, impl, is_shm);
}

}  // namespace buffer_internal

}  // namespace intrinsic
