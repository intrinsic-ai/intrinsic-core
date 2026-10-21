# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Provides utilities for working with asset ids."""

from __future__ import annotations

import dataclasses
import re

from intrinsic.assets.proto import id_pb2

# LINT.IfChange(regex)

# These patterns differ from the C++ and Go versions in order to optimize
# pattern matching in Python. Simple matching on the patterns used in the other
# languages can be incredibly slow in Python (i.e., on the order of seconds for
# some single strings).

# The following match package, name, and id, respectively, except that:
# 1) They don't care about multiple underscores in a row.
# 2) They don't verify that package parts and names don't end with an
#    underscore.
# Matching should be done with _match_package, _match_name, and _match_id
# instead of these raw patterns.
_ALMOST_PACKAGE_PATTERN = re.compile(
    r"(?P<package>^([a-z][a-z0-9_]*\.)+[a-z][a-z0-9_]*$)"
)
_ALMOST_NAME_PATTERN = re.compile(r"(?P<name>^[a-z][a-z0-9_]*$)")
_ALMOST_ID_PATTERN = re.compile(
    r"(?P<id>^(?P<package>([a-z][a-z0-9_]*\.)+[a-z][a-z0-9_]*)\.(?P<name>[a-z][a-z0-9_]*)$)"
)

# Taken from semver.org.
_VERSION_PATTERN_STR = r"(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?"
_VERSION_PATTERN = re.compile(rf"^{_VERSION_PATTERN_STR}$")

# The id part of this pattern is not validated. It should be validated via
# `validate_id`.
_COARSE_ID_VERSION_PATTERN = re.compile(
    rf"^(?P<maybe_id>.+)\.(?P<version>{_VERSION_PATTERN_STR})$"
)

_LABEL_PATTERN = re.compile(r"^([a-z])|([a-z][a-z0-9-]*[a-z0-9])$")
# LINT.ThenChange(
#  //intrinsic_sdk/intrinsic/assets/id_utils.cc:regex,
#  //intrinsic/assets/id_utils.ts:regex,
#  //intrinsic_sdk/intrinsic/assets/idutils.go:regex
# )


class IdValidationError(ValueError):
  """An id_version or part of one is not valid."""


class CannotConvertLabelError(ValueError):
  """A string cannot be converted into a label."""


# LINT.IfChange(interface)
@dataclasses.dataclass(frozen=True)
class IdVersionParts:
  """Provides access to all of the parts of an id_version.

  See is_id_version for details about id_version formatting.

  Attributes:
    id: The id part of the id_version.
    id_proto: The Id proto form of the id.
    id_version: The input id_version.
    id_version_proto: The IdVersion proto form of the id_version.
    name: The name part of the id_version.
    package: The package part of the id_version.
    version: The version of the id_version.
    version_build_metadata: The build metadata part of the version, if any.
    version_major: The major part of the version.
    version_minor: The minor part of the version.
    version_patch: The patch part of the version.
    version_pre_release: The pre-release part of the version, if any.
  """

  id: str
  id_version: str
  name: str
  package: str
  version: str
  version_build_metadata: str
  version_major: str
  version_minor: str
  version_patch: str
  version_pre_release: str

  @property
  def id_proto(self) -> id_pb2.Id:
    return id_pb2.Id(package=self.package, name=self.name)

  @property
  def id_version_proto(self) -> id_pb2.IdVersion:
    return id_pb2.IdVersion(id=self.id_proto, version=self.version)

  @classmethod
  def create(cls, id_version: str) -> IdVersionParts:
    """Creates a new IdVersionParts from an id_version string.

    Args:
      id_version: The target id_version.

    Returns:
      The IdVersionParts for the specified id_version.

    Raises:
      IdValidationError: If the input id_version is not valid.
    """
    coarse_match, id_match = _match_id_version_or_raise(id_version)
    build_metadata = coarse_match.group("buildmetadata")
    pre_release = coarse_match.group("prerelease")

    return cls(
        id=id_match.group("id"),
        id_version=id_version,
        name=id_match.group("name"),
        package=id_match.group("package"),
        version=coarse_match.group("version"),
        version_build_metadata="" if build_metadata is None else build_metadata,
        version_major=coarse_match.group("major"),
        version_minor=coarse_match.group("minor"),
        version_patch=coarse_match.group("patch"),
        version_pre_release="" if pre_release is None else pre_release,
    )


