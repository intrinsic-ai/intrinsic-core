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

#include "intrinsic/util/proto_time.h"

#include <algorithm>
#include <cstdint>
#include <ctime>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"

namespace intrinsic {

namespace {

// Earliest time that can be represented by a google::protobuf::Timestamp.
// Corresponds to 0001-01-01T00:00:00Z.
//
// See google/protobuf/timestamp.proto.
constexpr timespec kMinProtoTimestamp{.tv_sec = -62135596800, .tv_nsec = 0};
// Latest time that can be represented by a google::protobuf::Timestamp.
// Corresponds to 9999-12-31T23:59:59.999999999Z.
//
// See google/protobuf/timestamp.proto.
constexpr timespec kMaxProtoTimestamp{.tv_sec = 253402300799,
                                      .tv_nsec = 999999999};

// Least duration that can be represented by a google::protobuf::Duration.
// Corresponds to -10000 years.
//
// See google/protobuf/duration.proto.
constexpr timespec kMinProtoDuration{.tv_sec = -315576000000,
                                     .tv_nsec = -999999999};
// Greatest duration that can be represented by a google::protobuf::Duration.
// Corresponds to +10000 years.
//
// See google/protobuf/duration.proto.
constexpr timespec kMaxProtoDuration{.tv_sec = 315576000000,
                                     .tv_nsec = 999999999};

// Validate the values of a google::protobuf::Duration.
// Requirements documented in google/protobuf/duration.proto.
absl::Status ValidateProtoDurationMembers(int64_t sec, int32_t ns) {
  if (sec < kMinProtoDuration.tv_sec || sec > kMaxProtoDuration.tv_sec) {
    return absl::InvalidArgumentError(
        absl::StrCat("Duration seconds out of range: `", sec, "`. Must be in [",
                     kMinProtoDuration.tv_sec, ", ", kMaxProtoDuration.tv_sec,
                     "], i.e. [-10000 years, +10000 years]"));
  }
  if (ns < kMinProtoDuration.tv_nsec || ns > kMaxProtoDuration.tv_nsec) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Duration nanos out of range: `", ns, "`. Must be in [",
        kMinProtoDuration.tv_nsec, ", ", kMaxProtoDuration.tv_nsec, "]"));
  }
  if ((sec < 0 && ns > 0) || (sec > 0 && ns < 0)) {
    return absl::InvalidArgumentError("sign mismatch");
  }
  return absl::OkStatus();
}

// Validate the values of a google::protobuf::Timestamp.
// Requirements documented in google/protobuf/timestamp.proto.
absl::Status ValidateProtoTimestampMembers(int64_t sec, int32_t ns) {
  // sec must be [0001-01-01T00:00:00Z, 9999-12-31T23:59:59.999999999Z]
  if (sec < kMinProtoTimestamp.tv_sec || sec > kMaxProtoTimestamp.tv_sec) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Timestamp seconds out of range: `", sec, "`. Must be in [",
        kMinProtoTimestamp.tv_sec, ", ", kMaxProtoTimestamp.tv_sec, "]"));
  }
  if (ns < kMinProtoTimestamp.tv_nsec || ns > kMaxProtoTimestamp.tv_nsec) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Timestamp nanos out of range: `", ns, "`. Must be in [",
        kMinProtoTimestamp.tv_nsec, ", ", kMaxProtoTimestamp.tv_nsec, "]"));
  }
  return absl::OkStatus();
}

}  // namespace

google::protobuf::Timestamp MinTimestamp() {
  google::protobuf::Timestamp timestamp;
  timestamp.set_seconds(kMinProtoTimestamp.tv_sec);
  timestamp.set_nanos(kMinProtoTimestamp.tv_nsec);
  return timestamp;
}

google::protobuf::Timestamp MaxTimestamp() {
  google::protobuf::Timestamp timestamp;
  timestamp.set_seconds(kMaxProtoTimestamp.tv_sec);
  timestamp.set_nanos(kMaxProtoTimestamp.tv_nsec);
  return timestamp;
}

google::protobuf::Timestamp GetCurrentTimeProto() {
  absl::StatusOr<google::protobuf::Timestamp> time = FromAbslTime(absl::Now());
  if (!time.ok()) {
    return google::protobuf::Timestamp();
  }
  return *time;
}

/// google::protobuf::Timestamp <=> absl::Time.

