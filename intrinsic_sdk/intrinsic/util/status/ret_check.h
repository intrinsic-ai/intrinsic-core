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

#ifndef INTRINSIC_UTIL_STATUS_RET_CHECK_H_
#define INTRINSIC_UTIL_STATUS_RET_CHECK_H_

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/flags/declare.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/icon/release/source_location.h"  // IWYU pragma: keep
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace internal_status_macros_ret_check {

// Returns a StatusBuilder that corresponds to a `INTR_RET_CHECK` failure.
StatusBuilder RetCheckFailSlowPath(intrinsic::SourceLocation location);
StatusBuilder RetCheckFailSlowPath(intrinsic::SourceLocation location,
                                   const char* condition);
StatusBuilder RetCheckFailSlowPath(intrinsic::SourceLocation location,
                                   const char* condition,
                                   const absl::Status& s);

// Takes ownership of `condition`.  This API is a little quirky because it is
// designed to make use of the `::Check_*Impl` methods that implement `CHECK_*`
// and `DCHECK_*`.
StatusBuilder RetCheckFailSlowPath(intrinsic::SourceLocation location,
                                   std::string* condition);

inline StatusBuilder RetCheckImpl(const absl::Status& status,
                                  const char* condition,
                                  intrinsic::SourceLocation location) {
  if (ABSL_PREDICT_TRUE(status.ok())) {
    return StatusBuilder(absl::OkStatus(), location);
  }
  return RetCheckFailSlowPath(location, condition, status);
}

inline const absl::Status& AsStatus(const absl::Status& status) {
  return status;
}

template <typename T>
inline const absl::Status& AsStatus(const absl::StatusOr<T>& status_or) {
  return status_or.status();
}

// A helper class for formatting `expr (V1 vs. V2)` in a `INTR_RET_CHECK_XX`
// statement.  See `MakeCheckOpString` for sample usage.
class CheckOpMessageBuilder {
 public:
  // Inserts `exprtext` and ` (` to the stream.
  explicit CheckOpMessageBuilder(const char* exprtext);
  // Deletes `stream_`.
  ~CheckOpMessageBuilder();
  // For inserting the first variable.
  std::ostream* ForVar1() { return stream_; }
  // For inserting the second variable (adds an intermediate ` vs. `).
  std::ostream* ForVar2();
  // Get the result (inserts the closing `)`).
  std::string* NewString();

 private:
  std::ostringstream* stream_;
};

// This formats a value for a failing `INTR_RET_CHECK_XX` statement. Ordinarily,
// it uses the definition for `operator<<`, with a few special cases below.
template <typename T>
inline void MakeCheckOpValueString(std::ostream* os, const T& v) {
  (*os) << v;
}

// Overrides for char types provide readable values for unprintable characters.
void MakeCheckOpValueString(std::ostream* os, char v);
void MakeCheckOpValueString(std::ostream* os, signed char v);
void MakeCheckOpValueString(std::ostream* os, unsigned char v);

// We need an explicit specialization for `std::nullptr_t`.
void MakeCheckOpValueString(std::ostream* os, std::nullptr_t v);
void MakeCheckOpValueString(std::ostream* os, const char* v);
void MakeCheckOpValueString(std::ostream* os, const signed char* v);
void MakeCheckOpValueString(std::ostream* os, const unsigned char* v);
void MakeCheckOpValueString(std::ostream* os, char* v);
void MakeCheckOpValueString(std::ostream* os, signed char* v);
void MakeCheckOpValueString(std::ostream* os, unsigned char* v);

// Build the error message string.  Specify no inlining for code size.
template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2,
                               const char* exprtext) ABSL_ATTRIBUTE_NOINLINE;

template <typename T1, typename T2>
std::string* MakeCheckOpString(const T1& v1, const T2& v2,
                               const char* exprtext) {
  CheckOpMessageBuilder comb(exprtext);
  ::intrinsic::internal_status_macros_ret_check::MakeCheckOpValueString(
      comb.ForVar1(), v1);
  ::intrinsic::internal_status_macros_ret_check::MakeCheckOpValueString(
      comb.ForVar2(), v2);
  return comb.NewString();
}

// Helper functions for `INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP`
// macro.  The `(int, int)` specialization works around the issue that the
// compiler will not instantiate the template version of the function on values
// of unnamed enum type - see comment below.
#define INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(name, op)                     \
  template <typename T1, typename T2>                                          \
  inline std::string* name##Impl(const T1& v1, const T2& v2,                   \
                                 const char* exprtext) {                       \
    if (ABSL_PREDICT_TRUE(v1 op v2))                                           \
      return nullptr;                                                          \
    else                                                                       \
      return ::intrinsic::internal_status_macros_ret_check::MakeCheckOpString( \
          v1, v2, exprtext);                                                   \
  }                                                                            \
  inline std::string* name##Impl(int v1, int v2, const char* exprtext) {       \
    return ::intrinsic::internal_status_macros_ret_check::name##Impl<int,      \
                                                                     int>(     \
        v1, v2, exprtext);                                                     \
  }

INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_EQ, ==)
INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_NE, !=)
INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_LE, <=)
INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_LT, <)
INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_GE, >=)
INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(Check_GT, >)
#undef INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP

// `INTR_RET_CHECK_EQ` and friends want to pass their arguments by reference,
// however this winds up exposing lots of cases where people have defined and
// initialized static const data members but never declared them (i.e. in a .cc
// file), meaning they are not referenceable.  This function avoids that problem
// for integers (the most common cases) by overloading for every primitive
// integer type, even the ones we discourage, and returning them by value.
template <typename T>
inline const T& GetReferenceableValue(const T& t) {
  return t;
}
inline char GetReferenceableValue(char t) { return t; }
inline unsigned char GetReferenceableValue(unsigned char t) { return t; }
inline signed char GetReferenceableValue(signed char t) { return t; }
inline short GetReferenceableValue(short t) {  // NOLINT: runtime/int
  return t;
}
inline unsigned short GetReferenceableValue(  // NOLINT: runtime/int
    unsigned short t) {                       // NOLINT: runtime/int
  return t;
}
inline int GetReferenceableValue(int t) { return t; }
inline unsigned int GetReferenceableValue(unsigned int t) { return t; }
inline long GetReferenceableValue(long t)  // NOLINT: runtime/int
{
  return t;
}
inline unsigned long GetReferenceableValue(  // NOLINT: runtime/int
    unsigned long t) {                       // NOLINT: runtime/int
  return t;
}
inline long long GetReferenceableValue(long long t) {  // NOLINT: runtime/int
  return t;
}
inline unsigned long long GetReferenceableValue(  // NOLINT: runtime/int
    unsigned long long t) {                       // NOLINT: runtime/int
  return t;
}

}  // namespace internal_status_macros_ret_check
}  // namespace intrinsic

#define INTR_RET_CHECK(cond)                                                  \
  while (ABSL_PREDICT_FALSE(!(cond)))                                         \
  return ::intrinsic::internal_status_macros_ret_check::RetCheckFailSlowPath( \
      INTRINSIC_LOC, #cond)

#define INTR_RET_CHECK_FAIL()                                                 \
  return ::intrinsic::internal_status_macros_ret_check::RetCheckFailSlowPath( \
      INTRINSIC_LOC)

// Takes an expression returning absl::Status and asserts that the status is
// `ok()`.  If not, it returns an internal error.
//
// This is similar to `INTR_RETURN_IF_ERROR` in that it propagates errors.  The
// difference is that it follows the behavior of `INTR_RET_CHECK`, returning an
// internal error (wrapping the original error text), including the filename and
// line number, and logging a stack trace.
//
// This is appropriate to use to write an assertion that a function that returns
// `absl::Status` cannot fail, particularly when the error code itself should
// not be surfaced.
#define INTR_RET_CHECK_OK(status)                                          \
  INTR_RETURN_IF_ERROR(                                                    \
      ::intrinsic::internal_status_macros_ret_check::RetCheckImpl(         \
          ::intrinsic::internal_status_macros_ret_check::AsStatus(status), \
          #status, INTRINSIC_LOC))

#if defined(STATIC_ANALYSIS) || defined(PORTABLE_STATUS)
#define INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs, rhs) \
  INTR_RET_CHECK((lhs)op(rhs))
#else
#define INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(name, op, lhs, rhs)          \
  while (                                                                     \
      std::string* _result =                                                  \
          ::intrinsic::internal_status_macros_ret_check::Check_##name##Impl(  \
              ::intrinsic::internal_status_macros_ret_check::                 \
                  GetReferenceableValue(lhs),                                 \
              ::intrinsic::internal_status_macros_ret_check::                 \
                  GetReferenceableValue(rhs),                                 \
              #lhs " " #op " " #rhs))                                         \
  return ::intrinsic::internal_status_macros_ret_check::RetCheckFailSlowPath( \
      INTRINSIC_LOC, _result)
#endif

#define INTR_RET_CHECK_EQ(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(EQ, ==, lhs, rhs)
#define INTR_RET_CHECK_NE(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(NE, !=, lhs, rhs)
#define INTR_RET_CHECK_LE(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(LE, <=, lhs, rhs)
#define INTR_RET_CHECK_LT(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(LT, <, lhs, rhs)
#define INTR_RET_CHECK_GE(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(GE, >=, lhs, rhs)
#define INTR_RET_CHECK_GT(lhs, rhs) \
  INTR_COMMON_MACROS_INTERNAL_RET_CHECK_OP(GT, >, lhs, rhs)

#endif  // INTRINSIC_UTIL_STATUS_RET_CHECK_H_
