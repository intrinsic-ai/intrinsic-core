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

#ifndef INTRINSIC_ASSETS_ID_UTILS_H_
#define INTRINSIC_ASSETS_ID_UTILS_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "re2/stringpiece.h"

namespace intrinsic::assets {

// LINT.IfChange(interface)

// Provides access to all of the parts of an id_version.
//
// See IsIdVersion for details about id_version formatting.
class IdVersionParts {
 public:
  // Creates a new IdVersionParts from an id_version string.
  static absl::StatusOr<IdVersionParts> Create(absl::string_view id_version);

  absl::string_view Id() const { return id_; }

  intrinsic_proto::assets::Id IdProto() const {
    intrinsic_proto::assets::Id id_proto;
    id_proto.set_package(package_);
    id_proto.set_name(name_);
    return id_proto;
  }

  absl::string_view IdVersion() const { return id_version_; }

  intrinsic_proto::assets::IdVersion IdVersionProto() const {
    intrinsic_proto::assets::IdVersion id_version_proto;
    *id_version_proto.mutable_id() = IdProto();
    id_version_proto.set_version(version_);
    return id_version_proto;
  }

  absl::string_view Name() const { return name_; }

  absl::string_view Package() const { return package_; }

  absl::string_view Version() const { return version_; }

  absl::string_view VersionBuildMetadata() const {
    return version_build_metadata_;
  }

  absl::string_view VersionMajor() const { return version_major_; }

  absl::string_view VersionMinor() const { return version_minor_; }

  absl::string_view VersionPatch() const { return version_patch_; }

  absl::string_view VersionPreRelease() const { return version_pre_release_; }

 private:
  explicit IdVersionParts(const std::vector<re2::StringPiece>* matches);

