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

#ifndef BAZEL_CPP_HEAP_PROFILER_H_
#define BAZEL_CPP_HEAP_PROFILER_H_

#include <string>

#include "absl/status/status.h"

namespace intrinsic {

// Writes a snapshot of the live heap, as sampled by tcmalloc, to `path` as a
// gzipped pprof proto. View it with `pprof -http=: <binary> <path>`.
//
// Linking the :heap_profiler target also writes a profile automatically at
// process exit if the HEAPPROFILE environment variable is set.
absl::Status WriteHeapProfile(const std::string& path);

}  // namespace intrinsic

#endif  // BAZEL_CPP_HEAP_PROFILER_H_