absl::Status FromAbslTime(absl::Time time,
                          google::protobuf::Timestamp* timestamp) {
  int64_t s = absl::ToUnixSeconds(time);
  int32_t ns = (time - absl::FromUnixSeconds(s)) / absl::Nanoseconds(1);

  if (absl::Status status = ValidateProtoTimestampMembers(s, ns);
      !status.ok()) {
    return status;
  }

  timestamp->set_seconds(s);
  timestamp->set_nanos(ns);

  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Timestamp> FromAbslTime(absl::Time time) {
  google::protobuf::Timestamp timestamp;
  if (absl::Status status = FromAbslTime(time, &timestamp); !status.ok()) {
    return status;
  }
  return timestamp;
}

google::protobuf::Timestamp FromAbslTimeClampToValidRange(absl::Time time) {
  time = std::clamp(time, absl::TimeFromTimespec(kMinProtoTimestamp),
                    absl::TimeFromTimespec(kMaxProtoTimestamp));
  int64_t s = absl::ToUnixSeconds(time);
  int32_t ns = (time - absl::FromUnixSeconds(s)) / absl::Nanoseconds(1);

  google::protobuf::Timestamp out;
  out.set_seconds(s);
  out.set_nanos(ns);
  return out;
}

absl::StatusOr<absl::Time> ToAbslTime(
    const google::protobuf::Timestamp& proto) {
  if (absl::Status status =
          ValidateProtoTimestampMembers(proto.seconds(), proto.nanos());
      !status.ok()) {
    return status;
  }
  return ToAbslTimeNoValidation(proto);
}

namespace {
// Validate the values of a builtin_interfaces::msg::pb::jazzy::Time.
absl::Status ValidateROSTimeMembers(int32_t sec, int32_t ns) {
  // We don't need to validate sec, since the proto can represent the full
  // range of int32_t.

  constexpr int32_t kMinNs = 0;
  constexpr int32_t kMaxNs = 1000000000;

  if (ns < kMinNs || ns >= kMaxNs) {
    return absl::InvalidArgumentError(
        absl::StrCat("Timestamp nanos out of range: `", ns, "`. Must be in [",
                     kMinNs, ", ", kMaxNs, "]"));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<absl::Time> ToAbslTime(
    const builtin_interfaces::msg::pb::jazzy::Time& proto) {
  if (absl::Status status =
          ValidateROSTimeMembers(proto.sec(), proto.nanosec());
      !status.ok()) {
    return status;
  }
  return ToAbslTimeNoValidation(proto);
}

absl::Time ToAbslTimeNoValidation(const google::protobuf::Timestamp& proto) {
  return absl::FromUnixSeconds(proto.seconds()) +
         absl::Nanoseconds(proto.nanos());
}

absl::Time ToAbslTimeNoValidation(
    const builtin_interfaces::msg::pb::jazzy::Time& proto) {
  return absl::FromUnixSeconds(proto.sec()) +
         absl::Nanoseconds(proto.nanosec());
}

/// google::protobuf::Timestamp <=> floating-point time quantity.

absl::Status FromUnixDoubleNanos(double ns,
                                 google::protobuf::Timestamp* timestamp) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  return FromAbslTime(absl::UnixEpoch() + absl::Nanoseconds(ns), timestamp);
}

absl::Status FromUnixDoubleMicros(double us,
                                  google::protobuf::Timestamp* timestamp) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  return FromAbslTime(absl::UnixEpoch() + absl::Microseconds(us), timestamp);
}

absl::Status FromUnixDoubleMillis(double ms,
                                  google::protobuf::Timestamp* timestamp) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  return FromAbslTime(absl::UnixEpoch() + absl::Milliseconds(ms), timestamp);
}

absl::Status FromUnixDoubleSeconds(double s,
                                   google::protobuf::Timestamp* timestamp) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  return FromAbslTime(absl::UnixEpoch() + absl::Seconds(s), timestamp);
}

absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleNanos(double ns) {
  google::protobuf::Timestamp timestamp;
  if (absl::Status status = FromUnixDoubleNanos(ns, &timestamp); !status.ok()) {
    return status;
  }
  return timestamp;
}

absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleMicros(double us) {
  google::protobuf::Timestamp timestamp;
  if (absl::Status status = FromUnixDoubleMicros(us, &timestamp);
      !status.ok()) {
    return status;
  }
  return timestamp;
}

absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleMillis(double ms) {
  google::protobuf::Timestamp timestamp;
  if (absl::Status status = FromUnixDoubleMillis(ms, &timestamp);
      !status.ok()) {
    return status;
  }
  return timestamp;
}

absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleSeconds(double s) {
  google::protobuf::Timestamp timestamp;
  if (absl::Status status = FromUnixDoubleSeconds(s, &timestamp);
      !status.ok()) {
    return status;
  }
  return timestamp;
}

absl::StatusOr<double> ToUnixDoubleNanos(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::StatusOr<absl::Time> time = ToAbslTime(proto);
  if (!time.ok()) {
    return time.status();
  }
  absl::Duration d = *time - absl::UnixEpoch();
  return absl::ToDoubleNanoseconds(d);
}

absl::StatusOr<double> ToUnixDoubleMicros(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::StatusOr<absl::Time> time = ToAbslTime(proto);
  if (!time.ok()) {
    return time.status();
  }
  absl::Duration d = *time - absl::UnixEpoch();
  return absl::ToDoubleMicroseconds(d);
}

absl::StatusOr<double> ToUnixDoubleMillis(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::StatusOr<absl::Time> time = ToAbslTime(proto);
  if (!time.ok()) {
    return time.status();
  }
  absl::Duration d = *time - absl::UnixEpoch();
  return absl::ToDoubleMilliseconds(d);
}

absl::StatusOr<double> ToUnixDoubleSeconds(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::StatusOr<absl::Time> time = ToAbslTime(proto);
  if (!time.ok()) {
    return time.status();
  }
  absl::Duration d = *time - absl::UnixEpoch();
  return absl::ToDoubleSeconds(d);
}

double ToUnixDoubleNanosNoValidation(const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::Time time = ToAbslTimeNoValidation(proto);
  absl::Duration d = time - absl::UnixEpoch();
  return absl::ToDoubleNanoseconds(d);
}

double ToUnixDoubleMicrosNoValidation(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::Time time = ToAbslTimeNoValidation(proto);
  absl::Duration d = time - absl::UnixEpoch();
  return absl::ToDoubleMicroseconds(d);
}

double ToUnixDoubleMillisNoValidation(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::Time time = ToAbslTimeNoValidation(proto);
  absl::Duration d = time - absl::UnixEpoch();
  return absl::ToDoubleMilliseconds(d);
}

double ToUnixDoubleSecondsNoValidation(
    const google::protobuf::Timestamp& proto) {
  // We rely on absl to handle edge cases for us (e.g. fractional nanos).
  //
  // The absl types do not result in precision loss, so we defer precision-loss
  // only to the conversion to the floating-point type.
  absl::Time time = ToAbslTimeNoValidation(proto);
  absl::Duration d = time - absl::UnixEpoch();
  return absl::ToDoubleSeconds(d);
}

/// google::protobuf::Duration <=> absl::Duration.

absl::StatusOr<google::protobuf::Duration> FromAbslDuration(absl::Duration d) {
  google::protobuf::Duration proto;
  if (absl::Status status = FromAbslDuration(d, &proto); !status.ok()) {
    return status;
  }
  return proto;
}

absl::Status FromAbslDuration(absl::Duration d,
                              google::protobuf::Duration* proto) {
  // s and n may both be negative, per the Duration proto spec.
  const int64_t s = absl::IDivDuration(d, absl::Seconds(1), &d);
  const int64_t ns = absl::IDivDuration(d, absl::Nanoseconds(1), &d);

  if (absl::Status status = ValidateProtoDurationMembers(s, ns); !status.ok()) {
    return status;
  }

  proto->set_seconds(s);
  proto->set_nanos(ns);

  return absl::OkStatus();
}

absl::StatusOr<absl::Duration> ToAbslDuration(
    const google::protobuf::Duration& proto) {
  if (absl::Status status =
          ValidateProtoDurationMembers(proto.seconds(), proto.nanos());
      !status.ok()) {
    return status;
  }
  return ToAbslDurationNoValidation(proto);
}

absl::StatusOr<absl::Duration> ToAbslDurationRejectNegative(
    const google::protobuf::Duration& proto) {
  absl::StatusOr<absl::Duration> duration = ToAbslDuration(proto);
  if (!duration.ok()) {
    return duration.status();
  }
  if (*duration < absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Duration is negative, must be non-negative: `",
                     absl::ToInt64Nanoseconds(*duration), "` ns."));
  }
  return *duration;
}

absl::Duration ToAbslDurationNoValidation(
    const google::protobuf::Duration& proto) {
  return absl::Seconds(proto.seconds()) + absl::Nanoseconds(proto.nanos());
}

}  // namespace intrinsic
