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

#include <libxml/c14n.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlmemory.h>

#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace intrinsic::sdf {
namespace {

struct XmlDocDeleter {
  void operator()(xmlDoc* doc) const {
    if (doc != nullptr) {
      xmlFreeDoc(doc);
    }
  }
};

// Checks whether two tokens match, with numerical tolerance if both are floats.
bool TokensMatchWithTolerance(absl::string_view a, absl::string_view b,
                              double tolerance) {
  double val_a, val_b;
  if (absl::SimpleAtod(a, &val_a) && absl::SimpleAtod(b, &val_b)) {
    return std::abs(val_a - val_b) <= tolerance;
  }
  return a == b;
}

// Compares text strings, splitting by whitespace and comparing token by token
// with tolerance for numbers.
bool TextMatchesWithTolerance(absl::string_view a, absl::string_view b,
                              double tolerance) {
  std::vector<absl::string_view> tokens_a =
      absl::StrSplit(a, absl::ByAnyChar(" \t\n\r"), absl::SkipWhitespace());
  std::vector<absl::string_view> tokens_b =
      absl::StrSplit(b, absl::ByAnyChar(" \t\n\r"), absl::SkipWhitespace());
  if (tokens_a.size() != tokens_b.size()) {
    return false;
  }
  for (size_t i = 0; i < tokens_a.size(); ++i) {
    if (!TokensMatchWithTolerance(tokens_a[i], tokens_b[i], tolerance)) {
      return false;
    }
  }
  return true;
}

// Extracts non-empty element and text child nodes, ignoring pure-whitespace
// text nodes.
std::vector<xmlNode*> GetRelevantChildNodes(xmlNode* node) {
  std::vector<xmlNode*> children;
  for (xmlNode* cur = node->children; cur != nullptr; cur = cur->next) {
    if (cur->type == XML_ELEMENT_NODE) {
      children.push_back(cur);
    } else if (cur->type == XML_TEXT_NODE ||
               cur->type == XML_CDATA_SECTION_NODE) {
      if (cur->content != nullptr) {
        absl::string_view text(reinterpret_cast<const char*>(cur->content));
        if (!absl::StripAsciiWhitespace(text).empty()) {
          children.push_back(cur);
        }
      }
    }
  }
  return children;
}

// Compares attributes of two elements with tolerance for numerical attribute
// values.
bool AttributesMatchWithTolerance(xmlNode* a, xmlNode* b, double tolerance,
                                  std::string* mismatch_reason) {
  // Collect attributes into sorted map by attribute name.
  auto get_attrs = [](xmlNode* node) {
    std::map<std::string, std::string> attrs;
    for (xmlAttr* attr = node->properties; attr != nullptr; attr = attr->next) {
      if (attr->name != nullptr) {
        std::string name(reinterpret_cast<const char*>(attr->name));
        std::string val;
        if (attr->children != nullptr && attr->children->content != nullptr) {
          val = reinterpret_cast<const char*>(attr->children->content);
        }
        attrs[name] = val;
      }
    }
    return attrs;
  };

  auto attrs_a = get_attrs(a);
  auto attrs_b = get_attrs(b);

  if (attrs_a.size() != attrs_b.size()) {
    if (mismatch_reason != nullptr) {
      *mismatch_reason =
          absl::StrCat("attribute count mismatch on <",
                       reinterpret_cast<const char*>(a->name),
                       ">: ", attrs_a.size(), " vs ", attrs_b.size());
    }
    return false;
  }

  for (const auto& [name, val_a] : attrs_a) {
    auto it = attrs_b.find(name);
    if (it == attrs_b.end()) {
      if (mismatch_reason != nullptr) {
        *mismatch_reason =
            absl::StrCat("attribute '", name, "' missing in expected node");
      }
      return false;
    }
    const std::string& val_b = it->second;
    if (!TextMatchesWithTolerance(val_a, val_b, tolerance)) {
      if (mismatch_reason != nullptr) {
        *mismatch_reason =
            absl::StrCat("attribute '", name, "' value mismatch: '", val_a,
                         "' vs '", val_b, "'");
      }
      return false;
    }
  }
  return true;
}

// Recursively compares two XML nodes with numerical tolerance.
bool XmlNodesMatchWithTolerance(xmlNode* a, xmlNode* b, double tolerance,
                                std::string* mismatch_reason) {
  if (a == nullptr && b == nullptr) return true;
  if (a == nullptr || b == nullptr) {
    if (mismatch_reason != nullptr) {
      *mismatch_reason = "one node is null while the other is not";
    }
    return false;
  }

  if (a->type != b->type) {
    if (mismatch_reason != nullptr) {
      *mismatch_reason =
          absl::StrCat("node type mismatch: ", a->type, " vs ", b->type);
    }
    return false;
  }

  if (a->type == XML_ELEMENT_NODE) {
    absl::string_view name_a = reinterpret_cast<const char*>(a->name);
    absl::string_view name_b = reinterpret_cast<const char*>(b->name);
    if (name_a != name_b) {
      if (mismatch_reason != nullptr) {
        *mismatch_reason =
            absl::StrCat("tag name mismatch: <", name_a, "> vs <", name_b, ">");
      }
      return false;
    }

    if (!AttributesMatchWithTolerance(a, b, tolerance, mismatch_reason)) {
      return false;
    }

    auto children_a = GetRelevantChildNodes(a);
    auto children_b = GetRelevantChildNodes(b);

    if (children_a.size() != children_b.size()) {
      if (mismatch_reason != nullptr) {
        *mismatch_reason =
            absl::StrCat("child node count mismatch under <", name_a,
                         ">: ", children_a.size(), " vs ", children_b.size());
      }
      return false;
    }

    for (size_t i = 0; i < children_a.size(); ++i) {
      if (!XmlNodesMatchWithTolerance(children_a[i], children_b[i], tolerance,
                                      mismatch_reason)) {
        return false;
      }
    }
    return true;
  }

  if (a->type == XML_TEXT_NODE || a->type == XML_CDATA_SECTION_NODE) {
    absl::string_view text_a =
        a->content != nullptr ? reinterpret_cast<const char*>(a->content) : "";
    absl::string_view text_b =
        b->content != nullptr ? reinterpret_cast<const char*>(b->content) : "";
    if (!TextMatchesWithTolerance(text_a, text_b, tolerance)) {
      if (mismatch_reason != nullptr) {
        *mismatch_reason = absl::StrCat(
            "text content mismatch: '", absl::StripAsciiWhitespace(text_a),
            "' vs '", absl::StripAsciiWhitespace(text_b), "'");
      }
      return false;
    }
    return true;
  }

  return true;
}

}  // namespace

