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

#ifndef INTRINSIC_UTIL_PROTO_TIME_H_
#define INTRINSIC_UTIL_PROTO_TIME_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg/time.pb.h"

namespace intrinsic {

/// google::protobuf::Timestamp.

// Returns the minimum valid timestamp.
google::protobuf::Timestamp MinTimestamp();

// Returns the maximum valid timestamp.
google::protobuf::Timestamp MaxTimestamp();

// Returns the current time as a google::protobuf::Timestamp.
google::protobuf::Timestamp GetCurrentTimeProto();

// Converts an absl::Time to a google::protobuf::Timestamp.
//
// Returns an error if either end of the mapping involves a timestamp that is
// outside the range allowed in `google/protobuf/timestamp.proto`.
absl::Status FromAbslTime(absl::Time time,
                          google::protobuf::Timestamp* timestamp);
absl::StatusOr<google::protobuf::Timestamp> FromAbslTime(absl::Time time);

// Converts a google::protobuf::Timestamp to an absl::Time.
//
// Returns an error if either end of the mapping involves a timestamp that is
// outside the range allowed in `google/protobuf/timestamp.proto`.
absl::StatusOr<absl::Time> ToAbslTime(const google::protobuf::Timestamp& proto);

// Converts a google::protobuf::Timestamp to an absl::Time, allowing the
// input to be outside the valid range of `google/protobuf/timestamp.proto`.
//
// This is because absl::Time supports a larger range of values than
// google::protobuf::Timestamp.
absl::Time ToAbslTimeNoValidation(const google::protobuf::Timestamp& proto);

// Converts an absl:Time to a google::protobuf::Timestamp, clamping to the valid
// time range allowed in `google/protobuf/timestamp.proto`.
//
// This allows absl::InfiniteFuture() and absl::InfinitePast() to be used.
//
// Note that roundtrip with
// `ToAbslTime(FromAbslTimeClampToValidRange(timestamp_proto))` may differ from
// `timestamp_proto`.
google::protobuf::Timestamp FromAbslTimeClampToValidRange(absl::Time time);

// Converts a floating-point time quantity since the Unix epoch to
// google::protobuf::Timestamp.
//
// A google::protobuf::Timestamp is defined in `google/protobuf/timestamp.proto`
// as a time quantity since Unix epoch, so this conversion makes sense.
//
// Returns an error if the input value would result in a timestamp that is
// outside the range allowed in `google/protobuf/timestamp.proto`.
absl::Status FromUnixDoubleNanos(double ns,
                                 google::protobuf::Timestamp* timestamp);
absl::Status FromUnixDoubleMicros(double us,
                                  google::protobuf::Timestamp* timestamp);
absl::Status FromUnixDoubleMillis(double ms,
                                  google::protobuf::Timestamp* timestamp);
absl::Status FromUnixDoubleSeconds(double s,
                                   google::protobuf::Timestamp* timestamp);
absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleNanos(double ns);
absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleMicros(double us);
absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleMillis(double ms);
absl::StatusOr<google::protobuf::Timestamp> FromUnixDoubleSeconds(double s);

// Converts a google::protobuf::Timestamp to a floating-point time quantity
// since the Unix epoch.
//
// NOTE: Precision loss may occur for large values.
//
// A google::protobuf::Timestamp is defined in `google/protobuf/timestamp.proto`
// as a time quantity since Unix epoch, so this conversion makes sense.
//
// Returns an error if the input timestamp has a value that is outside the range
// allowed in `google/protobuf/timestamp.proto`.
absl::StatusOr<double> ToUnixDoubleNanos(
    const google::protobuf::Timestamp& proto);
absl::StatusOr<double> ToUnixDoubleMicros(
    const google::protobuf::Timestamp& proto);
absl::StatusOr<double> ToUnixDoubleMillis(
    const google::protobuf::Timestamp& proto);
absl::StatusOr<double> ToUnixDoubleSeconds(
    const google::protobuf::Timestamp& proto);

// Converts a google::protobuf::Timestamp to a floating-point time quantity
// since the Unix epoch, allowing the input to be outside the valid range of
// `google/protobuf/timesamp.proto`.
//
// NOTE: Precision loss may occur for large values.
//
// A google::protobuf::Timestamp is defined in `google/protobuf/timestamp.proto`
// as a time quantity since Unix epoch, so this conversion makes sense.
//
// This is because double potentially supports a larger range of values than
// google::protobuf::Timestamp.
double ToUnixDoubleNanosNoValidation(const google::protobuf::Timestamp& proto);
double ToUnixDoubleMicrosNoValidation(const google::protobuf::Timestamp& proto);
double ToUnixDoubleMillisNoValidation(const google::protobuf::Timestamp& proto);
double ToUnixDoubleSecondsNoValidation(
    const google::protobuf::Timestamp& proto);

/// google::protobuf::Duration.

// Converts an absl::Duration to a google::protobuf::Duration.
//
// Returns an error if either end of the mapping involves a duration that is
// outside the range allowed in `google/protobuf/duration.proto`.
absl::StatusOr<google::protobuf::Duration> FromAbslDuration(absl::Duration d);
absl::Status FromAbslDuration(absl::Duration d,
                              google::protobuf::Duration* proto);

// Converts a google::protobuf::Duration to an absl::Duration.
//
// Returns an error if either end of the mapping involves a duration that is
// outside the range allowed in `google/protobuf/duration.proto`.
absl::StatusOr<absl::Duration> ToAbslDuration(
    const google::protobuf::Duration& proto);

// Converts a google::protobuf::Duration to an absl::Duration, rejecting
// negative durations.
//
// Returns an error if either end of the mapping involves a duration that is
// outside the range allowed in `google/protobuf/duration.proto`, or the
// duration is negative.
absl::StatusOr<absl::Duration> ToAbslDurationRejectNegative(
    const google::protobuf::Duration& proto);

// Converts a google::protobuf::Duration to an absl::Duration, allowing the
// input to be outside the valid range of `google/protobuf/duration.proto`.
//
// This is because absl::Duration supports a larger range of values than
// google::protobuf::Duration.
absl::Duration ToAbslDurationNoValidation(
    const google::protobuf::Duration& proto);

/// builtin_interfaces::msg::pb::jazzy::Time
// Converts a builtin_interfaces::msg::pb::jazzy::Time to an absl::Time.
//
// Returns an error if either end of the mapping involves a timestamp that is
// outside the range allowed in `google/protobuf/timestamp.proto`.
absl::StatusOr<absl::Time> ToAbslTime(
    const builtin_interfaces::msg::pb::jazzy::Time& proto);

// Converts a builtin_interfaces::msg::pb::jazzy::Time to an absl::Time,
// allowing the input to be outside the valid range of
// `google/protobuf/timestamp.proto`.
//
// This is because absl::Time supports a larger range of values than
// builtin_interfaces::msg::pb::jazzy::Time.
absl::Time ToAbslTimeNoValidation(
    const builtin_interfaces::msg::pb::jazzy::Time& proto);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_TIME_H_