  std::string id_;
  std::string id_version_;
  std::string name_;
  std::string package_;
  std::string version_;
  std::string version_build_metadata_;
  std::string version_major_;
  std::string version_minor_;
  std::string version_patch_;
  std::string version_pre_release_;
};

// Creates an id from package and name strings.
//
// Ids are formatted as in IsId.
//
// Returns an error if `package` or `name` strings not valid.
absl::StatusOr<std::string> IdFrom(absl::string_view package,
                                   absl::string_view name);

// Creates an Id proto from package and name strings.
//
// Returns an error if `package` or `name` strings not valid.
absl::StatusOr<intrinsic_proto::assets::Id> IdProtoFrom(
    absl::string_view package, absl::string_view name);

// Creates an id string from an Id proto message.
//
// Ids are formatted as in IsId.
//
// Returns an error if `package` or `name` fields are not valid.
absl::StatusOr<std::string> IdFromProto(intrinsic_proto::assets::Id id);

// Creates an id_version from package, name, and version strings.
//
// Id_versions are formatted as in IsIdVersion.
//
// Returns an error if `package`, `name`, or `version` strings are not valid.
absl::StatusOr<std::string> IdVersionFrom(absl::string_view package,
                                          absl::string_view name,
                                          absl::string_view version);

// Creates an IdVersion proto from package, name, and version strings.
//
// Returns an error if `package`, `name`, or `version` strings are not valid.
absl::StatusOr<intrinsic_proto::assets::IdVersion> IdVersionProtoFrom(
    absl::string_view package, absl::string_view name,
    absl::string_view version);

// Creates an id_version string from an IdVersion proto message.
//
// Id_versions are formatted as in IsIdVersion.
//
// Returns an error if `package`, `name`, or `version` fields are not valid.
absl::StatusOr<std::string> IdVersionFromProto(
    intrinsic_proto::assets::IdVersion id_version);

// Returns the name part of an id or id_version.
//
// `id` must be formatted as an id or id_version, as described in IsId and
// IsIdVersion, respectively.
absl::StatusOr<std::string> NameFrom(absl::string_view id);

// Returns the package part of an id or id_version.
//
// `id` must be formatted as an id or id_version, as described in IsId and
// IsIdVersion, respectively.
absl::StatusOr<std::string> PackageFrom(absl::string_view id);

// Returns the version part of an id_version.
//
// `id_version` must be formatted as described in IsIdVersion.
absl::StatusOr<std::string> VersionFrom(absl::string_view id_version);

// RemoveVersionFrom strips the version from `id` and returns the id substring.
//
// `id` must be formatted as an id or id_version, as described in IsId and
// IsIdVersion, respectively.
//
// If there is no version information in the given `id`, the returned value will
// equal `id`.
absl::StatusOr<std::string> RemoveVersionFrom(absl::string_view id);

// Tests whether a string is a valid asset id.
//
// A valid id is formatted as "<package>.<name>", where `package` and `name` are
// formatted as described in IsPackage and IsName, respectively.
bool IsId(absl::string_view id);

// Tests whether a string is a valid asset id_version.
//
// A valid id_version is formatted as "<package>.<name>.<version>", where
// `package`, `name`, and `version` are formatted as described in IsPackage,
// IsName, and IsVersion, respectively.
bool IsIdVersion(absl::string_view id_version);

// Tests whether a string is a valid asset name.
//
// A valid name:
//  - consists only of lower case alphanumeric characters and underscores;
//  - begins with an alphabetic character;
//  - ends with an alphanumeric character;
//  - does not contain multiple underscores in a row.
//
// NOTE: Disallowing multiple underscores in a row enables underscores to be
// replaced with a hyphen (-) and periods to be replaced with two hyphens (--)
// in order to convert asset ids to kubernetes labels without possibility of
// collisions.
bool IsName(absl::string_view name);

// Tests whether a string is a valid asset package.
//
// A valid package:
//  - consists only of alphanumeric characters, underscores, and periods;
//  - begins with an alphabetic character;
//  - ends with an alphanumeric character;
//  - contains at least one period;
//  - precedes each period with an alphanumeric character;
//  - follows each period with an alphabetic character;
//  - does not contain multiple underscores in a row.
//
// NOTE: Disallowing multiple underscores in a row enables underscores to be
// replaced with a hyphen (-) and periods to be replaced with two hyphens (--)
// in order to convert asset ids to kubernetes labels without possibility of
// collisions.
bool IsPackage(absl::string_view package);

// Tests whether a string is a valid asset version.
//
// A valid version is formatted as described by semver.org.
bool IsVersion(absl::string_view version);

// Validates an id.
//
// A valid id is formatted as described in IsId.
//
// Returns an error if `id` is not valid.
absl::Status ValidateId(absl::string_view id);

// Validates an id_version.
//
// A valid id_version is formatted as described in IsIdVersion.
//
// Returns an error if `id_version` is not valid.
absl::Status ValidateIdVersion(absl::string_view id_version);

// Validates a name.
//
// A valid name is formatted as described in IsName.
//
// Returns an error if `name` is not valid.
absl::Status ValidateName(absl::string_view name);

// Validates a package.
//
// A valid package is formatted as described in IsPackage.
//
// Returns an error if `package` is not valid.
absl::Status ValidatePackage(absl::string_view package);

// Validates a version.
//
// A version is formatted as described in IsVersion.
//
// Returns an error if `version` is not valid.
absl::Status ValidateVersion(absl::string_view version);

// Returns the parent package of the specified package/
//
// Returns an empty string if the package has no parent.
//
// NOTE: It does not validate the package.
std::string ParentFromPackage(absl::string_view package);

// Converts the input into a label.
//
// A label can be used as, e.g.:
//   - a Kubernetes resource name
//     (https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#names);
//   - a SpiceDB id (https://authzed.com/docs).
//
// A label:
//   - consists of only alphanumeric characters and hyphens (-);
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character.
//
// This function will potentially apply two transformations to the input:
//   - "." is converted to "--";
//   - "_" is converted to "-".
//
// If the above transformations cannot convert the input into a label, an error
// status is returned.
//
// In order to support reversible transformations (see `FromLabel`), an input
// cannot be converted if it contains any of the following substrings: "-",
// "_.", "._", "__".
absl::StatusOr<std::string> ToLabel(absl::string_view s);

// Recovers an input string previously passed to ToLabel.
std::string FromLabel(absl::string_view label);

// LINT.ThenChange(
//  //intrinsic_sdk/intrinsic/assets/id_utils.py:interface,
//  //intrinsic/assets/id_utils.ts:interface,
//  //intrinsic_sdk/intrinsic/assets/idutils.go:interface
// )

}  // namespace intrinsic::assets

#endif  // INTRINSIC_ASSETS_ID_UTILS_H_
