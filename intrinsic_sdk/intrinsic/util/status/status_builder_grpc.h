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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_BUILDER_GRPC_H_
#define INTRINSIC_UTIL_STATUS_STATUS_BUILDER_GRPC_H_

#include <ostream>
#include <string_view>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/log/log_sink.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/time/time.h"
#include "grpcpp/support/status.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"

namespace intrinsic {

// This is a thin wrapper around StatusBuilder. The difference is that this
// wrapper supports implicit conversion to grpc::Status. It is used in the
// macros INTR_RETURN_IF_ERROR_GRPC and ASSIGN_OR_RETURN_GRPC. Generally you
// should not need to use this class directly. For method documentation check
// status_builder.h.

class ABSL_MUST_USE_RESULT StatusBuilderGrpc {
 public:
  explicit StatusBuilderGrpc(StatusBuilder&& builder)
      : builder_(std::move(builder)) {}

  explicit StatusBuilderGrpc(const absl::Status& status)
      : builder_(StatusBuilder(status)) {}

  explicit StatusBuilderGrpc(const grpc::Status& status)
      : builder_(StatusBuilder(ToAbslStatus(status))) {}

  StatusBuilderGrpc(const StatusBuilderGrpc&) = default;
  StatusBuilderGrpc& operator=(const StatusBuilderGrpc&) = default;
  StatusBuilderGrpc(StatusBuilderGrpc&&) = default;
  StatusBuilderGrpc& operator=(StatusBuilderGrpc&&) noexcept = default;

  StatusBuilderGrpc& SetPrepend() &;
  StatusBuilderGrpc&& SetPrepend() &&;

  StatusBuilderGrpc& SetAppend() &;
  StatusBuilderGrpc&& SetAppend() &&;

  StatusBuilderGrpc& SetNoLogging() &;
  StatusBuilderGrpc&& SetNoLogging() &&;

  StatusBuilderGrpc& Log(absl::LogSeverity level) &;
  StatusBuilderGrpc&& Log(absl::LogSeverity level) &&;
  StatusBuilderGrpc& LogError() & { return Log(absl::LogSeverity::kError); }
  StatusBuilderGrpc&& LogError() && { return std::move(LogError()); }
  StatusBuilderGrpc& LogWarning() & { return Log(absl::LogSeverity::kWarning); }
  StatusBuilderGrpc&& LogWarning() && { return std::move(LogWarning()); }
  StatusBuilderGrpc& LogInfo() & { return Log(absl::LogSeverity::kInfo); }
  StatusBuilderGrpc&& LogInfo() && { return std::move(LogInfo()); }

  StatusBuilderGrpc& LogEveryN(absl::LogSeverity level, int n) &;
  StatusBuilderGrpc&& LogEveryN(absl::LogSeverity level, int n) &&;

  StatusBuilderGrpc& LogEvery(absl::LogSeverity level, absl::Duration period) &;
  StatusBuilderGrpc&& LogEvery(absl::LogSeverity level,
                               absl::Duration period) &&;

  StatusBuilderGrpc& EmitStackTrace() &;
  StatusBuilderGrpc&& EmitStackTrace() &&;

  StatusBuilderGrpc& AlsoOutputToSink(absl::LogSink* sink) &;
  StatusBuilderGrpc&& AlsoOutputToSink(absl::LogSink* sink) &&;

  template <typename T>
  StatusBuilderGrpc& operator<<(const T& value) &;
  template <typename T>
  StatusBuilderGrpc&& operator<<(const T& value) &&;

  StatusBuilderGrpc& SetCode(absl::StatusCode code) &;
  StatusBuilderGrpc&& SetCode(absl::StatusCode code) &&;

  StatusBuilderGrpc& SetPayload(std::string_view type_url,
                                absl::Cord payload) &;
  StatusBuilderGrpc&& SetPayload(std::string_view type_url,
                                 absl::Cord payload) &&;

  template <typename Adaptor>
  auto With(
      Adaptor&& adaptor) & -> decltype(std::forward<Adaptor>(adaptor)(*this)) {
    return std::forward<Adaptor>(adaptor)(*this);
  }
  template <typename Adaptor>
  auto With(Adaptor&& adaptor) && -> decltype(std::forward<Adaptor>(adaptor)(
      std::move(*this))) {
    return std::forward<Adaptor>(adaptor)(std::move(*this));
  }

  bool ok() const;

  absl::StatusCode code() const;

  operator absl::Status() const&;  // NOLINT: Builder converts implicitly.
  operator absl::Status() &&;      // NOLINT: Builder converts implicitly.

  operator grpc::Status() const&;  // NOLINT: Builder converts implicitly.
  operator grpc::Status() &&;      // NOLINT: Builder converts implicitly.

  intrinsic::SourceLocation source_location() const;

