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

#ifndef INTRINSIC_ICON_UTILS_XML_REALTIME_XML_GENERATOR_H_
#define INTRINSIC_ICON_UTILS_XML_REALTIME_XML_GENERATOR_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <variant>

#include "absl/base/attributes.h"
#include "absl/container/fixed_array.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
class RealtimeXmlGenerator;
class RtXmlElement;
class RtXmlAttribute;

// String length reserved for all floating point numbers.
constexpr size_t kFloatingPointNumberStringSize = 64;

// XML generator that can be used in real-time code, i.e. this XML
// generator implementation uses no mutexes, does no memory allocations, and has
// a deterministic runtime. Only the Con/Destructors are not real-time safe.
// Thus, the class should be created in a non-real-time context and be passed to
// the real-time thread.
//
// Note: All strings given to any of the functions need to outlive the
// RealtimeXmlGenerator instance.
//
// Note: The RT XML parser and generator do not round-trip perfectly in all
// cases.
//

class RealtimeXmlGenerator {
 public:
  RealtimeXmlGenerator(size_t max_element_count,
                       size_t max_attribute_count) INTRINSIC_NON_REALTIME_ONLY;
  ~RealtimeXmlGenerator() INTRINSIC_NON_REALTIME_ONLY = default;

  // Adds the root element. There can only be one.
  // Note: The `name` buffer must exist until the XML string is generated.
  //
  // Returns a FailedPrecondition error when the generator has a root element
  // already.
  icon::RealtimeStatusOr<RtXmlElement*> AddRootElement(absl::string_view name)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Queries the root element. Never returns null.
  //
  // Returns a NotFound error when the root element does not exist.
  icon::RealtimeStatusOr<RtXmlElement*> RootElement() const
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Generates the XML string into the given span `buffer`.
  //
  // The span must have a size large enough to fit the whole XML string.
  // Returns an ResourceExhausted error if the buffer is too small for the XML
  // string.
  // Returns the size of the generated string. The string is not
  // null-terminated.
  // Returns NotFound error when the root node cannot be found.
  icon::RealtimeStatusOr<size_t> GenerateXMLString(absl::Span<char> buffer)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Clears all internal memory so that this generator can be reused for a new
  // XML document. Does *not* free any memory, but drops all references to
  // strings that have been passed in.
  void Clear() INTRINSIC_CHECK_REALTIME_SAFE;

 private:
  // Recursively generates `element` and all its sub-elements into `buffer`
  // starting at `position`.
  //
  // If `element` contains both, sub-elements and text, the sub-elements will be
  // generated before the text.
  // The address of `element` is used to search for sub-elements. Thus, make
  // sure not to pass in some temporary copy of an element but only elements
  // contained in `elements_`.
  //
  // On success, increments `position` by the size of the generated XML
  // string.
  icon::RealtimeStatus GenerateElements(
      absl::Span<char> buffer, size_t& position,
      const RtXmlElement* element) INTRINSIC_CHECK_REALTIME_SAFE;

  // Sorts the elements so that all elements belonging to the same parent
  // appear one after another. Sorting by the parents gives a better
  // performance for generating the XML string, since we only need to search
  // (using binary search on a sorted container) for the first element for a
  // given parent and can iterate through all sub-elements of this parent by
  // iterating until finding an element with a different parent.
  //
  // The element id is used as secondary sort criteria, which
  // is only used to keep the elements in the order in which they were added.
  void SortContainers() INTRINSIC_CHECK_REALTIME_SAFE;

  // Attempts to find the first child of `parent`. To get the other children,
  // increment the returned iterator until either one of the following:
  //  - The iterator points to an element whose parent is not equal to parent.
  //  - The iterator equals current_elements_span_.end().
  //
  // Note: `current_elements_span_` must be sorted with `SortContainers()`
  // before using this function.
  //
  // Returns `current_elements_span_.end()` if `parent` has no child.
  absl::Span<std::unique_ptr<RtXmlElement>>::const_iterator
  FindFirstChildElement(const RtXmlElement* parent) const
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Attempts to find the first attribute of `parent`. To get the other
  // attributes, increment the returned iterator until either one of the
  // following:
  //  - The iterator points to an attribute whose parent is not equal to parent.
  //  - The iterator equals current_attributes_span_.end().
  //
  // Note: `current_attributes_span_` must be sorted with `SortContainers()`
  // before using this function.
  //
  // Returns `current_attributes_span_.end()` if `parent` has no attributes.
  absl::Span<std::unique_ptr<RtXmlAttribute>>::const_iterator
  FindFirstAttribute(const RtXmlElement* parent) const
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Generates the XML code for all attributes of `parent` into `buffer`
  // starting at `position`.
  //
  // On success, `position` will be incremented by the size of the generated XML
  // string.
  icon::RealtimeStatus GenerateAttributes(
      absl::Span<char> buffer, size_t& position,
      const RtXmlElement* parent) INTRINSIC_CHECK_REALTIME_SAFE;

