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

// Package idutils contains utilities for interacting with id strings for Assets.
//
// For example, it contains utilities to extract sub-components of the id strings, and check
// whether id strings, and their subcomponents (such as the version), are valid.
package idutils

import (
	"fmt"
	"regexp"
	"strings"

	"golang.org/x/exp/slices"

	idpb "intrinsic/assets/proto/id_go_proto"
)

const (
	// IDVersionURLRegex is a regex for HTTP handlers that captures all valid IDs and IDVersions.
	// It also captures some invalid IDVersions, but those can be invalidated by the handler function
	// so a validation error rather than a 404 can be returned.
	// For fully qualified regex according to go/intrinsic-assets-metadata, use idVersionRegex.
	IDVersionURLRegex = `[a-zA-Z0-9_\.\+\-]+`
)

var (
	// LINT.IfChange(regex)
	nameRegex    = regexp.MustCompile(`(?P<name>^[a-z]([a-z0-9_]?[a-z0-9])*$)`)
	packageRegex = regexp.MustCompile(`(?P<package>^([a-z]([a-z0-9_]?[a-z0-9])*\.)+([a-z]([a-z0-9_]?[a-z0-9])*)+$)`)
	// Taken from semver.org.
	versionRegex        = regexp.MustCompile(`(?P<version>^(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$)`)
	idRegex             = regexp.MustCompile(`(?P<id>^(?P<package>([a-z]([a-z0-9_]?[a-z0-9])*\.)+[a-z]([a-z0-9_]?[a-z0-9])*)\.(?P<name>[a-z]([a-z0-9_]?[a-z0-9])*)$)`)
	idVersionRegex      = regexp.MustCompile(`(?P<id_version>^(?P<id>(?P<package>([a-z]([a-z0-9_]?[a-z0-9])*\.)+[a-z]([a-z0-9_]?[a-z0-9])*)\.(?P<name>[a-z]([a-z0-9_]?[a-z0-9])*))\.(?P<version>(?P<major>0|[1-9]\d*)\.(?P<minor>0|[1-9]\d*)\.(?P<patch>0|[1-9]\d*)(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?(?:\+(?P<buildmetadata>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?)$)`)
	labelRegex          = regexp.MustCompile(`^[a-z]([a-z0-9\-]*[a-z0-9])*$`)
	labelFirstCharRegex = regexp.MustCompile(`^[a-z].*`)
	labelBadCharRegex   = regexp.MustCompile(`[^a-z0-9\-]`)
	labelLastCharRegex  = regexp.MustCompile(`.*[a-z0-9]$`)

	nonReleasedVersionRegex = regexp.MustCompile("\\+(?:sideloaded|inlined)")
	// LINT.ThenChange(
	//  //intrinsic_sdk/intrinsic/assets/id_utils.cc:regex,
	//  //intrinsic_sdk/intrinsic/assets/id_utils.py:regex,
	//  //intrinsic/assets/id_utils.ts:regex,
	//  //intrinsic/frontend/go/api/assets.go:regex
	// )
)

// getNamedMatches extracts named groups from a match of a string on a regex pattern.
func getNamedMatches(str string, re *regexp.Regexp, requested []string) (map[string]string, error) {
	groups := re.SubexpNames()
	submatches := re.FindStringSubmatch(str)
	if submatches == nil {
		return nil, fmt.Errorf("%q is not a valid %s", str, groups[1])
	}

	result := make(map[string]string)
	for _, group := range requested {
		idx := slices.Index(groups, group)
		if idx == -1 {
			return nil, fmt.Errorf("unknown group: %q (groups: %v)", group, groups)
		}
		result[group] = submatches[idx]
	}

	return result, nil
}

// getNamedMatch extracts the named group from a match of a string on a regex pattern.
func getNamedMatch(str string, re *regexp.Regexp, group string) (string, error) {
	matches, err := getNamedMatches(str, re, []string{group})
	if err != nil {
		return "", err
	}
	return matches[group], nil
}

// IDVersionParts provides access to all of the parts of an id_version.
//
// See IsIDVersion for details about id_version formatting.
type IDVersionParts struct {
	id                   string
	idVersion            string
	name                 string
	pkg                  string
	version              string
	versionBuildMetadata string
	versionMajor         string
	versionMinor         string
	versionPatch         string
	versionPreRelease    string
}

