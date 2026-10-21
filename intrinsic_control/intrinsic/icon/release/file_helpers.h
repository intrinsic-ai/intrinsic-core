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

#ifndef INTRINSIC_ICON_RELEASE_FILE_HELPERS_H_
#define INTRINSIC_ICON_RELEASE_FILE_HELPERS_H_

#include <cerrno>
#include <cstring>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"

namespace intrinsic {

// T should be a proto.
template <typename T>
absl::StatusOr<T> GetBinaryProto(absl::string_view filename) {
  std::ifstream ifs(std::string(filename),
                    std::ios_base::in | std::ios_base::binary);
  if (!ifs.is_open()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unable to open file '", filename,
                     "'. Error: ", std::strerror(errno), "."));
  }
  std::stringstream ss;
  ss << ifs.rdbuf();

  T proto;
  bool result = proto.ParsePartialFromString(ss.str());
  if (!result) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unable to parse file '", filename, "'."));
  }

  return proto;
}

// T should be a proto.
template <typename T>
absl::Status SetBinaryProto(absl::string_view filename, const T& my_proto) {
  std::ofstream ofs(std::string(filename),
                    std::ios_base::out | std::ios_base::binary);
  if (!ofs.is_open()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unable to open file '", filename, "'."));
  }

  google::protobuf::io::OstreamOutputStream output_stream(&ofs);
  google::protobuf::io::CodedOutputStream coded_stream(&output_stream);
  coded_stream.SetSerializationDeterministic(true);
  if (!my_proto.SerializePartialToCodedStream(&coded_stream)) {
    return absl::InternalError("Failed to serialize proto to coded stream.");
  }

  return absl::OkStatus();
}

}  // namespace intrinsic

#endif  // INTRINSIC_ICON_RELEASE_FILE_HELPERS_H_
