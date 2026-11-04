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

// Package report provides a mechanism to collect validation errors and classify them by severity
// as warnings, errors, or fatal errors.
package report

import (
	"fmt"

	"intrinsic/util/status/extstatus"

	espb "intrinsic/util/status/extended_status_go_proto"
)

// Severity classifies an error added to a [Report], and determines how [Report.Add] handles it.
type Severity int32

const (
	// Warning is the severity of an error that does not fail the validation.
	Warning Severity = iota
	// Error is the severity of an error that fails the validation.
	Error
	// Fatal is the severity of an error that fails the validation and stops it.
	// [Report.Add] returns errors with this severity.
	Fatal
)

const (
	// defaultComponentName is the name that will be used as the component name for ExtendedStatus
	// errors unless overridden. This is arbitrarily chosen, and deviates from the official
	// guidelines that specifies providing a component name following an ID format
	// (e.g. <package>.<name>), since the component is not specific to any Asset.
	defaultComponentName = "assets_errors"
	// defaultStatusCode is the status code that will be used for ExtendedStatus errors unless
	// overridden.
	defaultStatusCode = 2000
)

// SeverityRule decides the [Severity] of an error.
type SeverityRule func(error) Severity

// allFatal is the default [SeverityRule]: every error has the [Fatal] severity.
func allFatal(error) Severity { return Fatal }

// AllErrors is a [SeverityRule] under which every error has the [Error] severity.
func AllErrors(error) Severity { return Error }

// AllWarnings is a [SeverityRule] under which every error has the [Warning] severity.
func AllWarnings(error) Severity { return Warning }

// ExtendedStatusConverter is implemented by error types that can define their own ExtendedStatus representation.
//
// When a Report is converted to an ExtendedStatus (via ToExtendedStatus), any errors implementing
// this interface will be converted using their custom implementation. This allows custom error types to
// preserve specific error codes, components, or contexts instead of falling back to the default
// component name, status code, and generic error message.
type ExtendedStatusConverter interface {
	// ToExtendedStatus converts the error to a customized ExtendedStatus representation.
	ToExtendedStatus() *extstatus.ExtendedStatus
}

// Report records the errors of a validation by [Severity], according to its [SeverityRule].
//
// Errors with the [Warning] or [Error] severity are accumulated in the report when they are added,
// while errors with the [Fatal] severity are returned to the caller. The caller can then inspect
// the report afterwards to check for warnings and errors.
type Report struct {
	rule     SeverityRule
	warnings []error
	errors   []error
}

// Option is a functional option to configure the [Report].
type Option func(*Report)

// WithSeverityRule sets the [SeverityRule] that decides the [Severity] of each error. By default,
// every error has the [Fatal] severity.
//
// Usage example:
//
//	r := report.New(report.WithSeverityRule(func(err error) report.Severity {
//		if errors.Is(err, ErrSentinel) {
//			return report.Warning
//		}
//		return report.Fatal
//	}))
func WithSeverityRule(rule SeverityRule) Option {
	return func(r *Report) { r.rule = rule }
}

// New creates a new [Report] with the provided options.
func New(opts ...Option) *Report {
	r := &Report{rule: allFatal}
	for _, opt := range opts {
		opt(r)
	}
	return r
}

// Add records an error with the [Severity] the [SeverityRule] gives it. A nil error is ignored. If
// the rule says the error is [Fatal], it is not recorded but returned, and the validation must
// return it.
func (r *Report) Add(e error) error {
	if e == nil {
		return nil
	}
	// Default to allFatal if no rule is set, e.g. for a zero-value Report.
	rule := r.rule
	if rule == nil {
		rule = allFatal
	}
	switch rule(e) {
	case Error:
		r.errors = append(r.errors, e)
	case Warning:
		r.warnings = append(r.warnings, e)
	default:
		return e
	}
	return nil
}

// Warnings returns the slice of all collected warning errors.
func (r *Report) Warnings() []error {
	return r.warnings
}

