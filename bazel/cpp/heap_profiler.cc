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

#include "heap_profiler.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "tcmalloc/malloc_extension.h"
#include "tcmalloc/profile_marshaler.h"

namespace intrinsic {

absl::Status WriteHeapProfile(const std::string& path) {
  tcmalloc::Profile profile =
      tcmalloc::MallocExtension::SnapshotCurrent(tcmalloc::ProfileType::kHeap);
  absl::StatusOr<std::string> data = tcmalloc::Marshal(profile);
  if (!data.ok()) return data.status();

  std::ofstream out(path, std::ios::binary);
  if (!out) return absl::InternalError(absl::StrCat("Failed to open ", path));
  out << *data;
  if (!out) return absl::InternalError(absl::StrCat("Failed to write ", path));
  return absl::OkStatus();
}

namespace {

void WriteHeapProfileAtExit() {
  const char* path = std::getenv("HEAPPROFILE");
  if (path == nullptr || *path == '\0') return;
  if (absl::Status status = WriteHeapProfile(path); !status.ok()) {
    std::cerr << "HEAPPROFILE: " << status << std::endl;
  } else {
    std::cerr << "HEAPPROFILE: wrote " << path << std::endl;
  }
}

// Registered during static initialization; requires alwayslink = True on the
// library so the linker does not drop this translation unit.
[[maybe_unused]] const bool kRegistered = [] {
  std::atexit(WriteHeapProfileAtExit);
  return true;
}();

}  // namespace
}  // namespace intrinsic
