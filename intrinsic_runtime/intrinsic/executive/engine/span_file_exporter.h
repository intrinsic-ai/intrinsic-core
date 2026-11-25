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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_SPAN_FILE_EXPORTER_H_
#define INTRINSIC_EXECUTIVE_ENGINE_SPAN_FILE_EXPORTER_H_

#include <string>
#include <string_view>

namespace intrinsic::executive {

// SpanFileExporter registers a custom OpenCensus exporter that writes spans
// to local Riegeli files when enabled.
//
// This class should only ever be used for executive internal purposes, e.g.,
// testing, debugging, benchmarking.
// Public traces of the executive are provided by the
// intrinsic::OpenCensusPlugin and should be retrieved by that (e.g. through the
// OTel collector).
class SpanFileExporter {
 public:
  // Enables exporting. Spans will be written to files in the output directory.
  // If the output directory does not exist, errors will be logged
  // (rate-limited) and spans will be discarded.
  static void Enable();

  // Disables exporting. Incoming spans will be discarded.
  static void Disable();

  // Returns true if exporting is currently enabled.
  static bool IsEnabled();

  // Forces an immediate, synchronous flush of all queued OpenCensus spans.
  // Useful to ensure database completeness before querying spans.
  static void ForceFlush();

  // Overrides the output directory. Primarily useful for testing.
  // Default directory is "/debug-logs/traces".
  static void SetOutputDirectoryForTesting(std::string_view dir);

  // Returns the current output directory.
  static std::string GetOutputDirectory();

 private:
  SpanFileExporter() = delete;
};

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_ENGINE_SPAN_FILE_EXPORTER_H_