  // Elements need to be on the heap so that reordering the array does not
  // change the parent each element points to. To keep real-time capabilities,
  // they are pre-allocated in the constructor.
  absl::FixedArray<std::unique_ptr<RtXmlElement>> elements_;
  // Span of the currently used part of the `elements_` array.
  absl::Span<std::unique_ptr<RtXmlElement>> current_elements_span_;
  // Attributes need to be on the heap so that reordering the array does not
  // change the parent each element points to. To keep real-time capabilities,
  // they are pre-allocated in the constructor.
  absl::FixedArray<std::unique_ptr<RtXmlAttribute>> attributes_;
  // Span of the currently used part of the `attributes_` array.
  absl::Span<std::unique_ptr<RtXmlAttribute>> current_attributes_span_;
  friend class RtXmlElement;
};

// Defines an XML attribute in combination with `RealtimeXmlGenerator`.
class RtXmlAttribute {
  explicit RtXmlAttribute(RtXmlElement* parent, absl::string_view name)
      : parent_(parent), name_(name) {}

 public:
  RtXmlAttribute() = default;

  // Sets a string as the value.
  //
  // Note: The `text` buffer must exist until the XML string is generated.
  icon::RealtimeStatus SetValue(absl::string_view text)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Stores a number as the value of an attribute. The precision for floating
  // point values is the precision used by absl::AlphaNum, i.e. 6 digits.
  template <typename NumType,
            typename = typename std::is_integral<NumType>::type>
  icon::RealtimeStatus SetValue(NumType value) INTRINSIC_CHECK_REALTIME_SAFE {
    string_ = icon::FixedStrCat<kFloatingPointNumberStringSize>(value);
    return icon::OkStatus();
  }

  // Returns the value as a string. This function does not escape the string
  // since this would need memory allocation.
  absl::string_view ValueToString() INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns the XML element with which this XML attribute is associated with.
  RtXmlElement* Parent() const INTRINSIC_CHECK_REALTIME_SAFE { return parent_; }

 private:
  static size_t NextId();
  RtXmlElement* parent_ = nullptr;
  absl::string_view name_;
  // Variant to store text references or numbers serialized to string.
  // kFloatingPointerNumberStringSize characters is large enough for all numbers
  // that are serialized to string.
  std::variant<icon::FixedString<kFloatingPointNumberStringSize>,
               absl::string_view>
      string_;
  size_t unique_id_ = NextId();
  friend class RealtimeXmlGenerator;
  friend class RtXmlElement;
};

// Class to define an XML element in combination with `RealtimeXmlGenerator`.
class RtXmlElement {
  RtXmlElement(const RtXmlElement& generator) = delete;  // sort must not copy
  RtXmlElement(RtXmlElement&& generator) = delete;       // sort must not move
  RtXmlElement& operator=(RtXmlElement& generator) =
      default;  // private so that sort cannot use it
  RtXmlElement& operator=(RtXmlElement&& generator) =
      default;  // private so that sort cannot use it

 public:
  RtXmlElement() = default;

