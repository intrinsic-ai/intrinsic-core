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

#include "intrinsic/scene/sdf/testing/xml_matchers.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "internal/testing.h"

namespace intrinsic::sdf {
namespace {

using ::testing::Not;

TEST(XmlMatchersTest, MatchesIdenticalXml) {
  std::string xml = "<root><child attr=\"value\"/></root>";
  EXPECT_THAT(xml, XmlEquivalent(xml));
}

TEST(XmlMatchersTest, MatchesXmlWithDifferentAttributeOrder) {
  std::string xml1 = "<element b=\"2\" a=\"1\" c=\"3\"/>";
  std::string xml2 = "<element a=\"1\" b=\"2\" c=\"3\"/>";
  EXPECT_THAT(xml1, XmlEquivalent(xml2));
}

TEST(XmlMatchersTest, MatchesXmlWithDifferentWhitespaceInTags) {
  std::string xml1 = "<element   attr=\"1\"   />";
  std::string xml2 = "<element attr=\"1\"/>";
  EXPECT_THAT(xml1, XmlEquivalent(xml2));
}

TEST(XmlMatchersTest, DetectsDifferentValues) {
  std::string xml1 = "<element attr=\"1\"/>";
  std::string xml2 = "<element attr=\"2\"/>";
  EXPECT_THAT(xml1, Not(XmlEquivalent(xml2)));
}

TEST(XmlMatchersTest, MatchesSmallFloatingPointDifferencesWithinTolerance) {
  std::string xml1 =
      "<model name=\"test\"><pose>0 0 0.15000000000000002 0 0 0</pose></model>";
  std::string xml2 = "<model name=\"test\"><pose>0 0 0.15 0 0 0</pose></model>";

  EXPECT_THAT(xml1, XmlEquivalentWithTolerance(xml2, 1e-6));
}

TEST(XmlMatchersTest, MatchesSmallFloatingPointDifferencesInAttributes) {
  std::string xml1 = "<box size=\"1.0000000000000004 2 3\"/>";
  std::string xml2 = "<box size=\"1 2 3\"/>";

  EXPECT_THAT(xml1, XmlEquivalentWithTolerance(xml2, 1e-6));
}

TEST(XmlMatchersTest, FailsWhenDifferenceExceedsTolerance) {
  std::string xml1 = "<element val=\"1.05\"/>";
  std::string xml2 = "<element val=\"1.00\"/>";

  EXPECT_THAT(xml1, Not(XmlEquivalentWithTolerance(xml2, 1e-3)));
  EXPECT_THAT(xml1, XmlEquivalentWithTolerance(xml2, 0.1));
}

TEST(XmlMatchersTest, CanonicalizeXmlFailsOnMalformedXml) {
  auto result = CanonicalizeXml("<unclosed_tag>");
  EXPECT_FALSE(result.ok());
}

}  // namespace
}  // namespace intrinsic::sdf
