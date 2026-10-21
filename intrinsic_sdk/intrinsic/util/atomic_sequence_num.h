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

#ifndef INTRINSIC_UTIL_ATOMIC_SEQUENCE_NUM_H_
#define INTRINSIC_UTIL_ATOMIC_SEQUENCE_NUM_H_
//
// Atomic operation to generate unique sequence numbers from a counter.
//
//     SequenceNumber<intptr_t> x;
//      ...
//     intptr_t y = x.GetNext();     // get the next sequence number
//
// This can also be used with user-defined strong integer types:
//
//     DEFINE_STRONG_INT_TYPE(MyIntId64, int64);
//     SequenceNumber<MyIntId64> sequence;
//      ...
//     MyIntId64 y = sequence.GetNext();     // get the next sequence number
//
// If your code already uses a Mutex, you may find it faster to protect a
// simple counter with that Mutex.
#include <atomic>
#include <type_traits>

#include "ortools/base/strong_int.h"

namespace intrinsic {

template <typename T, class IsIntIdCheck = void>
class SequenceNumber;

template <typename ValueT>
class SequenceNumber<
    ValueT,
    typename std::enable_if<!util_intops::IsStrongInt<ValueT>::value>::type> {
 public:
  constexpr SequenceNumber() : word_(0) {}

  SequenceNumber(const SequenceNumber&) = delete;
  SequenceNumber& operator=(const SequenceNumber&) = delete;

  ~SequenceNumber() = default;

  using Value = ValueT;

  // Return the integer one greater than was returned by the previous call on
  // this instance, or 0 if there have been no such calls.
  // Provided overflow does not occur, no two calls on the same instance will
  // return the same value, even in the face of concurrency.
  Value GetNext() {
    // As always, clients may not assume properties implied by the
    // implementation, which may change.
    return word_.fetch_add(1, std::memory_order_relaxed);
  }

  // SequenceNumber is implemented as a class specifically to stop clients
  // from reading the value of word_ without also incrementing it.
  // Please do not add such a call.

 private:
  std::atomic<Value> word_;
};

// Specialization for strong ints.
template <typename ValueT>
class SequenceNumber<
    ValueT,
    typename std::enable_if<util_intops::IsStrongInt<ValueT>::value>::type> {
 public:
  constexpr SequenceNumber() : word_(0) {}

  SequenceNumber(const SequenceNumber&) = delete;
  SequenceNumber& operator=(const SequenceNumber&) = delete;

  ~SequenceNumber() = default;

  using Value = ValueT;

  Value GetNext() {
    return ValueT(word_.fetch_add(1, std::memory_order_relaxed));
  }

 private:
  std::atomic<typename Value::ValueType> word_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_ATOMIC_SEQUENCE_NUM_H_