def id_from(package: str, name: str) -> str:
  """Creates an id from package and name strings.

  Ids are formatted as in is_id.

  Args:
    package: The asset package.
    name: The asset name.

  Returns:
    The asset id.

  Raises:
    IdValidationError: If the specified package or name are not valid.
  """
  try:
    validate_package(package)
    validate_name(name)
  except IdValidationError as err:
    raise IdValidationError(
        f"Cannot create id from ({package}, {name})"
    ) from err

  return f"{package}.{name}"


def id_proto_from(package: str, name: str) -> id_pb2.Id:
  """Creates an Id proto from package and name strings.

  Args:
    package: The asset package.
    name: The asset name.

  Returns:
    The Id proto.

  Raises:
    IdValidationError: If the specified package or name are not valid.
  """
  try:
    validate_package(package)
    validate_name(name)
  except IdValidationError as err:
    raise IdValidationError(
        f"Cannot create Id from ({package}, {name})"
    ) from err

  return id_pb2.Id(package=package, name=name)


def id_proto_from_string(id_string: str) -> id_pb2.Id:
  """Creates an Id proto from an ID string.

  Args:
    id_string: The ID string to convert.

  Returns:
    The Id proto.

  Raises:
    IdValidationError: If the specified package or name are not valid.
  """

  try:
    package = package_from(id_string)
    name = name_from(id_string)
  except IdValidationError as err:
    raise IdValidationError(f"Cannot create Id from '{id_string}'") from err

  return id_pb2.Id(package=package, name=name)


def id_from_proto(id: id_pb2.Id) -> str:  # pylint: disable=redefined-builtin
  """Creates an id string from an Id proto message.

  Ids are formatted as in is_id.

  Args:
    id: The Id proto to convert.

  Returns:
    The corresponding id string.

  Raises:
    IdValidationError: If the specified package or name are not valid.
  """
  return id_from(id.package, id.name)


def id_version_from(package: str, name: str, version: str) -> str:
  """Creates an id_version from package, name, and version strings.

  Id_versions are formatted as in is_id_version.

  Args:
    package: The asset package.
    name: The asset name.
    version: The asset version.

  Returns:
    The asset id_version.

  Raises:
    IdValidationError: If the specified package, name, or version are not valid.
  """
  try:
    validate_version(version)
  except IdValidationError as err:
    raise IdValidationError(
        f"Cannot create id_version from ({package}, {name}, {version})"
    ) from err

  return f"{id_from(package, name)}.{version}"


def id_version_proto_from(
    package: str, name: str, version: str
) -> id_pb2.IdVersion:
  """Creates an IdVersion proto from package, name, and version strings.

  Args:
    package: The asset package.
    name: The asset name.
    version: The asset version.

  Returns:
    The IdVersion proto.

  Raises:
    IdValidationError: If the specified package, name, or version are not valid.
  """
  try:
    validate_version(version)
  except IdValidationError as err:
    raise IdValidationError(
        f"Cannot create id_version from ({package}, {name}, {version})"
    ) from err

  return id_pb2.IdVersion(id=id_proto_from(package, name), version=version)


def id_version_from_proto(id_version: id_pb2.IdVersion) -> str:
  """Creates an id_version string from an IdVersion proto message.

  Id_versions are formatted as in is_id_version.

  Args:
    id_version: The IdVersion proto to convert.

  Returns:
    The corresponding id_version string.

  Raises:
    IdValidationError: If the specified package, name, or version are not valid.
  """
  return id_version_from(
      id_version.id.package, id_version.id.name, id_version.version
  )


def name_from(id: str) -> str:  # pylint: disable=redefined-builtin
  """Returns the name part of an id or id_version.

  Args:
    id: An id or id_version, as described in is_id and is_id_version,
      respectively.

  Returns:
    The package part of the id.

  Raises:
    IdValidationError: If the specified id is not valid.
  """
  match = _match_id(remove_version_from(id))
  if match is None:
    raise IdValidationError(f"{id!r} is not a valid id or id_version.")

  return match.group("name")


def package_from(id: str) -> str:  # pylint: disable=redefined-builtin
  """Returns the package part of an id or id_version.

  Args:
    id: An id or id_version, as described in is_id and is_id_version,
      respectively.

  Returns:
    The package part of the id.

  Raises:
    IdValidationError: If the specified id is not valid.
  """
  match = _match_id(remove_version_from(id))
  if match is None:
    raise IdValidationError(f"{id!r} is not a valid id or id_version.")

  return match.group("package")


