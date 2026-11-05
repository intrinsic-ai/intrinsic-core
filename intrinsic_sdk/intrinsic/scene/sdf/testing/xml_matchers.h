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

#ifndef INTRINSIC_SCENE_SDF_TESTING_XML_MATCHERS_H_
#define INTRINSIC_SCENE_SDF_TESTING_XML_MATCHERS_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace intrinsic::sdf {
namespace internal {

// Compares two XML strings with numerical tolerance on float values in
// attributes and text nodes.
bool XmlMatchesWithTolerance(absl::string_view actual_xml,
                             absl::string_view expected_xml, double tolerance,
                             std::string* mismatch_reason);

}  // namespace internal

// Canonicalizes an XML string using W3C Canonical XML (C14N 1.0) via libxml2.
// Attributes are sorted alphabetically, namespaces are canonicalized, and
// whitespace inside tags and attributes is normalized according to the C14N
// spec.
absl::StatusOr<std::string> CanonicalizeXml(absl::string_view xml);

// GTest matcher that checks if two XML strings are equivalent after
// canonicalization using libxml2 xmlC14NDocDumpMemory (C14N 1.0).
MATCHER_P(XmlEquivalent, expected_xml,
          std::string(negation ? "is not" : "is") +
              " canonical XML equivalent to expected XML") {
  absl::StatusOr<std::string> actual_c14n = CanonicalizeXml(arg);
  if (!actual_c14n.ok()) {
    *result_listener << "failed to canonicalize actual XML: "
                     << actual_c14n.status().message();
    return false;
  }

  absl::StatusOr<std::string> expected_c14n = CanonicalizeXml(expected_xml);
  if (!expected_c14n.ok()) {
    *result_listener << "failed to canonicalize expected XML: "
                     << expected_c14n.status().message();
    return false;
  }

  if (*actual_c14n != *expected_c14n) {
    *result_listener << "\nActual canonical XML:\n"
                     << *actual_c14n << "\nExpected canonical XML:\n"
                     << *expected_c14n;
    return false;
  }
  return true;
}

// GTest matcher that checks if two XML strings are structurally equivalent
// within a numerical tolerance for float values (both in attributes and text).
MATCHER_P2(XmlEquivalentWithTolerance, expected_xml, tolerance,
           std::string(negation ? "is not" : "is") +
               " XML equivalent to expected XML within tolerance " +
               ::testing::PrintToString(tolerance)) {
  std::string mismatch_reason;
  if (!internal::XmlMatchesWithTolerance(arg, expected_xml, tolerance,
                                         &mismatch_reason)) {
    *result_listener << "\nXML mismatch (" << mismatch_reason << ")";
    return false;
  }
  return true;
}

}  // namespace intrinsic::sdf

#endif  // INTRINSIC_SCENE_SDF_TESTING_XML_MATCHERS_H_
