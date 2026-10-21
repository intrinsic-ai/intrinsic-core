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

#include "intrinsic/assets/id_utils.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/util/status/annotate.h"
#include "intrinsic/util/status/status_macros.h"
#include "re2/re2.h"
#include "re2/stringpiece.h"

namespace intrinsic::assets {
namespace {

// LINT.IfChange(regex)
static LazyRE2 kNameExpr = {R"((?P<name>^[a-z]([a-z0-9_]?[a-z0-9])*$))"};
static LazyRE2 kPackageExpr = {
    R"((?P<package>^([a-z]([a-z0-9_]?[a-z0-9])*\.)+([a-z]([a-z0-9_]?[a-z0-9])*)+$))"};
// Taken from semver.org.
static LazyRE2 kVersionExpr = {
    R"((?P<version>^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$))"};
static LazyRE2 kIdExpr = {
    R"((?P<id>^(?P<package>([a-z]([a-z0-9_]?[a-z0-9])*\.)+[a-z]([a-z0-9_]?[a-z0-9])*)\.(?P<name>[a-z]([a-z0-9_]?[a-z0-9])*)$))"};
static LazyRE2 kIdVersionExpr = {
    R"((?P<id_version>^(?P<id>(?P<package>([a-z]([a-z0-9_]?[a-z0-9])*\.)+[a-z]([a-z0-9_]?[a-z0-9])*)\.(?P<name>[a-z]([a-z0-9_]?[a-z0-9])*))\.(?P<version>(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?)$))"};

static LazyRE2 kLabelExpr = {R"(^[a-z]([a-z0-9-]*[a-z0-9])*$)"};
// LINT.ThenChange(
//  //intrinsic_sdk/intrinsic/assets/id_utils.py:regex,
//  //intrinsic/assets/id_utils.ts:regex,
//  //intrinsic_sdk/intrinsic/assets/idutils.go:regex
// )

// Verifies that the specified string matches the specified regex pattern.
absl::Status ValidateMatch(absl::string_view str, const RE2* re) {
  if (!RE2::FullMatch(str, *re)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "'%s' is not a valid %s.", str, re->CapturingGroupNames().at(1)));
  }
  return absl::OkStatus();
}

// Matches a string to a regex pattern and returns the specified named group.
absl::StatusOr<std::string> GetNamedMatch(absl::string_view str, const RE2* re,
                                          absl::string_view group) {
  int num_groups = re->NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> matches(num_groups);
  if (!re->Match(str, 0, str.size(), RE2::Anchor::UNANCHORED, matches.data(),
                 num_groups)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "'%s' is not a valid %s.", str, re->CapturingGroupNames().at(1)));
  }

  auto group_it = re->NamedCapturingGroups().find(std::string(group));
  if (group_it == re->NamedCapturingGroups().end()) {
    return absl::InternalError(absl::StrFormat("Unknown group '%s'.", group));
  }

  return std::string(matches.at(group_it->second));
}

}  // namespace

IdVersionParts::IdVersionParts(const std::vector<re2::StringPiece>* matches) {
  const std::map<std::string, int>& groups =
      kIdVersionExpr->NamedCapturingGroups();

  id_ = std::string(matches->at(groups.at("id")));
  id_version_ = std::string(matches->at(groups.at("id_version")));
  name_ = std::string(matches->at(groups.at("name")));
  package_ = std::string(matches->at(groups.at("package")));
  version_ = std::string(matches->at(groups.at("version")));
  version_build_metadata_ =
      std::string(matches->at(groups.at("buildmetadata")));
  version_major_ = std::string(matches->at(groups.at("major")));
  version_minor_ = std::string(matches->at(groups.at("minor")));
  version_patch_ = std::string(matches->at(groups.at("patch")));
  version_pre_release_ = std::string(matches->at(groups.at("prerelease")));
}

absl::StatusOr<IdVersionParts> IdVersionParts::Create(
    absl::string_view id_version) {
  int num_groups = kIdVersionExpr->NumberOfCapturingGroups() + 1;
  std::vector<re2::StringPiece> matches(num_groups);
  if (!kIdVersionExpr->Match(id_version, 0, id_version.size(),
                             RE2::Anchor::UNANCHORED, matches.data(),
                             num_groups)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("'%s' is not a valid id_version.", id_version));
  }

  return IdVersionParts(&matches);
}

absl::StatusOr<std::string> IdFrom(absl::string_view package,
                                   absl::string_view name) {
  absl::Status status = ValidatePackage(package);
  if (status.ok()) {
    status = ValidateName(name);
  }
  if (!status.ok()) {
    return AnnotateError(
        status, absl::StrFormat("Cannot create id from (%s, %s): %s", package,
                                name, status.message()));
  }

  return absl::StrFormat("%s.%s", package, name);
}

absl::StatusOr<intrinsic_proto::assets::Id> IdProtoFrom(
    absl::string_view package, absl::string_view name) {
  absl::Status status = ValidatePackage(package);
  if (status.ok()) {
    status = ValidateName(name);
  }
  if (!status.ok()) {
    return AnnotateError(
        status, absl::StrFormat("Cannot create Id from (%s, %s): %s", package,
                                name, status.message()));
  }

  intrinsic_proto::assets::Id id;
  id.set_package(package);
  id.set_name(name);

  return id;
}

