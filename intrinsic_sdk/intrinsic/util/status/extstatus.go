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

// Package extstatus provides a wrapper for extended status.
//
// See go/intrinsic-extended-status-design for more details.
package extstatus

import (
	"fmt"
	"io"
	"strings"
	"time"

	"intrinsic/util/grpc/grpclimits"

	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	contextpb "intrinsic/logging/proto/context_go_proto"
	espb "intrinsic/util/status/extended_status_go_proto"

	statuspb "google.golang.org/genproto/googleapis/rpc/status"
	timestamppb "google.golang.org/protobuf/types/known/timestamppb"
)

// The ExtendedStatus wrapper implements a builder pattern to collect status information.
//
// Use the Err() function to create an error to return in functions.
//
// Example:
//
//		return nil, extstatus.NewError("ai.intrinsic.my_service", 2343,
//		              WithTitle("Failed to ..."),
//	                   WithUserMessage("External report"))
type ExtendedStatus struct {
	s        *espb.ExtendedStatus
	grpcCode codes.Code
}

// The newOptions struct enables to pass additional information for New*.
//
// It is strongly advised to always set Title and ExternalMessage if the error
// is expected to reach an end user (really, just always set this to something
// legible). The debugMessage can contain more detailed information which
// potentially only developers require to analyze an error. Whenever you have
// access to a LogContext add it to the status to enable querying additional
// data. You may use the GrpcCode in the context of a GrpcCall, i.e., if the
// exxtended status is expected to be converted to a GRPCStatus eventually.
type newOptions struct {
	timestamp        *time.Time
	title            string
	debugMessage     string
	userMessage      string
	userInstructions string
	severity         *espb.ExtendedStatus_Severity
	context          []*espb.ExtendedStatus
	logContext       *contextpb.Context
	grpcCode         codes.Code
}

// NewOption is a function type for modifying newOptions.
type NewOption func(*newOptions)

// WithTimestamp returns an option function to set the timestamp on the created extended status.
func WithTimestamp(timestamp time.Time) NewOption {
	return func(o *newOptions) {
		o.timestamp = &timestamp
	}
}

// WithTitle returns an option function to set the title on the created extended status.
func WithTitle(title string) NewOption {
	return func(o *newOptions) {
		o.title = title
	}
}

// WithDebugMessage returns an option function to set the debug report message on the created extended status.
func WithDebugMessage(message string) NewOption {
	return func(o *newOptions) {
		o.debugMessage = message
	}
}

// WithUserMessage returns an option function to set the user report message on the created extended status.
func WithUserMessage(message string) NewOption {
	return func(o *newOptions) {
		o.userMessage = message
	}
}

// WithUserInstructions returns an option function to set the user instructions on the created extended status.
func WithUserInstructions(instructions string) NewOption {
	return func(o *newOptions) {
		o.userInstructions = instructions
	}
}

// WithSeverity returns an option function to set the severity of the created extended status.
// Severity values are the same as the ones for absl::LogSeverity.
// If not set explicitly, severity defaults to INFO.
func WithSeverity(severity espb.ExtendedStatus_Severity) NewOption {
	return func(o *newOptions) {
		o.severity = &severity
	}
}

// WithLogContext returns an option function to set the log context extended status.
func WithLogContext(logContext *contextpb.Context) NewOption {
	return func(o *newOptions) {
		o.logContext = proto.Clone(logContext).(*contextpb.Context)
	}
}

// WithContextProto returns an option function to add a context from proto to the created extended status.
func WithContextProto(context *espb.ExtendedStatus) NewOption {
	return func(o *newOptions) {
		o.context = append(o.context, proto.Clone(context).(*espb.ExtendedStatus))
	}
}

// WithContext returns an option function to add a context to the created extended status.
func WithContext(context *ExtendedStatus) NewOption {
	return func(o *newOptions) {
		o.context = append(o.context, proto.Clone(context.Proto()).(*espb.ExtendedStatus))
	}
}

// WithContexts returns an option function to add contexts to the created extended status.
func WithContexts(contexts []*ExtendedStatus) NewOption {
	return func(o *newOptions) {
		for _, context := range contexts {
			o.context = append(o.context, proto.Clone(context.Proto()).(*espb.ExtendedStatus))
		}
	}
}

// WithContextProtos returns an option function to add context protos to the created extended status.
func WithContextProtos(contexts []*espb.ExtendedStatus) NewOption {
	return func(o *newOptions) {
		for _, context := range contexts {
			o.context = append(o.context, proto.Clone(context).(*espb.ExtendedStatus))
		}
	}
}

// WithContextFromError returns an option function to add an error as context to the created extended status.
func WithContextFromError(err error) NewOption {
	return func(o *newOptions) {
		context, ok := FromError(err)
		if !ok {
			// Failed to convert error to extended status, do it the
			// "old-fashioned" way from the error interface
			context = New("unknown-downstream", 0, WithTitle(err.Error()))
		}
		o.context = append(o.context, context.Proto())
	}
}