 private:
  StatusBuilder builder_;
};

// Implicitly converts `builder` to `Status` and write it to `os`.
std::ostream& operator<<(std::ostream& os, const StatusBuilderGrpc& builder);
std::ostream& operator<<(std::ostream& os, StatusBuilderGrpc&& builder);

// Each of the functions below creates StatusBuilder with a canonical error.
// The error code of the StatusBuilder matches the name of the function.
StatusBuilderGrpc AbortedErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc AlreadyExistsErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc CancelledErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc DataLossErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc DeadlineExceededErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc FailedPreconditionErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc InternalErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc InvalidArgumentErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc NotFoundErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc OutOfRangeErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc PermissionDeniedErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc UnauthenticatedErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc ResourceExhaustedErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc UnavailableErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc UnimplementedErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);
StatusBuilderGrpc UnknownErrorBuilderGrpc(
    intrinsic::SourceLocation location = INTRINSIC_LOC);

// Implementation details follow; clients should ignore.

inline StatusBuilderGrpc& StatusBuilderGrpc::SetPrepend() & {
  builder_.SetPrepend();
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::SetPrepend() && {
  return std::move(SetPrepend());
}

inline StatusBuilderGrpc& StatusBuilderGrpc::SetAppend() & {
  builder_.SetAppend();
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::SetAppend() && {
  return std::move(SetAppend());
}

inline StatusBuilderGrpc& StatusBuilderGrpc::SetNoLogging() & {
  builder_.SetNoLogging();
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::SetNoLogging() && {
  return std::move(SetNoLogging());
}

inline StatusBuilderGrpc& StatusBuilderGrpc::Log(absl::LogSeverity level) & {
  builder_.Log(level);
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::Log(absl::LogSeverity level) && {
  return std::move(Log(level));
}

inline StatusBuilderGrpc& StatusBuilderGrpc::LogEveryN(absl::LogSeverity level,
                                                       int n) & {
  builder_.LogEveryN(level, n);
  return *this;
}
inline StatusBuilderGrpc&& StatusBuilderGrpc::LogEveryN(absl::LogSeverity level,
                                                        int n) && {
  return std::move(LogEveryN(level, n));
}

inline StatusBuilderGrpc& StatusBuilderGrpc::LogEvery(absl::LogSeverity level,
                                                      absl::Duration period) & {
  builder_.LogEvery(level, period);
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::LogEvery(
    absl::LogSeverity level, absl::Duration period) && {
  return std::move(LogEvery(level, period));
}

inline StatusBuilderGrpc& StatusBuilderGrpc::EmitStackTrace() & {
  builder_.EmitStackTrace();
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::EmitStackTrace() && {
  return std::move(EmitStackTrace());
}

inline StatusBuilderGrpc& StatusBuilderGrpc::AlsoOutputToSink(
    absl::LogSink* sink) & {
  builder_.AlsoOutputToSink(sink);
  return *this;
}
inline StatusBuilderGrpc&& StatusBuilderGrpc::AlsoOutputToSink(
    absl::LogSink* sink) && {
  return std::move(AlsoOutputToSink(sink));
}

template <typename T>
StatusBuilderGrpc& StatusBuilderGrpc::operator<<(const T& value) & {
  builder_ << value;
  return *this;
}

template <typename T>
StatusBuilderGrpc&& StatusBuilderGrpc::operator<<(const T& value) && {
  return std::move(operator<<(value));
}

inline StatusBuilderGrpc& StatusBuilderGrpc::SetCode(absl::StatusCode code) & {
  builder_.SetCode(code);
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::SetCode(
    absl::StatusCode code) && {
  return std::move(SetCode(code));
}

inline StatusBuilderGrpc& StatusBuilderGrpc::SetPayload(
    std::string_view type_url, absl::Cord payload) & {
  builder_.SetPayload(type_url, payload);
  return *this;
}

inline StatusBuilderGrpc&& StatusBuilderGrpc::SetPayload(
    std::string_view type_url, absl::Cord payload) && {
  return std::move(SetPayload(type_url, payload));
}

inline bool StatusBuilderGrpc::ok() const { return builder_.ok(); }

inline absl::StatusCode StatusBuilderGrpc::code() const {
  return builder_.code();
}

inline StatusBuilderGrpc::operator absl::Status() const& {
  return static_cast<absl::Status>(builder_);
}

inline StatusBuilderGrpc::operator absl::Status() && {
  return static_cast<absl::Status>(std::move(builder_));
}

inline StatusBuilderGrpc::operator grpc::Status() const& {
  return intrinsic::ToGrpcStatus(static_cast<absl::Status>(builder_));
}

inline StatusBuilderGrpc::operator grpc::Status() && {
  return intrinsic::ToGrpcStatus(
      static_cast<absl::Status>(std::move(builder_)));
}

inline intrinsic::SourceLocation StatusBuilderGrpc::source_location() const {
  return builder_.source_location();
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_STATUS_BUILDER_GRPC_H_
