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

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace intrinsic {
namespace {

// Memory kept alive until process exit so that the heap profile written by
// :heap_profiler (when HEAPPROFILE is set) has something in it.
std::vector<std::unique_ptr<char[]>>& LiveAllocations() {
  static auto* allocations = new std::vector<std::unique_ptr<char[]>>();
  return *allocations;
}

TEST(Gperftools, CanRun) {
  // Allocate well above tcmalloc's default ~2 MiB sampling interval so the
  // heap profile reliably contains samples.
  for (int i = 0; i < 16; ++i) {
    LiveAllocations().push_back(std::make_unique<char[]>(4 << 20));
  }
}

}  // namespace
}  // namespace intrinsic
