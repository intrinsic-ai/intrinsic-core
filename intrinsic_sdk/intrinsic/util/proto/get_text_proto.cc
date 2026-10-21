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

#include "intrinsic/util/proto/get_text_proto.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>

#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/io/tokenizer.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace intrinsic {
namespace internal {
namespace {

// Collects protobuf errors and warnings into multiline strings.
class StringErrorCollector : public google::protobuf::io::ErrorCollector {
 public:
  StringErrorCollector() = default;
  ~StringErrorCollector() override = default;

  void RecordError(int line, google::protobuf::io::ColumnNumber column,
                   absl::string_view message) override {
    absl::StrAppend(&errors_, "line ", line, " column ", column, ": ", message,
                    "\n");
  }

  void RecordWarning(int line, google::protobuf::io::ColumnNumber column,
                     absl::string_view message) override {
    absl::StrAppend(&warnings_, "Warning line ", line, " column ", column, ": ",
                    message, "\n");
  }

  // Returns errors as a multiline string.
  std::string GetErrors() { return errors_; }

  // Returns warnings as a multiline string.
  std::string GetWarnings() { return warnings_; }

 private:
  // Multiline string describing all errors.
  std::string errors_;
  // Multiline string describing all warnings.
  std::string warnings_;
};

}  // namespace

absl::Status GetTextProtoPortable(absl::string_view filename,
                                  google::protobuf::Message& proto) {
  int fd = open(std::string(filename).c_str(), O_RDONLY);
  if (fd == -1) {
    return absl::NotFoundError(
        absl::StrCat("error reading file ", filename, ": ", strerror(errno)));
  }
  google::protobuf::io::FileInputStream fstream(fd);
  StringErrorCollector error_collector;
  google::protobuf::TextFormat::Parser parser;
  parser.RecordErrorsTo(&error_collector);
  if (!parser.Parse(&fstream, &proto)) {
    return absl::InvalidArgumentError(
        absl::StrCat("failed to parse text proto ", filename, "\n",
                     error_collector.GetErrors()));
  }
  if (std::string warnings = error_collector.GetWarnings(); !warnings.empty()) {
    LOG(WARNING) << "warnings while parsing text proto " << filename << "\n"
                 << warnings;
  }
  return absl::OkStatus();
}

}  // namespace internal

absl::Status GetTextProto(absl::string_view filename,
                          google::protobuf::Message& proto) {
  return internal::GetTextProtoPortable(filename, proto);
}

}  // namespace intrinsic