namespace internal {

bool XmlMatchesWithTolerance(absl::string_view actual_xml,
                             absl::string_view expected_xml, double tolerance,
                             std::string* mismatch_reason) {
  xmlResetLastError();
  std::unique_ptr<xmlDoc, XmlDocDeleter> actual_doc(xmlReadMemory(
      actual_xml.data(), static_cast<int>(actual_xml.size()), /*url=*/nullptr,
      /*encoding=*/nullptr,
      XML_PARSE_NOENT | XML_PARSE_DTDATTR | XML_PARSE_NONET));
  if (actual_doc == nullptr) {
    const xmlError* err = xmlGetLastError();
    if (mismatch_reason != nullptr) {
      *mismatch_reason = absl::StrCat("failed to parse actual XML: ",
                                      err != nullptr && err->message != nullptr
                                          ? err->message
                                          : "unknown error");
    }
    return false;
  }

  std::unique_ptr<xmlDoc, XmlDocDeleter> expected_doc(
      xmlReadMemory(expected_xml.data(), static_cast<int>(expected_xml.size()),
                    /*url=*/nullptr, /*encoding=*/nullptr,
                    XML_PARSE_NOENT | XML_PARSE_DTDATTR | XML_PARSE_NONET));
  if (expected_doc == nullptr) {
    const xmlError* err = xmlGetLastError();
    if (mismatch_reason != nullptr) {
      *mismatch_reason = absl::StrCat("failed to parse expected XML: ",
                                      err != nullptr && err->message != nullptr
                                          ? err->message
                                          : "unknown error");
    }
    return false;
  }

  return XmlNodesMatchWithTolerance(xmlDocGetRootElement(actual_doc.get()),
                                    xmlDocGetRootElement(expected_doc.get()),
                                    tolerance, mismatch_reason);
}

}  // namespace internal

absl::StatusOr<std::string> CanonicalizeXml(absl::string_view xml) {
  xmlResetLastError();
  std::unique_ptr<xmlDoc, XmlDocDeleter> doc(
      xmlReadMemory(xml.data(), static_cast<int>(xml.size()), /*url=*/nullptr,
                    /*encoding=*/nullptr,
                    XML_PARSE_NOENT | XML_PARSE_DTDATTR | XML_PARSE_NONET));
  if (doc == nullptr) {
    const xmlError* err = xmlGetLastError();
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse XML for canonicalization: ",
        err != nullptr && err->message != nullptr ? err->message
                                                  : "unknown error"));
  }

  xmlChar* output_buf = nullptr;
  int bytes_written = xmlC14NDocDumpMemory(
      doc.get(), /*nodes=*/nullptr, XML_C14N_1_0,
      /*inclusive_ns_prefixes=*/nullptr, /*with_comments=*/0, &output_buf);
  if (bytes_written < 0) {
    if (output_buf != nullptr) {
      xmlFree(output_buf);
    }
    const xmlError* err = xmlGetLastError();
    return absl::InternalError(absl::StrCat(
        "Failed to canonicalize XML: ",
        err != nullptr && err->message != nullptr ? err->message
                                                  : "unknown error"));
  }

  std::string result(reinterpret_cast<const char*>(output_buf), bytes_written);
  xmlFree(output_buf);
  return result;
}

}  // namespace intrinsic::sdf
