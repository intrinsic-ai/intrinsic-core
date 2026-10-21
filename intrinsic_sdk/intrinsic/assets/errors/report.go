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

// Package report provides a mechanism to treat errors as warnings.
package report

import (
	"errors"
	"fmt"

	"intrinsic/util/status/extstatus"

	espb "intrinsic/util/status/extended_status_go_proto"
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

// warningRule defines a function that returns true if an error should be
// collected as a warning rather than returned as an error.
type warningRule func(error) bool

// Report aggregates errors that are classified as warnings.
//
// Warning rules can be configured in the report. Errors matching these rules
// are accumulated as warnings in the report when an error is added.
// The caller can then inspect the report afterwards to check for warnings.
type Report struct {
	rules    []warningRule
	warnings []error
}

// Option is a functional option to configure the Report.
type Option func(*Report)

// AsWarning creates a rule that treats specific error values (e.g., sentinel errors)
// as warnings using errors.Is under the hood.
//
// Usage example:
//
//	r := report.New(report.AsWarning(ErrSentinel))
func AsWarning(target error) Option {
	return func(r *Report) {
		r.rules = append(r.rules, func(err error) bool {
			return errors.Is(err, target)
		})
	}
}

// AsWarningIf allows users to provide arbitrary logic to determine
// if an error should be treated as a warning.
//
// Usage example:
//
//	r := report.New(report.AsWarningIf(func(err error) bool {
//		return err.Error() == "warning"
//	}))
func AsWarningIf(fn func(error) bool) Option {
	return func(r *Report) {
		r.rules = append(r.rules, fn)
	}
}

// AsWarningIfType creates a rule that treats all errors of type T as warnings using errors.As.
//
// Usage example:
//
//	r := report.New(report.AsWarningIfType[*CustomError]())
func AsWarningIfType[T error]() Option {
	return func(r *Report) {
		r.rules = append(r.rules, func(err error) bool {
			var t T
			return errors.As(err, &t)
		})
	}
}

// AsWarningIfTypeAnd creates a generic rule that checks if an error is of a specific type T.
// If it is, it executes the provided closure. If the closure
// returns true, the error is collected as a warning.
//
// Usage example:
//
//	r := report.New(report.AsWarningIfTypeAnd(func(err *CustomError) bool {
//		return err.Code == 404
//	}))
func AsWarningIfTypeAnd[T error](fn func(T) bool) Option {
	return func(r *Report) {
		r.rules = append(r.rules, func(err error) bool {
			if target, ok := errors.AsType[T](err); ok {
				return fn(target)
			}
			return false
		})
	}
}

// New creates a new Report with the provided warning rules.
func New(opts ...Option) *Report {
	r := &Report{}
	for _, opt := range opts {
		opt(r)
	}
	return r
}

// Add evaluates an error. If the error is nil, it returns nil.
// If the error matches a defined warning rule, it collects the error
// into the internal warnings list and returns nil.
// If it does not match any warning rule, it returns the error directly.
func (r *Report) Add(err error) error {
	if err == nil {
		return nil
	}

	for _, rule := range r.rules {
		if rule(err) {
			// Matched a rule, collect as a warning and return nil.
			r.warnings = append(r.warnings, err)
			return nil
		}
	}

	// Error didn't match any rule; it is an error.
	return err
}

// Warnings returns the slice of all collected warning errors.
func (r *Report) Warnings() []error {
	return r.warnings
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

// ToExtendedStatus converts all warnings in the report to a combined ExtendedStatus,
// where each warning is included as a context in the returned ExtendedStatus.
//
// For each warning, it determines its ExtendedStatus representation by checking:
//   - If the error has an associated ExtendedStatus (e.g., created via extstatus.NewError or wrapped),
//     it extracts the status directly using extstatus.FromError.
//   - If the error implements ExtendedStatusConverter, it delegates conversion to the custom implementation.
//   - Otherwise, it falls back to creating a default ExtendedStatus using the default component name,
//     default status code, and the error's string message as the title.
//
// The severity of the returned ExtendedStatus is set to the highest severity among all warnings.
func (r *Report) ToExtendedStatus(opts ...ReportExtendedStatusOption) *extstatus.ExtendedStatus {
	if r == nil || len(r.warnings) == 0 {
		return nil
	}
	o := &reportExtendedStatusOpts{
		component: defaultComponentName,
		code:      defaultStatusCode,
	}
	for _, opt := range opts {
		opt(o)
	}
	contexts := make([]*extstatus.ExtendedStatus, 0, len(r.warnings))
	severity := espb.ExtendedStatus_DEFAULT
	for _, w := range r.warnings {
		var es *extstatus.ExtendedStatus
		if e, ok := extstatus.FromError(w); ok {
			es = e
		} else if converter, ok := w.(ExtendedStatusConverter); ok {
			es = converter.ToExtendedStatus()
		} else {
			es = extstatus.New(defaultComponentName,
				uint32(defaultStatusCode),
				extstatus.WithTitle(w.Error()),
			)
		}
		contexts = append(contexts, es)

		// Keep track of the highest severity among all warnings.
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
