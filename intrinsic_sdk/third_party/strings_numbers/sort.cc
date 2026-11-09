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

#include "third_party/strings_numbers/sort.h"

#include <cstddef>
#include <string_view>

#include "absl/strings/ascii.h"

namespace intrinsic {

// ----------------------------------------------------------------------
// AutoDigitStrCmp
// AutoDigitLessThan
// StrictAutoDigitLessThan
// autodigit_less
// autodigit_greater
// strict_autodigit_less
// strict_autodigit_greater
//    These are like std::less<string> and std::greater<string>, except when a
//    run of digits is encountered at corresponding points in the two
//    arguments.  Such digit strings are compared numerically instead
//    of lexicographically.  Therefore if you sort by
//    "autodigit_less", some machine names might get sorted as:
//        exaf1
//        exaf2
//        exaf10
//    When using "strict" comparison (AutoDigitStrCmp with the strict flag
//    set to true, or the strict version of the other functions),
//    strings that represent equal numbers will not be considered equal if
//    the string representations are not identical.  That is, "01" < "1" in
//    strict mode, but "01" == "1" otherwise.
// ----------------------------------------------------------------------

int AutoDigitStrCmp(std::string_view a, std::string_view b, bool strict) {
  size_t aindex = 0;
  size_t bindex = 0;
  while ((aindex < a.size()) && (bindex < b.size())) {
    if (absl::ascii_isdigit(a[aindex]) && absl::ascii_isdigit(b[bindex])) {
      // Compare runs of digits.  Instead of extracting numbers, we
      // just skip leading zeroes, and then get the run-lengths.  This
      // allows us to handle arbitrary precision numbers.  We remember
      // how many zeroes we found so that we can differentiate between
      // "1" and "01" in strict mode.

      // Skip leading zeroes, but remember how many we found
      size_t azeroes = aindex;
      size_t bzeroes = bindex;
      while ((aindex < a.size()) && (a[aindex] == '0')) aindex++;
      while ((bindex < b.size()) && (b[bindex] == '0')) bindex++;
      azeroes = aindex - azeroes;
      bzeroes = bindex - bzeroes;

      // Count digit lengths
      size_t astart = aindex;
      size_t bstart = bindex;
      while ((aindex < a.size()) && absl::ascii_isdigit(a[aindex])) aindex++;
      while ((bindex < b.size()) && absl::ascii_isdigit(b[bindex])) bindex++;
      if (aindex - astart < bindex - bstart) {
        // a has shorter run of digits: so smaller
        return -1;
      } else if (aindex - astart > bindex - bstart) {
        // a has longer run of digits: so larger
        return 1;
      } else {
        // Same lengths, so compare digit by digit
        for (size_t i = 0; i < aindex - astart; i++) {
          if (a[astart + i] < b[bstart + i]) {
            return -1;
          } else if (a[astart + i] > b[bstart + i]) {
            return 1;
          }
        }
        // Equal: did one have more leading zeroes?
        if (strict && azeroes != bzeroes) {
          if (azeroes > bzeroes) {
            // a has more leading zeroes: a < b
            return -1;
          } else {
            // b has more leading zeroes: a > b
            return 1;
          }
        }
        // Equal: so continue scanning
      }
    } else if (a[aindex] < b[bindex]) {
      return -1;
    } else if (a[aindex] > b[bindex]) {
      return 1;
    } else {
      aindex++;
      bindex++;
    }
  }

  if (aindex < a.size()) {
    // b is prefix of a
    return 1;
  } else if (bindex < b.size()) {
    // a is prefix of b
    return -1;
  } else {
    // a is equal to b
    return 0;
  }
}

}  // namespace intrinsic