def version_from(id_version: str) -> str:
  """Returns the version part of an id_version.

  Args:
    id_version: An id_version, as described in is_id_version.

  Returns:
    The version part of the id_version.

  Raises:
    IdValidationError: If the specified id_version is not valid.
  """
  coarse_match, _ = _match_id_version_or_raise(id_version)
  return coarse_match.group("version")


def remove_version_from(id: str) -> str:  # pylint: disable=redefined-builtin
  """Strips the version from `id` and returns the id substring.

  Args:
    id: An id or id_version, as described in is_id and is_id_version,
      respectively.

  Returns:
    The id part of the input with version, if any, stripped.

  Raises:
    IdValidationError: If the specified id is not valid.
  """
  try:
    _, id_match = _match_id_version_or_raise(id)
  except IdValidationError:
    validate_id(id)
    return id

  return id_match.group("id")


def is_id(id: str) -> bool:  # pylint: disable=redefined-builtin
  """Tests whether a string is a valid asset id.

  A valid id is formatted as "<package>.<name>", where `package` and `name` are
  formatted as described in is_package and is_name, respectively.

  Args:
    id: The string to test.

  Returns:
    True if `id` is a valid id.
  """
  return _match_id(id) is not None


def is_id_version(id_version: str) -> bool:
  """Tests whether a string is a valid asset id_version.

  A valid id_version is formatted as "<package>.<name>.<version>", where
  `package`, `name`, and `version` are formatted as described in is_package,
  is_name, and is_version, respectively.

  Args:
    id_version: The string to test.

  Returns:
    True if `id_version` is a valid id_version.
  """
  match = _COARSE_ID_VERSION_PATTERN.match(id_version)
  return match is not None and is_id(match.group("maybe_id"))


def is_name(name: str) -> bool:
  """Tests whether a string is a valid asset name.

  A valid name:
   - consists only of lower case alphanumeric characters and underscores;
   - begins with an alphabetic character;
   - ends with an alphanumeric character;
   - does not contain multiple underscores in a row.

  NOTE: Disallowing multiple underscores in a row enables underscores to be
  replaced with a hyphen (-) and periods to be replaced with two hyphens (--)
  in order to convert asset ids to labels (see `to_label`) without possibility
  of collisions.

  Args:
    name: The string to test.

  Returns:
    True if `name` is a valid name.
  """
  return _match_name(name) is not None


def is_package(package: str) -> bool:
  """Tests whether a string is a valid asset package.

  A valid package:
   - consists only of alphanumeric characters, underscores, and periods;
   - begins with an alphabetic character;
   - ends with an alphanumeric character;
   - contains at least one period;
   - precedes each period with an alphanumeric character;
   - follows each period with an alphabetic character;
   - does not contain multiple underscores in a row.

  NOTE: Disallowing multiple underscores in a row enables underscores to be
  replaced with a hyphen (-) and periods to be replaced with two hyphens (--)
  in order to convert asset ids to labels (see `to_label`) without possibility
  of collisions.

  Args:
    package: The string to test.

  Returns:
    True if `package` is a valid package.
  """
  return _match_package(package) is not None


def is_version(version: str) -> bool:
  """Tests whether a string is a valid asset version.

  A valid version is formatted as described by semver.org.

  Args:
    version: The string to test.

  Returns:
    True if `version` is a valid version.
  """
  return _VERSION_PATTERN.match(version) is not None


def validate_id(id: str) -> None:  # pylint: disable=redefined-builtin
  """Validates an id.

  A valid id is formatted as described in is_id.

  Args:
    id: The id to validate.

  Raises:
    IdValidationError: If the specified id is not valid.
  """
  if not is_id(id):
    raise IdValidationError(f"{id!r} is not a valid id.")


def validate_id_version(id_version: str) -> None:
  """Validates an id_version.

  A valid id_version is formatted as described in is_id_version.

  Args:
    id_version: The id_version to validate.

  Raises:
    IdValidationError: If the specified id_version is not valid.
  """
  if not is_id_version(id_version):
    raise IdValidationError(f"{id_version!r} is not a valid id_version.")


