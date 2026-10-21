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

#include "intrinsic/util/page_fault_info.h"

#include <sys/mman.h>
#include <sys/resource.h>

namespace intrinsic {

PagefaultInfo GetPagefaultInfo() {
  static PagefaultInfo info;

  rusage usage;
  getrusage(RUSAGE_THREAD, &usage);

  info.delta_major_faults = usage.ru_majflt - info.major_faults;
  info.delta_minor_faults = usage.ru_minflt - info.minor_faults;
  info.major_faults = usage.ru_majflt;
  info.minor_faults = usage.ru_minflt;

  return info;
}

}  // namespace intrinsic