  // Appends a new child element at the end of this element's contents.
  //
  // Returns InvalidArgumentError if `name` is not a valid XML element name.
  // Returns ResourceExhaustedError if the memory allocated in the generator is
  // exhausted.
  //
  // Note: The `name` buffer must exist until the XML string is generated.
  icon::RealtimeStatusOr<RtXmlElement*> AddElement(absl::string_view name)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Appends a new child element at the end of this element's contents and also
  // sets the text value of this new child.
  //
  // Returns a non-null pointer to the newly added child element on success.
  // Returns an InvalidArgumentError if `name` is not a valid XML element name.
  // Returns ResourceExhaustedError if the memory allocated in the generator is
  // exhausted.
  //
  // Note: The `name` buffer must exist until the XML string is generated.
  // If `T` is a string type, the memory of `value` must exist until the XML is
  // generated.
  template <typename T>
  icon::RealtimeStatusOr<RtXmlElement*> AddElementWithText(
      absl::string_view name, const T& value) INTRINSIC_CHECK_REALTIME_SAFE {
    INTRINSIC_RT_ASSIGN_OR_RETURN(RtXmlElement * element, AddElement(name));
    element->SetText(value);
    return element;
  }

  // Adds an attribute to this XML element named `name` and sets its value to
  // the empty string.
  //
  // Returns an InvalidArgumentError if `name` is not a valid XML attribute
  // name.
  // Returns ResourceExhaustedError if the memory allocated in the
  // generator is exhausted.
  //
  // Set `check_for_duplicate` to true to check if an attribute with same name
  // already exists in this element (which is forbidden in XML) (at a
  // performance cost).
  //
  // Note: The `name` buffer must exist until the XML string is generated.
  icon::RealtimeStatusOr<RtXmlAttribute*> AddAttribute(
      absl::string_view name,
      bool check_for_duplicate = true) INTRINSIC_CHECK_REALTIME_SAFE;

  // Convenience function to add an XML attribute and its value to this
  // element.
  //
  // Set `check_for_duplicate` to true to check if an attribute with same name
  // already exists in this element (which is forbidden in XML) (at a
  // performance cost).
  //
  // Note: The string in `name` must outlive the generator instance.
  // If `T` is a string type, the memory of `value` must exist until the XML is
  // generated.
  template <typename T>
  icon::RealtimeStatus AddAttributeWithValue(
      absl::string_view name, const T& value,
      bool check_for_duplicate = true) INTRINSIC_CHECK_REALTIME_SAFE {
    INTRINSIC_RT_ASSIGN_OR_RETURN(RtXmlAttribute * attr,
                                  AddAttribute(name, check_for_duplicate));
    return attr->SetValue(value);
  }

  // Sets a string as the element's text. The `text` buffer must
  // exist until the XML string is generated.
  void SetText(absl::string_view text) INTRINSIC_CHECK_REALTIME_SAFE {
    text_ = text;
  }

  // Stores a string representation of the given `value` as the text value of
  // this XML element. The type of `value` must be convertible with
  // icon::FixedStrCat(). The precision for floating
  // point values is the precision used by absl::AlphaNum, i.e. 6 digits.
  template <typename NumType>
  void SetText(NumType value) INTRINSIC_CHECK_REALTIME_SAFE {
    text_ = icon::FixedStrCat<kFloatingPointNumberStringSize>(value);
  }

  // Returns the XML element's text. The buffer lives as long as this
  // element instance lives in case of numbers. In case of a string, the
  // buffer's life span depends on the buffer that was passed to `SetText()`.
  absl::string_view Text() const INTRINSIC_CHECK_REALTIME_SAFE;

  // Returns the parent XML element of this element.
  RtXmlElement* Parent() const INTRINSIC_CHECK_REALTIME_SAFE { return parent_; }

 private:
  // Creates a new xml element with name `name` and as a sub-element of
  // `parent`. All attributes and sub-elements will use memory from `generator`.
  // Therefore, `generator` must outlive this instance.
  explicit RtXmlElement(RealtimeXmlGenerator* generator
                            ABSL_ATTRIBUTE_LIFETIME_BOUND,
                        absl::string_view name,
                        RtXmlElement* parent ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : generator_(generator), name_(name), parent_(parent) {}

  static size_t NextId();

  RealtimeXmlGenerator* generator_;
  absl::string_view name_;
  // Variant to store text references or numbers serialized to string.
  // kFloatingPointerNumberStringSize characters is large enough for all numbers
  // serialized to string.
  std::variant<icon::FixedString<kFloatingPointNumberStringSize>,
               absl::string_view>
      text_;
  RtXmlElement* parent_ = nullptr;
  size_t unique_id_ = NextId();

  friend class RealtimeXmlGenerator;
};

}  // namespace intrinsic

#endif  // INTRINSIC_ICON_UTILS_XML_REALTIME_XML_GENERATOR_H_
