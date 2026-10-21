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

#ifndef INTRINSIC_UTIL_PAGE_FAULT_INFO_H_
#define INTRINSIC_UTIL_PAGE_FAULT_INFO_H_

#include <cstdint>

namespace intrinsic {

// PagefaultInfo contains information about the number of pagefaults occurring
// during the runtime of the current thread.
struct PagefaultInfo {
  // Absolute number of page faults encountered during the lifetime of this
  // thread.
  uint64_t major_faults = 0;
  uint64_t minor_faults = 0;
  // Relative number of new page faults encountered since the previous call to
  // `PageFaultInfo`.
  uint64_t delta_major_faults = 0;
  uint64_t delta_minor_faults = 0;

  bool operator==(const PagefaultInfo& other) const {
    return this->major_faults == other.major_faults &&
           this->minor_faults == other.minor_faults &&
           this->delta_major_faults == other.delta_major_faults &&
           this->delta_minor_faults == other.delta_minor_faults;
  }
  bool operator!=(const PagefaultInfo& other) const {
    return !(*this == other);
  }
};

// Returns a PagefaultInfo struct indicating the pagefault count of the
// current thread.
PagefaultInfo GetPagefaultInfo();

}  // namespace intrinsic
#endif  // INTRINSIC_UTIL_PAGE_FAULT_INFO_H_
