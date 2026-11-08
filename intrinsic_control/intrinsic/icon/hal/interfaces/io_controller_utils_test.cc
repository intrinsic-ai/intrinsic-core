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

#include "intrinsic/icon/hal/interfaces/io_controller_utils.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <vector>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/interfaces/adio.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"

namespace intrinsic {
namespace adio {
namespace {

using ::testing::Test;

using ::intrinsic_fbs::AnalogInputUnit;

TEST(IoControllerUtilsTest, BuildDIOCommand) {
  std::vector<std::string> params;
  params.push_back("first");
  params.push_back("second");

  flatbuffers::DetachedBuffer buffer = intrinsic_fbs::BuildDIOCommand(params);

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<intrinsic_fbs::DIOCommand>(), true);

  auto status =
      flatbuffers::GetMutableRoot<intrinsic_fbs::DIOCommand>(buffer.data());
  EXPECT_NE(status, nullptr);

  auto signals = status->mutable_signals();
  EXPECT_EQ(signals->size(), params.size());

  for (size_t i = 0; i < signals->size(); i++) {
    auto tmp_signal = signals->GetMutableObject(i);
    EXPECT_NE(tmp_signal, nullptr);
    std::string tmp_name = params.at(i);
    EXPECT_EQ(tmp_signal->name()->c_str(), tmp_name);
    EXPECT_EQ(tmp_signal->bit_number(), i);
    EXPECT_TRUE(tmp_signal->mutate_value(false));
    EXPECT_TRUE(tmp_signal->mutate_value(true));
  }
}

TEST(IoControllerUtilsTest, BuildDIOStatus) {
  std::vector<std::string> params;
  params.push_back("first");
  params.push_back("second");

  flatbuffers::DetachedBuffer buffer = intrinsic_fbs::BuildDIOStatus(params);

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<intrinsic_fbs::DIOStatus>(), true);

  auto status =
      flatbuffers::GetMutableRoot<intrinsic_fbs::DIOStatus>(buffer.data());
  EXPECT_NE(status, nullptr);

  auto signals = status->mutable_signals();
  EXPECT_EQ(signals->size(), params.size());

  for (size_t i = 0; i < signals->size(); i++) {
    auto tmp_signal = signals->GetMutableObject(i);
    EXPECT_NE(tmp_signal, nullptr);
    std::string tmp_name = params.at(i);
    EXPECT_EQ(tmp_signal->name()->c_str(), tmp_name);
    EXPECT_EQ(tmp_signal->bit_number(), i);
    EXPECT_TRUE(tmp_signal->mutate_value(false));
    EXPECT_TRUE(tmp_signal->mutate_value(true));
  }
}

TEST(IoControllerUtilsTest, BuildAIOCommand) {
  std::vector<std::string> names;
  names.push_back("first");
  names.push_back("second");
  flatbuffers::DetachedBuffer buffer = intrinsic_fbs::BuildAIOCommand(names);

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<intrinsic_fbs::AIOCommand>(), true);

  auto status =
      flatbuffers::GetMutableRoot<intrinsic_fbs::AIOCommand>(buffer.data());
  EXPECT_NE(status, nullptr);

  auto signals = status->mutable_signals();
  EXPECT_EQ(signals->size(), names.size());

  for (size_t i = 0; i < signals->size(); i++) {
    auto tmp_signal = signals->GetMutableObject(i);
    EXPECT_NE(tmp_signal, nullptr);
    std::string tmp_name = names.at(i);
    EXPECT_EQ(tmp_signal->name()->c_str(), tmp_name);
    // Test mutation of values
    EXPECT_TRUE(tmp_signal->mutate_value(873.23));
    EXPECT_TRUE(tmp_signal->mutate_unit(AnalogInputUnit::kSievert));
    EXPECT_TRUE(tmp_signal->mutate_is_enabled(true));
    EXPECT_TRUE(tmp_signal->mutate_is_enabled(false));
  }
}

TEST(IoControllerUtilsTest, BuildAIOStatus) {
  std::vector<std::string> names;
  names.push_back("first");
  names.push_back("second");
  flatbuffers::DetachedBuffer buffer = intrinsic_fbs::BuildAIOStatus(names);

  flatbuffers::Verifier verifier(buffer.data(), buffer.size());
  EXPECT_EQ(verifier.VerifyBuffer<intrinsic_fbs::AIOStatus>(), true);

  auto status =
      flatbuffers::GetMutableRoot<intrinsic_fbs::AIOStatus>(buffer.data());
  EXPECT_NE(status, nullptr);

  auto signals = status->mutable_signals();
  EXPECT_EQ(signals->size(), names.size());

  for (size_t i = 0; i < signals->size(); i++) {
    auto tmp_signal = signals->GetMutableObject(i);
    EXPECT_NE(tmp_signal, nullptr);
    std::string tmp_name = names.at(i);
    EXPECT_EQ(tmp_signal->name()->c_str(), tmp_name);
    // Test mutation of values
    EXPECT_TRUE(tmp_signal->mutate_value(873.23));
    EXPECT_TRUE(tmp_signal->mutate_unit(AnalogInputUnit::kTesla));
    EXPECT_TRUE(tmp_signal->mutate_is_enabled(true));
    EXPECT_TRUE(tmp_signal->mutate_is_enabled(false));
  }
}

}  // namespace
}  // namespace adio
}  // namespace intrinsic
