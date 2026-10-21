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

#ifndef INTRINSIC_UTIL_FIXED_VECTOR_H_
#define INTRINSIC_UTIL_FIXED_VECTOR_H_

#include <cstddef>
#include <type_traits>

#include "absl/container/inlined_vector.h"  // IWYU pragma: export
#include "absl/log/log.h"

namespace intrinsic {

namespace fixed_vector_details {
// As the name implies, this allocator does nothing when called, and should
// never be called as doing so is an error and will assert.
// This is intended to be used from a "fixed size" container that gets its
// memory from somewhere else besides the allocator (e.g. in place, inlined).
template <typename T>
class NoopAllocator {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using is_always_equal = std::true_type;
  using propagate_on_container_swap = std::false_type;
  using propagate_on_container_move_assignment = std::false_type;
  using propagate_on_container_copy_assignment = std::false_type;

  T* allocate(size_type) {
    LOG(FATAL) << "[FixedVector] Attempting to allocate from NoopAllocator";
    return nullptr;
  }

  void deallocate(T*, size_type) {
    LOG(FATAL) << "[FixedVector] Attempting to deallocate from NoopAllocator";
  }
};

}  // namespace fixed_vector_details

// A vector type with a fixed maximum capacity of 'N' elements, containing
// elements of type 'T'.
// It is safe to use in realtime contexts because it allocates on the stack.
// It provides all well known functions similar to std::vector.
// Unlike std::vector, it cannot reallocate; when the capacity is exceeded,
// for instance when too many elements are inserted, it fails a runtime assert.
// So, callers must do checks before any operation that can increase the size.
//
// You should not pass this type by constant reference, because this would limit
// functions to a fixed maximum size 'N'. In almost all cases, it is better to
// pass a 'absl::Span<const T>' instead, this will accept any maximum size.
//
//   // BAD: Library function takes const reference of FixedVector.
//   double Sum(const FixedVector<double, 10>& v);
//
//   // GOOD: Library function takes absl::Span.
//   double Sum(absl::Span<const double> v);
//
// If a function modifies a vector only without changing its size, you can use
// mutable 'absl::Span<T>' as an argument.
//
//   // GOOD: Library function writes mutable absl::Span without changing size.
//   void DoubleInPlace(absl::Span<double> v);
template <typename T, size_t N>
using FixedVector =
    absl::InlinedVector<T, N, fixed_vector_details::NoopAllocator<T>>;
}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_FIXED_VECTOR_H_
