// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef THIRD_PARTY_STRINGS_NUMBERS_SORT_H_
#define THIRD_PARTY_STRINGS_NUMBERS_SORT_H_

#include <string_view>

namespace intrinsic {


// This is a non-google3, minimal version of natural sorting in
// google3/strings/numbers.h, also available with Apache licenses e.g. in
// https://github.com/google/sling/blob/master/sling/string/numbers.h and
// https://github.com/apache/impala/blob/master/be/src/gutil/strings/numbers.h.

// -----------------------------------------------------------------------------
// Natural Sort Order Utilities
// -----------------------------------------------------------------------------
//
// A Natural Sort Order sorts strings containing multi-digit characters ordered
// as if those digits were considered as one character, in numerical order.
// For example, "image9" is considered before "image10".  (This goes by the
// name `natsort()` in Go and PHP.)
//
// For more information, see https://en.wikipedia.org/wiki/Natural_sort_order.

// These are like std::less<string> and std::greater<string>, except when a
// run of digits is encountered at corresponding points in the two
// arguments.  Such digit strings are compared numerically instead
// of lexicographically.  Therefore if you sort by
// "autodigit_less", some machine names might get sorted as:
//    exaf1
//    exaf2
//    exaf10
// When using "strict" comparison (AutoDigitStrCmp with the strict flag
// set to true, or the strict version of the other functions),
// strings that represent equal numbers will not be considered equal if
// the string representations are not identical.  That is, "01" < "1" in
// strict mode, but "01" == "1" otherwise.

int AutoDigitStrCmp(std::string_view a, std::string_view b, bool strict);
inline bool AutoDigitLessThan(std::string_view a, std::string_view b) {
  return AutoDigitStrCmp(a, b, false) < 0;
}
inline bool StrictAutoDigitLessThan(std::string_view a, std::string_view b) {
  return AutoDigitStrCmp(a, b, true) < 0;
}

struct autodigit_less {
  // go/totw/144  
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/false) < 0;
  }
};

struct autodigit_greater {
  // go/totw/144  
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/false) > 0;
  }
};

struct strict_autodigit_less {
  // go/totw/144  
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/true) < 0;
  }
};

struct strict_autodigit_greater {
  // go/totw/144  
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const {
    return AutoDigitStrCmp(a, b, /*strict=*/true) > 0;
  }
};

}  // namespace intrinsic

#endif  // THIRD_PARTY_STRINGS_NUMBERS_SORT_H_