// WithContextFromErrors returns an option function to add errors as context to the created extended status.
func WithContextFromErrors(errs []error) NewOption {
	return func(o *newOptions) {
		for _, err := range errs {
			context, ok := FromError(err)
			if !ok {
				// Failed to convert error to extended status, do it the
				// "old-fashioned" way from the error interface
				context = New("unknown-downstream", 0, WithTitle(err.Error()))
			}
			o.context = append(o.context, context.Proto())
		}
	}
}

// WithGrpcCode sets code to be used when the error is used in a gRPC context.
// If this is set, calling GRPCStatus on the error will use the given code as
// the generic gRPC error code.
func WithGrpcCode(code codes.Code) NewOption {
	return func(o *newOptions) {
		o.grpcCode = code
	}
}

// New creates an ExtendedStatus with the given StatusCode (component + numeric code).
func New(component string, code uint32, options ...NewOption) *ExtendedStatus {
	p := &espb.ExtendedStatus{StatusCode: &espb.StatusCode{
		Code: code, Component: component,
	}}

	opts := newOptions{grpcCode: codes.Internal}

	for _, optFunc := range options {
		optFunc(&opts)
	}

	p.Title = opts.title
	if opts.debugMessage != "" {
		p.DebugReport = &espb.ExtendedStatus_DebugReport{
			Message: opts.debugMessage,
		}
	}
	if opts.userMessage != "" || opts.userInstructions != "" {
		p.UserReport = &espb.ExtendedStatus_UserReport{
			Message:      opts.userMessage,
			Instructions: opts.userInstructions,
		}
	}
	if opts.timestamp != nil {
		p.Timestamp = timestamppb.New(*opts.timestamp)
	} else {
		p.Timestamp = timestamppb.Now()
	}
	for _, context := range opts.context {
		p.Context = append(p.Context, context)
	}
	if opts.logContext != nil {
		p.RelatedTo = &espb.ExtendedStatus_Relations{LogContext: opts.logContext}
	}

	if opts.severity != nil {
		p.Severity = *opts.severity
	}
	return &ExtendedStatus{s: p, grpcCode: opts.grpcCode}
}

// NewError creates an ExtendedStatus wrapped in an error.
func NewError(component string, code uint32, options ...NewOption) error {
	return New(component, code, options...).Err()
}

// FromProto creates a new ExtendedStatus from a given ExtendedStatus proto.
func FromProto(es *espb.ExtendedStatus) *ExtendedStatus {
	return &ExtendedStatus{s: proto.Clone(es).(*espb.ExtendedStatus)}
}

// FromError tries to convert an error to an ExtendedStatus. This may fail (and
// ok will be false) if the error was not created from an ExtendedStatus.
func FromError(err error) (es *ExtendedStatus, ok bool) {
	e, ok := err.(*Error)
	if ok {
		return e.es, true
	}

	return nil, false
}

// FromGRPCError tries to convert an error to a new ExtendedStatus. This may
// fail (and ok will be false) if the error is not a gRPC status/error or if the
// gRPC status does not have an ExtendedStatus detail.
//
// Use this, for example, if you called a gRPC service as a client and want to
// use extended status information for more specific handling of a returned
// error. To just pass an error as context use ContextFromErrors when creating
// the caller component's extended status to pass on the error.
func FromGRPCError(err error) (es *ExtendedStatus, ok bool) {
	grpcStatus, ok := status.FromError(err)
	if !ok {
		return nil, false
	}
	return FromGRPCStatus(grpcStatus)
}

// FromGRPCStatusProto tries to convert a gRPC Status proto to a new ExtendedStatus.
// This may fail (and ok will be false) if the gRPC status does not have an
// ExtendedStatus detail.
//
// Use this, for example, if you received an error result from a long-running
// operation on a gRPC service and want to use extended status information for
// more specific handling of the returned error.
func FromGRPCStatusProto(s *statuspb.Status) (es *ExtendedStatus, ok bool) {
	return FromGRPCStatus(status.FromProto(s))
}

// FromGRPCStatus tries to convert a gRPC Status to a new ExtendedStatus. This
// may fail (and ok will be false) if the gRPC status does not have an
// ExtendedStatus detail.
func FromGRPCStatus(s *status.Status) (es *ExtendedStatus, ok bool) {
	details := s.Details()
	if len(details) == 0 {
		return nil, false
	}
	for _, detail := range details {
		extendedStatus, ok := detail.(*espb.ExtendedStatus)
		if !ok {
			continue
		}
		return FromProto(extendedStatus), true
	}

	return nil, false
}

// GRPCStatus converts to and returns a gRPC status.
func (e *ExtendedStatus) GRPCStatus() *status.Status {
	st := status.New(e.grpcCode, e.s.GetTitle())
	ds, err := st.WithDetails(e.s)
	if err != nil {
		return st
	}
	return ds
}

