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

// Package validate provides utilities for validating input.
package validate

import (
	"errors"
	"fmt"
	"regexp"

	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"
)

const (
	dnsLabelMaxLength = 63
)

var (
	// anyRegex matches any reasonable single-line user input without quotes.
	anyRegex = regexp.MustCompile(`^[A-Za-z0-9-._~:/?#\[\]@!$&()*+,;%=]*$`)

	// alphabeticRegex matches an alphabetic string.
	alphabeticRegex = regexp.MustCompile(`^[a-zA-Z]*$`)

	// dnsLabelRegex matches a DNS label (without length check).
	dnsLabelRegex = regexp.MustCompile(`^[a-z]([a-z0-9\-]*[a-z0-9])*$`)

	// These are simplified checks to mainly prevent empty or multiline strings
	registryRegex = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9-_./:]*$`)
	// TODO(b/335121968): Look at removing "/" as valid character.  The
	// modified solution service specified images with a slash in the name, but
	// we should potentially be more specified that the part before the slash
	// refers to the repository, which aligns a bit more with
	// https://cloud.google.com/artifact-registry/docs/docker/names.
	imageNameRegex = regexp.MustCompile(`^[a-zA-Z0-9][a-zA-Z0-9-_./]*[a-zA-Z0-9-_]?$`)
	imageTagRegex  = regexp.MustCompile(`^(@sha256:[a-zA-Z0-9]+|:[a-zA-Z0-9-_.]+)$`)

	errTooLong             = errors.New("input is too long")
	errDoesNotMatchPattern = errors.New("input does not match pattern")
)

// UserString validates any reasonable single-line user input without quotes.
//
// It can be used to prevent injection attacks.
func UserString(input string) error {
	return validateInput(input, &validateInputParams{
		pattern:            anyRegex,
		patternDescription: "single-line string without quotes or white space",
	})
}

// Alphabetic validates an alphabetic string.
func Alphabetic(input string) error {
	return validateInput(input, &validateInputParams{
		pattern:            alphabeticRegex,
		patternDescription: "alphabetic string",
	})
}

// DNSLabel validates a DNS label.
//
// A DNS label is a string consisting of at most 63 lower case alphanumeric characters or '-', must
// start with an alphanumeric character, and must end with an alphanumeric character.
//
// See: https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#names.
func DNSLabel(input string) error {
	return validateInput(input, &validateInputParams{
		maxLength:          dnsLabelMaxLength,
		pattern:            dnsLabelRegex,
		patternDescription: "DNS label",
	})
}

// Image performs basic validation of an image proto to avoid basic syntax problems and injection
// attacks.
func Image(img *ipb.Image) error {
	if reg := img.GetRegistry(); !registryRegex.MatchString(reg) {
		return fmt.Errorf("invalid registry: %w", patternMatchError(registryRegex, "", reg))
	}
	if name := img.GetName(); !imageNameRegex.MatchString(name) {
		return fmt.Errorf("invalid name: %w", patternMatchError(imageNameRegex, "", name))
	}
	if tag := img.GetTag(); !imageTagRegex.MatchString(tag) {
		return fmt.Errorf("invalid tag: %w", patternMatchError(imageTagRegex, "", tag))
	}
	return nil
}

type validateInputParams struct {
	maxLength          int
	pattern            *regexp.Regexp
	patternDescription string
}

func validateInput(input string, params *validateInputParams) error {
	if params.maxLength > 0 && len(input) > params.maxLength {
		truncated := fmt.Sprintf("%q...", input[:params.maxLength])
		return fmt.Errorf("%w (length %d > %d, got: %q)", errTooLong, len(input), params.maxLength, truncated)
	}

	if params.pattern != nil && !params.pattern.MatchString(input) {
		return patternMatchError(params.pattern, params.patternDescription, input)
	}

	return nil
}

func patternMatchError(p *regexp.Regexp, pDescription string, input string) error {
	var description string
	if pDescription != "" {
		description = fmt.Sprintf(" (%s)", pDescription)
	}
	return fmt.Errorf("%w %v%s (got: %s)", errDoesNotMatchPattern, p, description, input)
}