def validate_name(name: str) -> None:
  """Validates a name.

  A valid name is formatted as described in is_name.

  Args:
    name: The name to validate.

  Raises:
    IdValidationError: If the specified name is not valid.
  """
  if not is_name(name):
    raise IdValidationError(f"{name!r} is not a valid name.")


def validate_package(package: str) -> None:
  """Validates a package.

  A valid package is formatted as described in is_package.

  Args:
    package: The package to validate.

  Raises:
    IdValidationError: If the specified package is not valid.
  """
  if not is_package(package):
    raise IdValidationError(f"{package!r} is not a valid package.")


def validate_version(version: str) -> None:
  """Validates a version.

  A valid version is formatted as described in is_version.

  Args:
    version: The version to validate.

  Raises:
    IdValidationError: If the specified version is not valid.
  """
  if not is_version(version):
    raise IdValidationError(
        f"{version!r} is not a valid version, it must conform to SemVer 2"
        " (https://semver.org)."
    )


def parent_package_from(package: str) -> str:
  """Returns the parent package of the specified package.

  Returns an empty string if the package has no parent.

  NOTE: It does not validate the package.

  Args:
    package: The child package.

  Returns:
    The parent package, or an empty string if the child has no parent.
  """
  if package.count(".") < 2:
    return ""

  idx = package.rfind(".")
  return package[:idx]


def to_label(s: str) -> str:
  """Converts the input into a label.

  A label can be used as, e.g.:
    - a Kubernetes resource name
      (https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#names);
    - a SpiceDB id (https://authzed.com/docs).

  A label:
    - consists of only alphanumeric characters and hyphens (-);
    - begins with an alphabetic character;
    - ends with an alphanumeric character.

  This function will potentially apply two transformations to the input:
    - "." is converted to "--";
    - "_" is converted to "-".

  If the above transformations cannot convert the input into a label, an error
  is raised.

  In order to support reversible transformations (see `from_label`), an input
  cannot be converted if it contains any of the following substrings: "-", "_.",
  "._", "__".

  Args:
    s: The string to convert.

  Returns:
    The converted label.

  Raises:
    CannotConvertLabelError: If the input cannot be converted into a label.
  """
  for offender in ("-", "_.", "._", "__"):
    if offender in s:
      raise CannotConvertLabelError(
          f"Cannot convert {s!r} into a label (contains {offender!r})."
      )

  label = s.replace("_", "-").replace(".", "--")

  if _LABEL_PATTERN.fullmatch(label) is None:
    raise CannotConvertLabelError(f"Cannot convert {s!r} into a label.")

  return label


def from_label(label: str) -> str:
  """Recovers an input string previously passed to `to_label`.

  Args:
    label: An output from `to_label`.

  Returns:
    The recovered string.
  """
  return label.replace("--", ".").replace("-", "_")


# LINT.ThenChange(
#  //intrinsic_sdk/intrinsic/assets/id_utils.h:interface,
#  //intrinsic/assets/id_utils.ts:interface,
#  //intrinsic_sdk/intrinsic/assets/idutils.go:interface
# )


def _match_package(package: str) -> re.Match[str] | None:
  """Returns a regex Match for a package, or None for no match."""
  if "__" in package or "_." in package or package.endswith("_"):
    return None
  return _ALMOST_PACKAGE_PATTERN.match(package)


def _match_name(name: str) -> re.Match[str] | None:
  """Returns a regex Match for a name, or None for no match."""
  if "__" in name or name.endswith("_"):
    return None
  return _ALMOST_NAME_PATTERN.match(name)


def _match_id(id: str) -> re.Match[str] | None:  # pylint: disable=redefined-builtin
  """Returns a regex Match for an asset ID, or None for no match."""
  if "__" in id or "_." in id or id.endswith("_"):
    return None
  return _ALMOST_ID_PATTERN.match(id)


def _match_id_version_or_raise(
    id_version: str,
) -> tuple[re.Match[str], re.Match[str]]:
  """Returns regex Matches for an asset id_version, or raises for no match."""
  coarse_match = _COARSE_ID_VERSION_PATTERN.match(id_version)
  if coarse_match is None:
    raise IdValidationError(f"{id_version!r} is not a valid id_version.")

  id_match = _match_id(coarse_match.group("maybe_id"))
  if id_match is None:
    raise IdValidationError(f"{id_version!r} is not a valid id_version.")

  return coarse_match, id_match
