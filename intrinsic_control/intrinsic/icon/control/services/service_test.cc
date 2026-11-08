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

#include "intrinsic/icon/control/services/service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/control/services/service_collection.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

class TestService {
 public:
  virtual ~TestService() = default;
  virtual int AlwaysReturn42() = 0;
};

class TestServiceImpl : public ServiceImplBase<TestServiceImpl, TestService> {
 public:
  int AlwaysReturn42() override { return 42; }
};

TEST(ServiceTest, CreateFromServiceListNoInitializeDuplicateKeyError) {
  std::vector<std::unique_ptr<Service>> services;
  services.emplace_back(std::make_unique<TestServiceImpl>());
  services.emplace_back(std::make_unique<TestServiceImpl>());
  EXPECT_THAT(
      ServiceCollection::CreateFromServiceListNoInitialize(std::move(services)),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr(Service::Key<TestService>())));
}

TEST(ServiceTest, CreateFromServiceListNoInitialize) {
  auto test_service = std::make_unique<TestServiceImpl>();
  std::vector<std::unique_ptr<Service>> services;
  services.emplace_back(std::move(test_service));
  ASSERT_OK_AND_ASSIGN(ServiceCollection collection,
                       ServiceCollection::CreateFromServiceListNoInitialize(
                           std::move(services)));
  TestService* service = collection.GetServiceOrNull<TestService>();
  EXPECT_NE(service, nullptr);
  EXPECT_EQ(service->AlwaysReturn42(), 42);
}

}  // namespace
}  // namespace intrinsic::icon