// NewIDVersionParts creates a new IDVersionParts from an id_version string.
func NewIDVersionParts(idVersion string) (*IDVersionParts, error) {
	submatches := idVersionRegex.FindStringSubmatch(idVersion)
	if submatches == nil {
		return nil, fmt.Errorf("%q is not a valid id_version", idVersion)
	}

	groups := idVersionRegex.SubexpNames()

	return &IDVersionParts{
		id:                   submatches[slices.Index(groups, "id")],
		idVersion:            idVersion,
		name:                 submatches[slices.Index(groups, "name")],
		pkg:                  submatches[slices.Index(groups, "package")],
		version:              submatches[slices.Index(groups, "version")],
		versionBuildMetadata: submatches[slices.Index(groups, "buildmetadata")],
		versionMajor:         submatches[slices.Index(groups, "major")],
		versionMinor:         submatches[slices.Index(groups, "minor")],
		versionPatch:         submatches[slices.Index(groups, "patch")],
		versionPreRelease:    submatches[slices.Index(groups, "prerelease")],
	}, nil
}

// NewIDVersionPartsFromProto creates a new IDVersionParts from an IdVersion proto.
func NewIDVersionPartsFromProto(idVersion *idpb.IdVersion) (*IDVersionParts, error) {
	return NewIDVersionParts(IDVersionFromProtoUnchecked(idVersion))
}

// ID returns the id part of id_version.
func (p *IDVersionParts) ID() string {
	return p.id
}

// IDProto returns the id proto part of id_version.
func (p *IDVersionParts) IDProto() *idpb.Id {
	return &idpb.Id{Package: p.pkg, Name: p.name}
}

// IDVersion returns the id_version.
func (p *IDVersionParts) IDVersion() string {
	return p.idVersion
}

// IDVersionProto returns the id_version proto.
func (p *IDVersionParts) IDVersionProto() *idpb.IdVersion {
	return &idpb.IdVersion{Id: p.IDProto(), Version: p.Version()}
}

// Name returns the name part of id_version.
func (p *IDVersionParts) Name() string {
	return p.name
}

// Package returns the package part of id_version.
func (p *IDVersionParts) Package() string {
	return p.pkg
}

// Version returns the version part of id_version.
func (p *IDVersionParts) Version() string {
	return p.version
}

// VersionBuildMetadata returns the build metadata part of id_version.
func (p *IDVersionParts) VersionBuildMetadata() string {
	return p.versionBuildMetadata
}

// VersionMajor returns the major part of the version.
func (p *IDVersionParts) VersionMajor() string {
	return p.versionMajor
}

// VersionMinor returns the minor part of the version.
func (p *IDVersionParts) VersionMinor() string {
	return p.versionMinor
}

// VersionPatch returns the patch part of the version.
func (p *IDVersionParts) VersionPatch() string {
	return p.versionPatch
}

// VersionPreRelease returns the pre-release part of the version, if any.
func (p *IDVersionParts) VersionPreRelease() string {
	return p.versionPreRelease
}

// LINT.IfChange(interface)

// IDFrom creates an id from package and name strings.
//
// Ids are formatted as in IsID.
//
// Returns an error if `pkg` or `name` strings not valid.
func IDFrom(pkg string, name string) (string, error) {
	err := ValidatePackage(pkg)
	if err == nil {
		err = ValidateName(name)
	}
	if err != nil {
		return "", fmt.Errorf("cannot create id from (%q, %q): %v", pkg, name, err)
	}

	return fmt.Sprintf("%s.%s", pkg, name), nil
}

// IDProtoFrom creates an Id proto from package and name strings.
//
// Returns an error if `pkg` or `name` strings are not valid.
func IDProtoFrom(pkg string, name string) (*idpb.Id, error) {
	err := ValidatePackage(pkg)
	if err == nil {
		err = ValidateName(name)
	}
	if err != nil {
		return nil, fmt.Errorf("cannot create Id from (%q, %q): %v", pkg, name, err)
	}

	return &idpb.Id{Name: name, Package: pkg}, nil
}

// IDProtoFromString creates an Id proto from an id string.
//
// A valid id is formatted as in IsID.
func IDProtoFromString(id string) (*idpb.Id, error) {
	matches, err := getNamedMatches(id, idRegex, []string{"name", "package"})
	if err != nil {
		return nil, err
	}

	return &idpb.Id{
		Name:    matches["name"],
		Package: matches["package"],
	}, nil
}

