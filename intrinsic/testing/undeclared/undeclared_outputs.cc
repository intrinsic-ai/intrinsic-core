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

#include "intrinsic/testing/undeclared/undeclared_outputs.h"

#include <gtest/gtest.h>
#include <stdlib.h>

#include <cstdlib>
#include <optional>
#include <string>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/testing/undeclared/undeclared_output_annotation.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/unique_id.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "riegeli/bytes/fd_writer.h"
#include "riegeli/messages/serialize_message.h"

namespace intrinsic::testing {

namespace {

// Use a UUID for the annotation file name to avoid collisions with other
// annotation files created by other libraries. Use the same UUID for all
// annotations created in the same test process, which results in a single file
// containing all annotations.
std::string AnnotationUuid() {
  static const absl::NoDestructor<std::string> kUuid(intrinsic::UniqueId());
  return *kUuid;
}

std::optional<std::string> GetTestCaseAnnotation() {
  const ::testing::TestInfo* const test_info =
      ::testing::UnitTest::GetInstance()->current_test_info();
  if (test_info == nullptr) {
    return std::nullopt;
  }
  return absl::StrCat(test_info->test_suite_name(), "#", test_info->name());
}

}  // namespace

absl::StatusOr<std::string> PrepareUndeclaredOutput(
    absl::string_view filename, absl::string_view description,
    absl::string_view content_type, const Options& options) {
  const char* const kUndeclaredOutputsDir = getenv(kUndeclaredOutputsVar);
  if (kUndeclaredOutputsDir == nullptr) {
    return absl::FailedPreconditionError(
        "TEST_UNDECLARED_OUTPUTS_DIR is not set.");
  }

  const std::optional<std::string> test_case_annotation =
      GetTestCaseAnnotation();
  if (!test_case_annotation.has_value()) {
    return absl::FailedPreconditionError("No test is currently running.");
  }

  const std::string relative_file_path =
      file::JoinPath(*test_case_annotation, filename);
  std::string absolute_file_path =
      file::JoinPath(kUndeclaredOutputsDir, relative_file_path);

  INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(
      file::Dirname(absolute_file_path), file::Defaults()));

  if (options.hide_file) {
    return absolute_file_path;
  }

  const char* const kAnnotationsDir = getenv(kUndeclaredOutputsAnnotationsVar);
  if (kAnnotationsDir == nullptr) {
    return absl::FailedPreconditionError(
        "TEST_UNDECLARED_OUTPUTS_ANNOTATIONS_DIR is not set.");
  }

  const std::string annotation_file_path =
      file::JoinPath(kAnnotationsDir, absl::StrCat(AnnotationUuid(), ".pb"));
  intrinsic_proto::testing::UndeclaredOutputAnnotation annotation;
  annotation.set_relative_path(relative_file_path);
  annotation.set_description(description);
  annotation.set_content_type(content_type);
  annotation.set_test_case(*test_case_annotation);

  INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(
      file::Dirname(annotation_file_path), file::Defaults()));
  riegeli::FdWriter annotations_writer(
      annotation_file_path, riegeli::FdWriterBase::Options().set_append(true));
  if (!annotations_writer.ok()) {
    return annotations_writer.status();
  }

  INTR_RETURN_IF_ERROR(
      riegeli::SerializeLengthPrefixedMessage(annotation, annotations_writer));
  if (!annotations_writer.Close()) {
    return absl::InternalError(
        absl::StrCat("failed to write undeclared output annotations: ",
                     annotations_writer.status()));
  }

  return absolute_file_path;
}

absl::Status SaveUndeclaredOutput(absl::string_view filename,
                                  absl::string_view description,
                                  absl::string_view content_type,
                                  absl::string_view data,
                                  const Options& options) {
  INTR_ASSIGN_OR_RETURN(
      const std::string absolute_file_path,
      PrepareUndeclaredOutput(filename, description, content_type, options));

  return file::SetContents(absolute_file_path, data, file::Defaults());
}

}  // namespace intrinsic::testing
