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

package operations

import (
	"fmt"
	"regexp"
	"strings"
)

// Filter is an implementation to support matching long-running operations using a string filter
// expression as defined in go/aip/160. The implementation is operation-specific and only supports a
// small subset of the filter grammar.
// Filtering is currently limited to a subset of the filter spec:
// * only the `name` and `done` fields are evaluated
// * only the `=` operator is supported for comparisons
// * only the `AND` logical operator is supported for joining fields
// * strings may use a wildcard operator (`*`) at the beginning or the end
type Filter struct {
	done *bool
	name *regexp.Regexp
}

// ParseFilterString parses a filter string as defined in go/aip/160 to create a [Filter]. See the
// comment on [Filter] for details on what parts of the filter spec are supported.
func ParseFilterString(filterString string) (*Filter, error) {
	filter := &Filter{}
	if filterString == "" {
		return filter, nil
	}
	// Remove spaces around "=" to make splitting into tokens easier.
	filterString = regexp.MustCompile(" ?(=) ?").ReplaceAllString(filterString, "$1")
	tokens := strings.Split(filterString, " ")

	wantLogicalOperator := false
	for _, token := range tokens {
		// The next token must be a logical operator. If it is not the expression is invalid.
		if wantLogicalOperator {
			// Only allow logical AND operations to join field comparisons.
			if token == "AND" {
				wantLogicalOperator = false
				continue
			}
			return nil, fmt.Errorf("expected AND, got: %q", token)
		}

		wantLogicalOperator = true
		fieldName, fieldValue, found := strings.Cut(token, "=")
		if !found {
			return nil, fmt.Errorf("expected field=value, got: %q", token)
		}

		switch fieldName {
		case "done":
			doneValue, err := parseBoolFieldValue(fieldValue)
			if err != nil {
				return nil, fmt.Errorf("field %q is invalid: %v", "done", err)
			}
			filter.done = &doneValue
		case "name":
			nameGlob, err := parseStringFieldValue(fieldValue)
			if err != nil {
				return nil, fmt.Errorf("field %q is invalid: %v", "name", err)
			}
			filter.name = nameGlob
		default:
			return nil, fmt.Errorf("unsupported field: %q", fieldName)
		}
	}
	return filter, nil
}

// Match returns true if the given operation satisfies the filter and false otherwise.
func (f *Filter) Match(op *Operation) bool {
	if f.done != nil && *f.done != op.Done() {
		return false
	}
	if f.name != nil && !f.name.MatchString(op.Name()) {
		return false
	}
	return true
}

func parseBoolFieldValue(value string) (bool, error) {
	switch value {
	case "true":
		return true, nil
	case "false":
		return false, nil
	default:
		return false, fmt.Errorf("expected boolean (true or false), got: %q", value)
	}
}

func parseStringFieldValue(value string) (*regexp.Regexp, error) {
	// Replace the quoted string with the value between those quotes.
	s := regexp.MustCompile("^\"([^\"]*)\"$").ReplaceAllString(value, "$1")
	// The resulting string must be exactly two characters shorter than the quoted string. Only the
	// quotes should be removed. If the string contained additional quotes or was not surrounded by
	// quotes the length will be different.
	if len(s) != len(value)-2 {
		return nil, fmt.Errorf("expected string surrounded by quotes, got: %s", value)
	}
	r, err := wildcardToRegexp(s)
	if err != nil {
		return nil, fmt.Errorf("provided string value %q is invalid: %v", value, err)
	}
	return r, nil
}

func wildcardToRegexp(s string) (*regexp.Regexp, error) {
	var expr string
	if after, foundPrefix := strings.CutPrefix(s, "*"); foundPrefix {
		expr = fmt.Sprintf("^.*%s$", regexp.QuoteMeta(after))
	}
	if before, foundSuffix := strings.CutSuffix(s, "*"); foundSuffix {
		// The spec only allows one wildcard at the beginning OR the end. The expression was already set
		// based on a prefix wildcard. Having a suffix wildcard at this point is an error state.
		if expr != "" {
			return nil, fmt.Errorf("only one wildcard is permitted at the beginning or the end, got: %q", expr)
		}
		expr = fmt.Sprintf("^%s.*$", regexp.QuoteMeta(before))
	}
	// Match for the whole string if neither prefix nor suffix wildcard is set.
	if expr == "" {
		expr = fmt.Sprintf("^%s$", regexp.QuoteMeta(s))
	}
	r, err := regexp.Compile(expr)
	if err != nil {
		return nil, fmt.Errorf("cannot compile expression: %v", err)
	}
	return r, nil
}