// IDFromProto creates an id string from an Id proto message.
//
// Ids are formatted as in IsID.
//
// Returns an error if `package` or `name` fields are not valid.
func IDFromProto(id *idpb.Id) (string, error) {
	return IDFrom(id.GetPackage(), id.GetName())
}

// IDFromProtoUnchecked creates an id string from an Id proto message, but does no validation. This
// variant should be used in cases where validation has already been done and conversion between
// APIs or for mapping is required.
func IDFromProtoUnchecked(p *idpb.Id) string {
	return fmt.Sprintf("%s.%s", p.GetPackage(), p.GetName())
}

// IDVersionFrom creates an id_version from package, name, and version strings.
//
// Id_versions are formatted as in IsIDVersion.
//
// Returns an error if `pkg`, `name`, or `version` strings are not valid.
func IDVersionFrom(pkg string, name string, version string) (string, error) {
	err := ValidatePackage(pkg)
	if err == nil {
		err = ValidateName(name)
	}
	if err == nil {
		err = ValidateVersion(version)
	}
	if err != nil {
		return "", fmt.Errorf("cannot create id_version from (%q, %q, %q): %v", pkg, name, version, err)
	}

	return fmt.Sprintf("%s.%s.%s", pkg, name, version), nil
}

// IDVersionProtoFrom creates an IdVersion proto from package, name, and version strings.
//
// Returns an error if `pkg`, `name`, or `version` strings are not valid.
func IDVersionProtoFrom(pkg string, name string, version string) (*idpb.IdVersion, error) {
	id, err := IDProtoFrom(pkg, name)
	if err != nil {
		return nil, err
	}
	if err := ValidateVersion(version); err != nil {
		return nil, fmt.Errorf("cannot create IdVersion from (%q, %q, %q): %v", pkg, name, version, err)
	}

	return &idpb.IdVersion{Id: id, Version: version}, nil
}

// IDOrIDVersionProtoFrom creates an IdVersion proto with an optionally empty version string from a
// candidate input.
func IDOrIDVersionProtoFrom(str string) (*idpb.IdVersion, error) {
	if matches, err := getNamedMatches(str, idVersionRegex, []string{"name", "package", "version"}); err == nil {
		return &idpb.IdVersion{
			Id: &idpb.Id{
				Name:    matches["name"],
				Package: matches["package"],
			},
			Version: matches["version"],
		}, nil
	}
	if matches, err := getNamedMatches(str, idRegex, []string{"name", "package"}); err == nil {
		return &idpb.IdVersion{
			Id: &idpb.Id{
				Name:    matches["name"],
				Package: matches["package"],
			},
		}, nil
	}
	return nil, fmt.Errorf("%q is not a valid id or id_version", str)
}

// IDVersionFromProto creates an id_version string from an IdVersion proto message.
//
// Id_versions are formatted as in IsIDVersion.
//
// Returns an error if `package`, `name`, or `version` fields are not valid.
func IDVersionFromProto(idVersion *idpb.IdVersion) (string, error) {
	return IDVersionFrom(idVersion.GetId().GetPackage(), idVersion.GetId().GetName(), idVersion.GetVersion())
}

// IDVersionFromProtoUnchecked creates an id_version string from an IdVersion proto message, but
// does no validation. This variant should be used in cases where validation has already been done
// and conversion between APIs or for mapping is required.
func IDVersionFromProtoUnchecked(p *idpb.IdVersion) string {
	return fmt.Sprintf("%s.%s.%s", p.GetId().GetPackage(), p.GetId().GetName(), p.GetVersion())
}

// NameFrom returns the name part of an id or id_version.
//
// `id` must be formatted as an id or id_version, as described in IsID and IsIDVersion,
// respectively.
func NameFrom(id string) (string, error) {
	name, err := getNamedMatch(id, idVersionRegex, "name")
	if err == nil {
		return name, nil
	}

	return getNamedMatch(id, idRegex, "name")
}

// PackageFrom returns the package part of an id or id_version.
//
// `id` must be formatted as an id or id_version, as described in IsID and IsIDVersion,
// respectively.
func PackageFrom(id string) (string, error) {
	pkg, err := getNamedMatch(id, idVersionRegex, "package")
	if err == nil {
		return pkg, nil
	}

	return getNamedMatch(id, idRegex, "package")
}

