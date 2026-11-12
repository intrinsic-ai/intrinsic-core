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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status_matchers.h"
#include "google/protobuf/descriptor.pb.h"
#include "internal/testing.h"
#include "intrinsic/util/proto/dynamic_message_parser.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "re2/re2.h"

ABSL_FLAG(
    std::string, message_full_name, "",
    "Full name of the message type to parse, e.g. 'intrinsic_proto.Pose3d'");
ABSL_FLAG(std::vector<std::string>, text_proto_files, {},
          "Names of the files containing the text proto to parse.");
ABSL_FLAG(std::string, transitive_descriptor_set_file, "",
          "Name of the file containing a binary-encoded file descriptor set "
          "with all dependencies of the message type to parse.");

namespace intrinsic {
namespace {

class TextProtoTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    fds_ = new google::protobuf::FileDescriptorSet();
    CHECK_OK(file::GetBinaryProto(
        absl::GetFlag(FLAGS_transitive_descriptor_set_file), fds_,
        file::Defaults()))
        << "Failed to read transitive descriptor set";
  }

  static void TearDownTestSuite() { delete fds_; }

  static google::protobuf::FileDescriptorSet* fds_;
};

google::protobuf::FileDescriptorSet* TextProtoTest::fds_;

class TextProtoTestParameterized
    : public TextProtoTest,
      public ::testing::WithParamInterface<std::string> {};

INSTANTIATE_TEST_SUITE_P(
    AllFiles, TextProtoTestParameterized,
    ::testing::ValuesIn(absl::GetFlag(FLAGS_text_proto_files)),
    [](const ::testing::TestParamInfo<std::string>& info) {
      std::string test_name = info.param;
      // Replace all non-word characters with underscores.
      RE2::GlobalReplace(&test_name, R"(\W)", "_");
      return test_name;
    });

TEST_P(TextProtoTestParameterized, IsValid) {
  ASSERT_OK_AND_ASSIGN(std::string contents,
                       file::GetContents(GetParam(), file::Defaults()));

  ASSERT_OK(DynamicMessageParser::ParseSingleTextProto(
      *fds_, absl::GetFlag(FLAGS_message_full_name), contents));
}

}  // namespace
}  // namespace intrinsic
