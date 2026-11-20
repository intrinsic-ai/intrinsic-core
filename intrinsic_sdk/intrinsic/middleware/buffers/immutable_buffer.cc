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

#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_internal.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"

namespace intrinsic {

static_assert(MutableBuffer::kAlignment == ImmutableBuffer::kAlignment,
              "MutableBuffer and ImmutableBuffer must share kAlignment.");

ImmutableBuffer::ImmutableBuffer(const ImmutableBuffer& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      impl_(other.impl_),
      is_shm_(other.is_shm_) {
  if (impl_ != nullptr) {
    impl_->Retain();
  }
}

ImmutableBuffer& ImmutableBuffer::operator=(
    const ImmutableBuffer& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (other.impl_ != nullptr) {
    other.impl_->Retain();
  }
  if (impl_ != nullptr) {
    impl_->Release();
  }
  data_ = other.data_;
  size_ = other.size_;
  impl_ = other.impl_;
  is_shm_ = other.is_shm_;
  return *this;
}

ImmutableBuffer::ImmutableBuffer(ImmutableBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      impl_(std::exchange(other.impl_, nullptr)),
      is_shm_(std::exchange(other.is_shm_, false)) {}

ImmutableBuffer& ImmutableBuffer::operator=(ImmutableBuffer&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (impl_ != nullptr) {
    impl_->Release();
  }
  data_ = std::exchange(other.data_, nullptr);
  size_ = std::exchange(other.size_, 0);
  impl_ = std::exchange(other.impl_, nullptr);
  is_shm_ = std::exchange(other.is_shm_, false);
  return *this;
}

ImmutableBuffer::~ImmutableBuffer() {
  if (impl_ != nullptr) {
    impl_->Release();
  }
}

ImmutableBuffer ImmutableBuffer::Slice(size_t offset,
                                       size_t length) const noexcept {
  if (!valid() || offset > size_ || length > size_ - offset) {
    return ImmutableBuffer();
  }
  ImmutableBuffer sliced(*this);
  sliced.data_ = data_ + offset;
  sliced.size_ = length;
  return sliced;
}

size_t ImmutableBuffer::alignment() const noexcept {
  if (data_ == nullptr) {
    return 0;
  }
  const uintptr_t addr_or_max = reinterpret_cast<uintptr_t>(data_) | kAlignment;
  return static_cast<size_t>(addr_or_max & (~addr_or_max + 1));
}

ImmutableBuffer::ImmutableBuffer(const uint8_t* data, size_t size,
                                 buffer_internal::BufferControlBlock* impl,
                                 bool is_shm) noexcept
    : data_(data), size_(size), impl_(impl), is_shm_(is_shm) {}

namespace buffer_internal {

ImmutableBuffer PrivateAccessor::Freeze(MutableBuffer&& buffer) noexcept {
  const uint8_t* const data = buffer.data_;
  const size_t size = buffer.size_;
  const bool is_shm = buffer.is_shm_;
  BufferControlBlock* const impl = buffer.ReleaseImpl();
  return ImmutableBuffer(data, size, impl, is_shm);
}

}  // namespace buffer_internal

}  // namespace intrinsic