// VersionFrom returns the  version part of an id_version.
//
// `id_version` must be formatted as described in IsIDVersion.
func VersionFrom(idVersion string) (string, error) {
	return getNamedMatch(idVersion, idVersionRegex, "version")
}

// RemoveVersionFrom strips the version from `id` and returns the id substring.
//
// `id` must be formatted as an id or id_version, as described in IsID and IsIDVersion,
// respectively.
//
// If there is no version information in the given `id`, the returned value will equal `id`.
func RemoveVersionFrom(id string) (string, error) {
	stripped, err := getNamedMatch(id, idVersionRegex, "id")
	if err == nil {
		return stripped, nil
	}

	if err := ValidateID(id); err != nil {
		return "", err
	}
	return id, nil
}

// SplitID splits an id by its separator character ".".
// If the input is an id_version, it returns only the id part split by periods.
func SplitID(id string) []string {
	if p, err := NewIDVersionParts(id); err == nil {
		return strings.Split(p.ID(), ".")
	}
	return strings.Split(id, ".")
}

// IsID Tests whether a string is a valid Asset id.
//
// A valid id is formatted as "<package>.<name>", where `package` and `name` are formatted as
// described in IsPackage and IsName, respectively.
func IsID(id string) bool {
	return idRegex.MatchString(id)
}

// IsIDVersion tests whether a string is a valid Asset id_version.
//
// A valid id_version is formatted as "<package>.<name>.<version>", where `package`, `name`, and
// `version` are formatted as described in IsPackage, IsName, and IsVersion, respectively.
func IsIDVersion(idVersion string) bool {
	return idVersionRegex.MatchString(idVersion)
}

// IsName tests whether a string is a valid Asset name.
//
// A valid name:
//   - consists only of lower case alphanumeric characters and underscores;
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character;
//   - does not contain multiple underscores in a row.
//
// NOTE: Disallowing multiple underscores in a row enables underscores to be replaced with a hyphen
// (-) and periods to be replaced with two hyphens (--) in order to convert Asset ids to kubernetes
// labels without possibility of collisions.
func IsName(name string) bool {
	return nameRegex.MatchString(name)
}

// IsPackage tests whether a string is a valid Asset package.
//
// A valid package:
//   - consists only of alphanumeric characters, underscores, and periods;
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character;
//   - contains at least one period;
//   - precedes each period with an alphanumeric character;
//   - follows each period with an alphabetic character;
//   - does not contain multiple underscores in a row.
//
// NOTE: Disallowing multiple underscores in a row enables underscores to be replaced with a hyphen
// (-) and periods to be replaced with two hyphens (--) in order to convert Asset ids to kubernetes
// labels without possibility of collisions.
func IsPackage(pkg string) bool {
	return packageRegex.MatchString(pkg)
}

// IsVersion tests whether a string is a valid Asset version.
//
// A valid version is formatted as described by semver.org.
func IsVersion(version string) bool {
	return versionRegex.MatchString(version)
}

// IsUnreleasedVersion tests whether a string is a valid and unreleased Asset version.
//
// A valid unreleased version is formatted as described by semver.org with build metadata matching
// the reserved prefix for unreleased Assets.
//
// Deprecated: New Assets are not marked as unreleased on their version and this will return false
// even if they are unreleased.
func IsUnreleasedVersion(version string) bool {

	// TODO(b/336012830): Remove when all checks for sideloaded version markers are removed.

	return IsVersion(version) && nonReleasedVersionRegex.MatchString(version)
}

// ValidateID validates an id.
//
// A valid id is formatted as described in IsID.
//
// Returns an error if `id` is not valid.
func ValidateID(id string) error {
	if !IsID(id) {
		return fmt.Errorf("%q is not a valid id", id)
	}
	return nil
}

// ValidateIDProto validates the parts of an Id proto.
//
// Returns an error if `idProto` is not valid.
func ValidateIDProto(idProto *idpb.Id) error {
	if err := ValidatePackage(idProto.GetPackage()); err != nil {
		return err
	}
	if err := ValidateName(idProto.GetName()); err != nil {
		return err
	}

	return nil
}

