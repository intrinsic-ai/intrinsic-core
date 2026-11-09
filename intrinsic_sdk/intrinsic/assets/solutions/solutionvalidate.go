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

// Package solutionvalidate provides utils for validating Solutions.
package solutionvalidate

import (
	"errors"
	"fmt"

	deperrs "intrinsic/assets/dependencies/errors"
	"intrinsic/assets/errors/report"

	svpb "intrinsic/assets/proto/v1/solution_validation_policy_go_proto"
)

// SeverityRuleFor returns the rule used to decide the severity of each validation error under the
// given SolutionValidationPolicy value.
//
// Returns an error if the policy is unspecified or unknown.
func SeverityRuleFor(p svpb.SolutionValidationPolicy) (report.SeverityRule, error) {
	switch p {
	case svpb.SolutionValidationPolicy_SOLUTION_VALIDATION_POLICY_STRICT:
		return strictSeverity, nil
	case svpb.SolutionValidationPolicy_SOLUTION_VALIDATION_POLICY_RECOVERABLE:
		return report.AllWarnings, nil
	case svpb.SolutionValidationPolicy_SOLUTION_VALIDATION_POLICY_UNSPECIFIED:
		return nil, errors.New("solution validation policy must be specified")
	default:
		return nil, fmt.Errorf("unsupported solution validation policy: %v", p)
	}
}

// strictSeverity is the severity rule for SOLUTION_VALIDATION_POLICY_STRICT.
func strictSeverity(err error) report.Severity {
	// Mismatched Asset versions are considered warnings even in a strict policy.
	// Every other issue as a result of validation is considered an error.
	if de, ok := errors.AsType[*deperrs.Error](err); ok &&
		de.Code() == deperrs.CodeErrReferencedAssetVersionMismatch {
		return report.Warning
	}
	return report.Error
}