// Errors returns the slice of all errors recorded with the [Error] severity.
func (r *Report) Errors() []error {
	return r.errors
}

// ReportExtendedStatusOption is a functional option to configure the ExtendedStatus generation for Report.
type ReportExtendedStatusOption func(*reportExtendedStatusOpts)

type reportExtendedStatusOpts struct {
	code        uint32
	component   string
	titlePrefix string
}

// WithCode sets the status code for the ExtendedStatus.
func WithCode(code uint32) ReportExtendedStatusOption {
	return func(opts *reportExtendedStatusOpts) {
		opts.code = code
	}
}

// WithComponentName sets the component name for the ExtendedStatus.
func WithComponentName(name string) ReportExtendedStatusOption {
	return func(opts *reportExtendedStatusOpts) {
		opts.component = name
	}
}

// WithTitlePrefix sets a custom prefix for the title of the returned ExtendedStatus.
func WithTitlePrefix(prefix string) ReportExtendedStatusOption {
	return func(opts *reportExtendedStatusOpts) {
		opts.titlePrefix = prefix
	}
}

// ToExtendedStatus converts the recorded errors in the report to a combined
// [extstatus.ExtendedStatus], where each error is included as a context in the returned
// ExtendedStatus.
//
// If [Report.Errors] is non-empty, only those errors are included and warnings are omitted.
// Otherwise, [Report.Warnings] are included. It returns nil if both are empty.
//
// For each error, it determines its ExtendedStatus representation by checking:
//   - If the error has an associated ExtendedStatus (e.g., created via [extstatus.NewError]), it
//     extracts the status directly using [extstatus.FromError]. Wrapped errors are not unwrapped,
//     so a wrapped ExtendedStatus error falls back to the default conversion below.
//   - If the error implements [ExtendedStatusConverter], it delegates conversion to the custom
//     implementation.
//   - Otherwise, it falls back to creating a default ExtendedStatus using the default component
//     name, default status code, and the error's string message as the title.
//
// The returned ExtendedStatus takes the highest ExtendedStatus severity among the included errors.
func (r *Report) ToExtendedStatus(opts ...ReportExtendedStatusOption) *extstatus.ExtendedStatus {
	if r == nil {
		return nil
	}
	if len(r.errors) > 0 {
		return extendedStatus(r.errors, opts)
	}
	return extendedStatus(r.warnings, opts)
}

// extendedStatus converts errs to a combined [extstatus.ExtendedStatus], where each error is
// included as a context. It returns nil if errs is empty.
func extendedStatus(errs []error, opts []ReportExtendedStatusOption) *extstatus.ExtendedStatus {
	if len(errs) == 0 {
		return nil
	}
	o := &reportExtendedStatusOpts{
		component: defaultComponentName,
		code:      defaultStatusCode,
	}
	for _, opt := range opts {
		opt(o)
	}
	contexts := make([]*extstatus.ExtendedStatus, 0, len(errs))
	severity := espb.ExtendedStatus_DEFAULT
	for _, err := range errs {
		var es *extstatus.ExtendedStatus
		if e, ok := extstatus.FromError(err); ok {
			es = e
		} else if converter, ok := err.(ExtendedStatusConverter); ok {
			es = converter.ToExtendedStatus()
		} else {
			es = extstatus.New(defaultComponentName,
				uint32(defaultStatusCode),
				extstatus.WithTitle(err.Error()),
			)
		}
		contexts = append(contexts, es)

		// Keep track of the highest severity among all errors.
		if s := es.Proto().GetSeverity(); s > severity {
			severity = s
		}
	}
	title := fmt.Sprintf("%d error(s) found", len(contexts))
	if o.titlePrefix != "" {
		title = o.titlePrefix + ": " + title
	}
	return extstatus.New(o.component,
		uint32(o.code),
		extstatus.WithSeverity(severity),
		extstatus.WithTitle(title),
		extstatus.WithContexts(contexts),
	)
}
