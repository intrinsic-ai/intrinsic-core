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

#ifndef INTRINSIC_UTIL_MEMORY_LOCK_H_
#define INTRINSIC_UTIL_MEMORY_LOCK_H_

#include <malloc.h>
#include <sys/mman.h>

#include "absl/status/status.h"

namespace intrinsic {

// Locks and prefaults a specified amount of stack and heap memory.
// Returns absl::InternalError if the system is unable to lock memory to RAM.
// This is most likely due to unavailable capabilities.
template <int STACK_SIZE, int HEAP_SIZE>
inline absl::Status LockMemory() {
  // Configure malloc to use only heap memory.
  // By setting the maximum number of allocations via mmap to zero effectively
  // disables this feature and solely relies on heap allocations. We want to
  // avoid using mmap’d memory, since ranges aren’t reused after free and thus
  // would disable the memory locked heap allocations below.
  mallopt(M_MMAP_MAX, 0);
  // Configure malloc to not shrink free heap allocation blocks.
  mallopt(M_TRIM_THRESHOLD, -1);

  if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) {
    return absl::InternalError(
        absl::StrCat("Failed to lock memory: ", strerror(errno)));
  }

  // We reserve stack memory as well as heap memory.
  char* heap_prefault = static_cast<char*>(malloc(HEAP_SIZE));
  if (heap_prefault == nullptr) {
    return absl::InternalError(
        "Unable to allocate heap memory to lock in RAM.");
  }
  // It's sufficient to touch only one page at a time.
  // Each write here will provoke a pagefault, but locks the space in RAM.
  for (int i = 0; i < HEAP_SIZE; i += sysconf(_SC_PAGESIZE)) {
    heap_prefault[i] = i;
  }
  // We can free the heap allocation. Future heap allocations will use this
  // buffer for allocations.
  free(heap_prefault);

  __attribute__((unused)) uint8_t stack_prefault[STACK_SIZE];
  for (int i = 0; i < STACK_SIZE; i += sysconf(_SC_PAGESIZE)) {
    stack_prefault[i] = i;
  }

  return absl::OkStatus();
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_MEMORY_LOCK_H_
