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

#ifndef INTRINSIC_TESTING_UNDECLARED_UNDECLARED_OUTPUTS_H_
#define INTRINSIC_TESTING_UNDECLARED_UNDECLARED_OUTPUTS_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"

namespace intrinsic::testing {

inline constexpr char kUndeclaredOutputsVar[] = "TEST_UNDECLARED_OUTPUTS_DIR";
inline constexpr char kUndeclaredOutputsAnnotationsVar[] =
    "TEST_UNDECLARED_OUTPUTS_ANNOTATIONS_DIR";

struct Options {
  // If true, the output file will not appear in the Sponge/BTX tests UI as a
  // linked file. The file will still be shown in the Artifacts tab.
  bool hide_file = false;
};

// Writes the annotation file for the undeclared output and prepares the
// directory for the file. Returns the absolute path to the output file.
//
// If the environment does not support undeclared outputs, this function
// returns an error.
//
// Example usage:
//
// ASSERT_OK_AND_ASSIGN(
//       const std::string absolute_file_path,
//       PrepareUndeclaredOutput(
//           /*filename=*/"test_image.jpg",
//           /*description=*/"Test image",
//           /*content_type=*/"image/jpeg",
//       ));
//   ASSERT_OK(perception::WriteImage(absolute_file_path, image));
//
// Follows
// go/sponge-undeclared-outputs#writing-undeclared-outputs-with-annotations.
absl::StatusOr<std::string> PrepareUndeclaredOutput(
    absl::string_view filename, absl::string_view description,
    absl::string_view content_type, const Options& options = Options());

// Saves a file to the undeclared outputs directory and annotates it. Returns an
// error if the environment does not support undeclared outputs.
//
// Example usage:
//
// ASSERT_OK(SaveUndeclaredOutput(
//       /*filename=*/"test_file.txt",
//       /*description=*/"Test file",
//       /*content_type=*/"text/plain",
//       /*data=*/"abcdef"));
//
// Follows
// go/sponge-undeclared-outputs#writing-undeclared-outputs-with-annotations.
absl::Status SaveUndeclaredOutput(absl::string_view filename,
                                  absl::string_view description,
                                  absl::string_view content_type,
                                  absl::string_view data,
                                  const Options& options = Options());

}  // namespace intrinsic::testing

#endif  // INTRINSIC_TESTING_UNDECLARED_UNDECLARED_OUTPUTS_H_