// ValidateIDVersionProto validates the parts of an IdVersion proto.
//
// Returns an error if `idVersionProto` is not valid.
func ValidateIDVersionProto(idVersion *idpb.IdVersion) error {
	if err := ValidateIDProto(idVersion.GetId()); err != nil {
		return err
	}
	if err := ValidateVersion(idVersion.GetVersion()); err != nil {
		return err
	}
	return nil
}

// ValidateIDVersion validates an id_version.
//
// A valid id_version is formatted as described in IsIDVersion.
//
// Returns an error if `idVersion` is not valid.
func ValidateIDVersion(idVersion string) error {
	if !IsIDVersion(idVersion) {
		return fmt.Errorf("%q is not a valid id_version", idVersion)
	}
	return nil
}

// ValidateName validates a name.
//
// A valid name is formatted as described in IsName.
//
// Returns an error if `name` is not valid.
func ValidateName(name string) error {
	if !IsName(name) {
		return fmt.Errorf("%q is not a valid name", name)
	}
	return nil
}

// ValidatePackage validates a package.
//
// A valid package is formatted as described in IsPackage.
//
// Returns an error if `pkg` is not valid.
func ValidatePackage(pkg string) error {
	if !IsPackage(pkg) {
		return fmt.Errorf("%q is not a valid package", pkg)
	}
	return nil
}

// ValidateVersion validates a version.
//
// A version is formatted as described in IsVersion.
//
// Returns an error if `version` is not valid.
func ValidateVersion(version string) error {
	if !IsVersion(version) {
		return fmt.Errorf("%q is not a valid version, it must conform to SemVer 2 (https://semver.org)", version)
	}
	return nil
}

// ParentFromPackage returns the parent package of the specified package/
//
// Returns an empty string if the package has no parent.
//
// NOTE: It does not validate the package.
func ParentFromPackage(pkg string) string {
	if n := strings.Count(pkg, "."); n < 2 {
		return "" // No parent.
	}

	idx := strings.LastIndex(pkg, ".")
	return pkg[:idx]
}

// ToLabel converts the input into a label.
//
// A label can be used as, e.g.:
//   - a Kubernetes resource name
//     (https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#names);
//   - a SpiceDB id (https://authzed.com/docs).
//
// A label:
//   - consists of only lower case alphanumeric characters and hyphens (-);
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character.
//
// This function will potentially apply two transformations to the input:
//   - "." is converted to "--";
//   - "_" is converted to "-".
//
// If the above transformations cannot convert the input into a label, an error is returned.
//
// In order to support reversible transformations (see `FromLabel`), an input cannot be converted if
// it contains any of the following substrings: "-", "_.", "._", "__".
func ToLabel(s string) (string, error) {
	for _, offender := range []string{"-", "_.", "._", "__"} {
		if strings.Contains(s, offender) {
			return "", fmt.Errorf("cannot convert %q into a label (contains %q)", s, offender)
		}
	}

	label := strings.ReplaceAll(strings.ReplaceAll(s, "_", "-"), ".", "--")

	if !labelRegex.MatchString(label) {
		return "", fmt.Errorf("cannot convert %q into a label (got invalid label: %q)", s, label)
	}

	return label, nil
}

// ToLabelNonReversible converts the input into a label (see ToLabel).
//
// The label may not be reversible using FromLabel.
func ToLabelNonReversible(s string) (string, error) {
	if len(s) == 0 {
		return "", fmt.Errorf("cannot convert empty string into a label")
	}
	label := strings.ToLower(s)

	label = strings.ReplaceAll(strings.ReplaceAll(label, "_", "-"), ".", "--")
	label = labelBadCharRegex.ReplaceAllString(label, "-")
	if !labelFirstCharRegex.MatchString(label) {
		label = fmt.Sprintf("a%s", label)
	}
	if !labelLastCharRegex.MatchString(label) {
		label = fmt.Sprintf("%sa", label)
	}

	if !labelRegex.MatchString(label) {
		return "", fmt.Errorf("cannot convert %q into a label (got invalid label: %q)", s, label)
	}

	return label, nil
}

// FromLabel recovers an input string previously passed to ToLabel.
func FromLabel(label string) string {
	return strings.ReplaceAll(strings.ReplaceAll(label, "--", "."), "-", "_")
}

// LINT.ThenChange(
//  //intrinsic_sdk/intrinsic/assets/id_utils.h:interface,
//  //intrinsic_sdk/intrinsic/assets/id_utils.py:interface,
//  //intrinsic/assets/id_utils.ts:interface
// )