// Proto returns the contained ExtendedStatus proto.
func (e *ExtendedStatus) Proto() *espb.ExtendedStatus {
	return e.s
}

// Err converts to an error.
func (e *ExtendedStatus) Err() error {
	return &Error{es: e}
}

// String implements [fmt.Stringer].
func (e *ExtendedStatus) String() string {
	if e == nil || e.s == nil {
		return "<nil>"
	}
	return formatExtendedStatus(e.s, 0)
}

// Format implements [fmt.Formatter].
func (e *ExtendedStatus) Format(s fmt.State, verb rune) {
	res := e.String()
	switch verb {
	case 'v', 's':
		io.WriteString(s, res)
	case 'q':
		fmt.Fprintf(s, "%q", res)
	}
}

func indentLines(s string, indent string) string {
	if s == "" {
		return ""
	}
	lines := strings.Split(s, "\n")
	for i, l := range lines {
		lines[i] = indent + l
	}
	return strings.Join(lines, "\n")
}

func formatExtendedStatus(p *espb.ExtendedStatus, level int) string {
	if p == nil {
		return ""
	}
	var sb strings.Builder
	indent := strings.Repeat("  ", level)

	sc := p.GetStatusCode()
	if sc != nil {
		sb.WriteString(fmt.Sprintf("%sStatusCode: %s:%d\n", indent, sc.GetComponent(), sc.GetCode()))
	}

	if title := p.GetTitle(); title != "" {
		sb.WriteString(fmt.Sprintf("%sTitle: %s\n", indent, title))
	}

	if ts := p.GetTimestamp(); ts != nil {
		t := ts.AsTime()
		if !t.IsZero() {
			sb.WriteString(fmt.Sprintf("%sTimestamp: %s\n", indent, t.Format(time.RFC3339)))
		}
	}

	if ur := p.GetUserReport(); ur != nil {
		if msg := ur.GetMessage(); msg != "" {
			sb.WriteString(fmt.Sprintf("%sUser Report:\n%s\n", indent, indentLines(msg, indent+"  ")))
		}
		if inst := ur.GetInstructions(); inst != "" {
			sb.WriteString(fmt.Sprintf("%sInstructions:\n%s\n", indent, indentLines(inst, indent+"  ")))
		}
	}

	if dr := p.GetDebugReport(); dr != nil {
		if msg := dr.GetMessage(); msg != "" {
			sb.WriteString(fmt.Sprintf("%sDebug Report:\n%s\n", indent, indentLines(msg, indent+"  ")))
		}
	}

	if ctxs := p.GetContext(); len(ctxs) > 0 {
		sb.WriteString(fmt.Sprintf("%sContext:\n", indent))
		for i, c := range ctxs {
			sb.WriteString(fmt.Sprintf("%s  Context[%d]:\n", indent, i))
			sb.WriteString(formatExtendedStatus(c, level+2))
		}
	}

	return sb.String()
}

// Error wraps an ExtendedStatus. It implements error and gRPC's Status.
type Error struct {
	es *ExtendedStatus
}

// Error implements error interface and returns the title as error string.
func (e *Error) Error() string {
	return fmt.Sprintf("%s:%d: %s", e.es.Proto().GetStatusCode().GetComponent(),
		e.es.Proto().GetStatusCode().GetCode(),
		e.es.Proto().GetTitle())
}

// GRPCStatus implements the golang grpc status interface and returns a gRPC status.
func (e *Error) GRPCStatus() *status.Status {
	metadataSize := proto.Size(e.es.GRPCStatus().Proto())
	if metadataSize > grpclimits.GrpcRecommendedMaxMetadataHardLimit {
		log.Warningf(
			"ExtendedStatus converted to RPC status is larger than "+
				"recommended metadata hard limit (%d > %d)."+
				"Will very likely result in error",
			metadataSize, grpclimits.GrpcRecommendedMaxMetadataHardLimit)
	} else if metadataSize > grpclimits.GrpcRecommendedMaxMetadataSoftLimit {
		log.Warningf(
			"ExtendedStatus converted to RPC status is larger than "+
				"recommended metadata soft limit (%d > %d)."+
				"Will likely result in error",
			metadataSize, grpclimits.GrpcRecommendedMaxMetadataSoftLimit)
	} else if metadataSize > (grpclimits.GrpcRecommendedMaxMetadataSoftLimit / 2) {
		log.Warningf(
			"ExtendedStatus converted to RPC status is larger than "+
				"half the recommended metadata soft limit (%d > %d)."+
				"Will possibly result in error",
			metadataSize, grpclimits.GrpcRecommendedMaxMetadataSoftLimit/2)
	}
	return e.es.GRPCStatus()
}

// Is implements future error.Is functionality.
// A Error is equivalent if StatusCodes are identical.
func (e *Error) Is(target error) bool {
	tse, ok := target.(*Error)
	if !ok {
		return false
	}
	return proto.Equal(e.es.s.GetStatusCode(), tse.es.s.GetStatusCode())
}