absl::StatusOr<std::string> IdFromProto(intrinsic_proto::assets::Id id) {
  return IdFrom(id.package(), id.name());
}

absl::StatusOr<std::string> IdVersionFrom(absl::string_view package,
                                          absl::string_view name,
                                          absl::string_view version) {
  INTR_ASSIGN_OR_RETURN(std::string id, IdFrom(package, name));

  absl::Status status = ValidateVersion(version);
  if (!status.ok()) {
    return AnnotateError(
        status,
        absl::StrFormat("Cannot create id_version from (%s, %s, %s): %s",
                        package, name, version, status.message()));
  }

  return absl::StrFormat("%s.%s", id, version);
}

absl::StatusOr<intrinsic_proto::assets::IdVersion> IdVersionProtoFrom(
    absl::string_view package, absl::string_view name,
    absl::string_view version) {
  intrinsic_proto::assets::IdVersion id_version;

  INTR_ASSIGN_OR_RETURN(*id_version.mutable_id(), IdProtoFrom(package, name));

  absl::Status status = ValidateVersion(version);
  if (!status.ok()) {
    return AnnotateError(
        status, absl::StrFormat("Cannot create IdVersion from (%s, %s, %s): %s",
                                package, name, version, status.message()));
  }
  id_version.set_version(version);

  return id_version;
}

absl::StatusOr<std::string> IdVersionFromProto(
    intrinsic_proto::assets::IdVersion id_version) {
  return IdVersionFrom(id_version.id().package(), id_version.id().name(),
                       id_version.version());
}

absl::StatusOr<std::string> NameFrom(absl::string_view id) {
  absl::StatusOr<std::string> name_or_status =
      GetNamedMatch(id, kIdVersionExpr.get(), /*group=*/"name");
  if (name_or_status.ok()) {
    return *name_or_status;
  }

  return GetNamedMatch(id, kIdExpr.get(), /*group=*/"name");
}

absl::StatusOr<std::string> PackageFrom(absl::string_view id) {
  absl::StatusOr<std::string> package_or_status =
      GetNamedMatch(id, kIdVersionExpr.get(), /*group=*/"package");
  if (package_or_status.ok()) {
    return *package_or_status;
  }

  return GetNamedMatch(id, kIdExpr.get(), /*group=*/"package");
}

absl::StatusOr<std::string> VersionFrom(absl::string_view id_version) {
  return GetNamedMatch(id_version, kIdVersionExpr.get(), /*group=*/"version");
}

absl::StatusOr<std::string> RemoveVersionFrom(absl::string_view id) {
  auto id_or_status = GetNamedMatch(id, kIdVersionExpr.get(), /*group=*/"id");
  if (id_or_status.ok()) {
    return *id_or_status;
  }

  INTR_RETURN_IF_ERROR(ValidateId(id));
  return std::string(id);
}

bool IsId(absl::string_view id) { return ValidateId(id).ok(); }

bool IsIdVersion(absl::string_view id_version) {
  return ValidateIdVersion(id_version).ok();
}

bool IsName(absl::string_view name) { return ValidateName(name).ok(); }

bool IsPackage(absl::string_view package) {
  return ValidatePackage(package).ok();
}

bool IsVersion(absl::string_view version) {
  return ValidateVersion(version).ok();
}

absl::Status ValidateId(absl::string_view id) {
  return ValidateMatch(id, kIdExpr.get());
}

absl::Status ValidateIdVersion(absl::string_view id_version) {
  return ValidateMatch(id_version, kIdVersionExpr.get());
}

absl::Status ValidateName(absl::string_view name) {
  return ValidateMatch(name, kNameExpr.get());
}

absl::Status ValidatePackage(absl::string_view package) {
  return ValidateMatch(package, kPackageExpr.get());
}

absl::Status ValidateVersion(absl::string_view version) {
  return ValidateMatch(version, kVersionExpr.get());
}

std::string ParentFromPackage(absl::string_view package) {
  std::vector<absl::string_view> v = absl::StrSplit(package, '.');
  if (v.size() < 3) {
    return "";
  }

  std::size_t idx = package.find_last_of('.');
  return std::string(package.substr(0, idx));
}

absl::StatusOr<std::string> ToLabel(absl::string_view s) {
  for (auto& offender : std::vector<absl::string_view>{"-", "_.", "._", "__"}) {
    if (absl::StrContains(s, offender)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Cannot convert '%s' into a label (contains '%s')", s, offender));
    }
  }

  std::string label = absl::StrReplaceAll(s, {{"_", "-"}, {".", "--"}});

  if (!RE2::FullMatch(label, *kLabelExpr)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Cannot convert '%s' into a label", s));
  }

  return label;
}

std::string FromLabel(absl::string_view label) {
  return absl::StrReplaceAll(label, {{"--", "."}, {"-", "_"}});
}

}  // namespace intrinsic::assets
