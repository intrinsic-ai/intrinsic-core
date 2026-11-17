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

#include "absl/status/statusor.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/util/scene_object_gzf.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace scene {
namespace {

using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::Partially;

using ::intrinsic::scene_object::GetSceneObjectFromGzf;
using ::intrinsic_proto::scene_object::v1::SceneObject;

absl::StatusOr<SceneObject> GetSceneObjectFromGzf() {
  const std::string kBoxFile = PathResolver::ResolveRunfilesPath(
      "intrinsic/scene/build_defs/tests/box_with_updates.gzf");
  INTR_ASSIGN_OR_RETURN(auto gzfile, GZFile::Open(kBoxFile));
  return GetSceneObjectFromGzf(*gzfile);
}

absl::StatusOr<SceneObject> GetSceneObjectFromTextProto() {
  const std::string kBoxFile = PathResolver::ResolveRunfilesPath(
      "intrinsic/scene/build_defs/tests/box_with_updates.pbtxt");
  SceneObject scene_object;
  INTR_RETURN_IF_ERROR(
      file::GetTextProto(kBoxFile, &scene_object, file::Defaults()));
  return scene_object;
}

struct SceneObjectTestParam {
  std::string test_name;
  absl::StatusOr<SceneObject> scene_object;
};

std::vector<SceneObjectTestParam> GetTestParams() {
  return {
      {"FromGzfFile", GetSceneObjectFromGzf()},
      {"FromTextProto", GetSceneObjectFromTextProto()},
  };
}

class BoxWithUpdatesTest
    : public ::testing::TestWithParam<SceneObjectTestParam> {};

INSTANTIATE_TEST_SUITE_P(
    BoxWithUpdatesTest, BoxWithUpdatesTest,
    ::testing::ValuesIn(GetTestParams()),
    [](const ::testing::TestParamInfo<SceneObjectTestParam>& info) {
      return info.param.test_name;
    });

TEST_P(BoxWithUpdatesTest, CheckUpdatesApplied) {
  const SceneObjectTestParam& param = GetParam();
  ASSERT_OK_AND_ASSIGN(const auto scene_object, param.scene_object);

  // Checks that the link has the correct pose.
  EXPECT_THAT(scene_object, Partially(EqualsProto(R"pb(
                entities {
                  name: "link"
                  parent_t_this: {
                    position { x: 0.5 y: 0.5 z: 0.5 }
                    orientation: { w: 1 }
                  }
                }
              )pb")));

  // Checks that user data was applied.
  // NOTE: Use `default_instance()` for the expected user_data protobuf types to
  // ensure that corresponding cc protos are explicitly linked into the test.
  // This is needed for `Any` proto parsing to work as expected.
  EXPECT_THAT(scene_object.user_data(),
              UnorderedElementsAre(
                  Pair("my_key", EqualsProto(R"pb(
                         [type.googleapis.com/google.protobuf.StringValue] {
                           value: "my_value"
                         }
                       )pb")),
                  Pair("struct_key", EqualsProto(R"pb(
                         [type.googleapis.com/google.protobuf.Struct] {
                           fields {
                             key: "is_it_a_box"
                             value { bool_value: true }
                           }
                         }
                       )pb"))))
      << "Failed to match user data for types: "
      << google::protobuf::StringValue::default_instance().GetTypeName()
      << " and " << google::protobuf::Struct::default_instance().GetTypeName();
}

}  // namespace
}  // namespace scene
}  // namespace intrinsic
