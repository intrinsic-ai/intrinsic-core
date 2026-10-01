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

// Package telemetry configures OpenTelemetry tracing and Prometheus/OpenCensus metrics for Go services.
//
// For quick-start recipes on writing new OpenTelemetry code (spans, gRPC, HTTP), see README.md.
// For migrating legacy OpenCensus code or debugging cross-language traces, see MIGRATION.md.
package telemetry

import (
	"bufio"
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"strings"

	"contrib.go.opencensus.io/exporter/prometheus"
	traceexporter "github.com/GoogleCloudPlatform/opentelemetry-operations-go/exporter/trace"
	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"go.opencensus.io/stats/view"
	"go.opencensus.io/trace"
	b3prop "go.opentelemetry.io/contrib/propagators/b3"
	ocprop "go.opentelemetry.io/contrib/propagators/opencensus"
	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/bridge/opencensus"
	"go.opentelemetry.io/otel/codes"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracegrpc"
	"go.opentelemetry.io/otel/propagation"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	semconv "go.opentelemetry.io/otel/semconv/v1.34.0"
	oteltrace "go.opentelemetry.io/otel/trace"
	grpccodes "google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

type localSamplerKey struct{}

var (
	// HTTPPropagator is a composite propagator for HTTP services supporting both W3C TraceContext
	// (traceparent) and OpenCensus B3 multi-header formats.
	//
	// Prefer [NewOtelHTTPHandler] and [NewOtelHTTPTransport], which pass this
	// propagator for you.
	//
	// WARNING: Because telemetry.Initialize() does not set a global propagator,
	// you MUST explicitly pass otelhttp.WithPropagators(telemetry.HTTPPropagator) to every
	// otelhttp.NewHandler and otelhttp.NewTransport call, or trace context will be silently dropped.
	HTTPPropagator = propagation.NewCompositeTextMapPropagator(
		// Default propagator for opentelemetry, uses traceparent header
		propagation.TraceContext{},
		// OpenCensus http propagator, uses b3 format with multiple headers
		// See https://github.com/census-instrumentation/opencensus-go/blob/master/plugin/ochttp/trace.go#L29
		b3prop.New(b3prop.WithInjectEncoding(b3prop.B3MultipleHeader)),
	)

	// GRPCPropagator is a composite propagator for gRPC services supporting both W3C TraceContext
	// (traceparent) and OpenCensus binary (grpc-trace-bin) formats.
	//
	// Prefer [NewOtelGRPCServerOptions] and [NewOtelGRPCClientOptions], which pass
	// this propagator for you.
	//
	// WARNING: Because telemetry.Initialize() does not set a global propagator,
	// you MUST explicitly pass otelgrpc.WithPropagators(telemetry.GRPCPropagator) to every
	// otelgrpc.NewClientHandler and otelgrpc.NewServerHandler call (or use NewOtelGRPCServerOptions()),
	// or trace context will be silently dropped.
	GRPCPropagator = propagation.NewCompositeTextMapPropagator(
		// Default propagator for opentelemetry, uses traceparent header
		propagation.TraceContext{},
		// OpenCensus grpc propagator, uses grpc-trace-bin header
		// See https://github.com/census-instrumentation/opencensus-go/blob/master/plugin/ocgrpc/trace_common.go#L30
		ocprop.Binary{},
	)
)

// tracingConfig contains the configuration parameters for tracing.
type tracingConfig struct {
	Enabled         bool
	CloudDeployment bool
	ProjectName     string
	ServiceName     string
	Probability     float64
}

// String returns the string representation of the tracing config.
// The Stringer interface improves debuggability in unit tests.
func (tc *tracingConfig) String() string {
	if tc == nil {
		return "<nil>"
	}
	str := `{
		Enabled: %t
		CloudDeployment: %t
		ProjectName: '%s'
		ServiceName: '%s'
		Probability: %v
	}`
	return fmt.Sprintf(str, tc.Enabled, tc.CloudDeployment, tc.ProjectName,
		tc.ServiceName, tc.Probability)
}

// metricsConfig contains the configuration parameters for metrics.
type metricsConfig struct {
	Enabled     bool
	MetricsPath string
	MetricsPort int64
	Views       []*view.View
}

// String returns the string representation of the tracing config.
// The Stringer interface improves debuggability in unit tests.
func (mc *metricsConfig) String() string {
	if mc == nil {
		return "<nil>"
	}
	str := `{
		Enabled: %t
		MetricsPath: %t
		MetricsPort: %d
	}`
	return fmt.Sprintf(str, mc.Enabled, mc.MetricsPath, mc.MetricsPort)
}

// config contains the configuration parameters for telemetry.
type config struct {
	TracingCfg *tracingConfig
	MetricsCfg *metricsConfig
}

// newConfig creates a new config.
func newConfig(opts ...ConfigOption) config {
	cfg := config{}
	for _, opt := range opts {
		opt(&cfg)
	}
	return cfg
}

// getTracingConfig returns the tracing config.
func (c *config) getTracingConfig() *tracingConfig {
	if c.TracingCfg == nil {
		c.TracingCfg = &tracingConfig{}
		c.TracingCfg.Enabled = false
		c.TracingCfg.CloudDeployment = false
		c.TracingCfg.Probability = 1.0
	}
	return c.TracingCfg
}

// getMetricsConfig returns the metrics config.
func (c *config) getMetricsConfig() *metricsConfig {
	if c.MetricsCfg == nil {
		c.MetricsCfg = &metricsConfig{}
		c.MetricsCfg.Enabled = false
		c.MetricsCfg.MetricsPath = "/metrics"
		c.MetricsCfg.Views = ocgrpc.DefaultServerViews
	}
	return c.MetricsCfg
}

// ConfigOption is a function that can be used to modify the config.
type ConfigOption func(c *config)

// WithCloudTracing enables cloud tracing. For on-prem workloads, please use
// EnableTracing() or WithTracing() instead. The intended use for this function
// are cases where tracing can be turned on and off based on a parameter, e.g.
// one specified as a command line parameter.
func WithCloudTracing(enabled bool) ConfigOption {
	return func(c *config) {
		cfg := c.getTracingConfig()
		cfg.Enabled = enabled
		cfg.CloudDeployment = true
	}
}

// EnableCloudTracing enables cloud tracing.
func EnableCloudTracing() ConfigOption {
	return WithCloudTracing(true)
}

// WithTracing enables tracing for on-prem (non-cloud) workloads. For cloud
// hosted workloads, please use EnableCloudTracing() or WithCloudTracing()
// instead. The intended use for this function are cases where tracing can be
// turned on and off based on a parameter, e.g. one specified as a command line
// parameter.
func WithTracing(enabled bool) ConfigOption {
	return func(c *config) {
		cfg := c.getTracingConfig()
		cfg.Enabled = enabled
		cfg.CloudDeployment = false
	}
}

// EnableTracing enables tracing.
func EnableTracing() ConfigOption {
	return WithTracing(true)
}

// WithProjectName sets the project name for cloud tracing.
func WithProjectName(pn string) ConfigOption {
	return func(c *config) {
		cfg := c.getTracingConfig()
		cfg.ProjectName = pn
	}
}

// WithServiceName sets the service name for cloud tracing.
func WithServiceName(sn string) ConfigOption {
	return func(c *config) {
		cfg := c.getTracingConfig()
		cfg.ServiceName = sn
	}
}

// WithProbability sets the probability for tracing.
func WithProbability(p float64) ConfigOption {
	return func(c *config) {
		cfg := c.getTracingConfig()
		cfg.Probability = p
	}
}

// WithMetrics enables metrics.
func WithMetrics(enabled bool, port int64) ConfigOption {
	return func(c *config) {
		cfg := c.getMetricsConfig()
		cfg.Enabled = enabled
		cfg.MetricsPort = port
	}
}

// EnableMetrics enables and serves metrics at "0.0.0.0:<port>/metrics".
func EnableMetrics(port int64) ConfigOption {
	return WithMetrics(true, port)
}

// WithViews sets the views for metrics.
func WithViews(Views []*view.View) ConfigOption {
	return func(c *config) {
		cfg := c.getMetricsConfig()
		cfg.Views = Views
	}
}

// Telemetry is an object to configure tracing and metrics export in services.
type Telemetry struct {
	tp            *sdktrace.TracerProvider
	metricsServer *http.Server
}

// Initialize creates a new telemetry instance.
func Initialize(opts ...ConfigOption) Telemetry {
	t := Telemetry{}
	t.init(newConfig(opts...))
	return t
}

type filterErrorHandler struct{}

func (f filterErrorHandler) Handle(err error) {
	if err == nil {
		return
	}

	// Ignore "unsupported sampler" errors originating from the OpenCensus-to-OpenTelemetry bridge.
	// Context: The OpenCensus metrics exporter relies on a custom sampler that OpenTelemetry does not support.
	// Reference: https://github.com/census-instrumentation/opencensus-go/blob/v0.24.0/metric/metricexport/reader.go#L191
	if strings.Contains(err.Error(), "unsupported sampler") {
		return
	}

	log.Warningf("OpenTelemetry error: %v", err)
}

func initTracerProvider(ctx context.Context, exporter sdktrace.SpanExporter, serviceName string, probability float64) *sdktrace.TracerProvider {
	otel.SetErrorHandler(filterErrorHandler{})

	var attrs []attribute.KeyValue
	if serviceName != "" {
		attrs = append(attrs, semconv.ServiceName(serviceName))
	}

	res, err := resource.New(ctx,
		resource.WithAttributes(attrs...),
		resource.WithTelemetrySDK(),
		resource.WithHost(),
	)
	if err != nil {
		// resource.New returns partial resource on failure. It's also safe to pass nil resource to WithResource.
		log.Warningf("Failed to create telemetry resource: %v", err)
	}

	tp := sdktrace.NewTracerProvider(
		sdktrace.WithBatcher(exporter),
		sdktrace.WithResource(res),
		sdktrace.WithSampler(LocalBased(sdktrace.ParentBased(sdktrace.TraceIDRatioBased(probability)))),
	)

	otel.SetTracerProvider(tp)
	opencensus.InstallTraceBridge()

	return tp
}

func (t *Telemetry) createCloudTracerProvider(project string, serviceName string, probability float64) error {
	if project == "" {
		return fmt.Errorf("project must be specified")
	}
	log.Info("Creating Google Cloud Trace exporter for tracing.")

	exporter, err := traceexporter.New(traceexporter.WithProjectID(project))
	if err != nil {
		return fmt.Errorf("create trace exporter: %w", err)
	}

	t.tp = initTracerProvider(context.Background(), exporter, serviceName, probability)

	return nil
}

func (t *Telemetry) createOnPremTracerProvider(serviceName string, probability float64) error {
	log.Info("Creating OTLP trace exporter for tracing.")

	ctx := context.Background()
	exporter, err := otlptracegrpc.New(ctx,
		otlptracegrpc.WithInsecure(),
		otlptracegrpc.WithEndpoint("oc-agent.app-intrinsic-base.svc.cluster.local:4317"),
	)
	if err != nil {
		return fmt.Errorf("create trace exporter: %w", err)
	}

	t.tp = initTracerProvider(ctx, exporter, serviceName, probability)

	return nil
}

// init initializes the telemetry instance.
func (t *Telemetry) init(cfg config) {
	if cfg.TracingCfg != nil {
		t.enableTracing(*cfg.TracingCfg)
	}
	if cfg.MetricsCfg != nil {
		t.enableMetrics(*cfg.MetricsCfg)
	}
}

// Shutdown gracefully shuts down the current telemetry instance without
// interrupting active metrics connections.
//
// Example usage:
//
//	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
//	defer cancel()
//	if err := tele.Shutdown(ctx); err != nil {
//		log.Fatalf("Telemetry shutdown error: %v", err)
//	}
//	log.Println("Graceful telemetry shutdown complete.")
func (t *Telemetry) Shutdown(ctx context.Context) error {
	var err error
	if t.tp != nil {
		err = errors.Join(err, t.tp.Shutdown(ctx))
	}
	if t.metricsServer != nil {
		err = errors.Join(err, t.metricsServer.Shutdown(ctx))
	}
	return err
}

// enableTracing enables tracing for the current telemetry instance.
func (t *Telemetry) enableTracing(c tracingConfig) {
	if !c.Enabled {
		return
	}
	if c.CloudDeployment {
		err := t.createCloudTracerProvider(c.ProjectName, c.ServiceName, c.Probability)
		if err != nil {
			log.Warningf("Tracing is disabled! Tracing setup failed: %v", err)
		}
		return
	}
	err := t.createOnPremTracerProvider(c.ServiceName, c.Probability)
	if err != nil {
		log.Warningf("Tracing is disabled! Tracing setup failed: %v", err)
	}
}

// enableMetrics enables metrics for the current telemetry instance.
func (t *Telemetry) enableMetrics(c metricsConfig) {
	if !c.Enabled {
		return
	}
	if err := view.Register(c.Views...); err != nil {
		log.Warningf("Failed to register views: %v", err)
	}
	pe, err := prometheus.NewExporter(prometheus.Options{})
	if err != nil {
		log.Errorf("Metrics is disabled! Metrics setup failed: %v", err)
	}
	view.RegisterExporter(pe)

	mux := http.NewServeMux()
	mux.Handle(c.MetricsPath, pe)
	t.metricsServer = &http.Server{
		Addr:    fmt.Sprintf("0.0.0.0:%v", c.MetricsPort),
		Handler: mux,
	}

	go func() {
		if err := t.metricsServer.ListenAndServe(); !errors.Is(err, http.ErrServerClosed) {
			log.Errorf("Metrics is disabled! Failed to start HTTP server: %v", err)
		}
		log.Info("Stopped serving new metrics connections.")
	}()
}

// TraceIDWriter makes sure only one trace identifier header is written.
// Otherwise our proxy setup in the portal would also add the
// upstream header after we set the header in the first handler.
// This has to be done only once in WriteHeader or Write.
type TraceIDWriter struct {
	http.ResponseWriter
	traceID string
}

// IDHeader is the trace identifier header
const IDHeader = "X-Intrinsic-TraceID"

// WriteHeader writes the headers but makes sure the trace identifier is only present once.
func (w *TraceIDWriter) WriteHeader(status int) {
	if w.traceID != "" {
		w.ResponseWriter.Header().Set(IDHeader, w.traceID)
		w.traceID = ""
	}
	w.ResponseWriter.WriteHeader(status)
}

// Write writes the bytes to the transport and also makes sure only one trace identifier is present.
func (w *TraceIDWriter) Write(b []byte) (int, error) {
	if w.traceID != "" {
		w.ResponseWriter.Header().Set(IDHeader, w.traceID)
		w.traceID = ""
	}
	return w.ResponseWriter.Write(b)
}

// Flush triggers Flush in the underlying response writer.
func (w *TraceIDWriter) Flush() {
	if f, ok := w.ResponseWriter.(http.Flusher); ok {
		f.Flush()
	}
}

// Hijack allows hijacking of the underlying response writer.
func (w *TraceIDWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	h, ok := w.ResponseWriter.(http.Hijacker)
	if !ok {
		return nil, nil, fmt.Errorf("http.Hijacker interface not supported by %T", w.ResponseWriter)
	}
	return h.Hijack()
}

// DefaultHandler returns a handler wrapping [http.DefaultServeMux] that adds a
// trace ID and annotates the span with the given serviceName.
func DefaultHandler(serviceName string) http.Handler {
	return ServiceNameHandler(serviceName, TraceIDHandler(http.DefaultServeMux))
}

// StartSpan starts a new OpenTelemetry span using the global TracerProvider.
func StartSpan(ctx context.Context, spanName string, opts ...oteltrace.SpanStartOption) (context.Context, oteltrace.Span) {
	return otel.Tracer("intrinsic").Start(ctx, spanName, opts...)
}

// ServiceNameHandler returns a handler which annotates the current span with the given service
// name.
//
// Note: Prefer configuring the service name once at startup via
// [WithServiceName] rather than per-request HTTP middleware.
func ServiceNameHandler(serviceName string, h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if span := oteltrace.SpanFromContext(r.Context()); span.IsRecording() {
			span.SetAttributes(semconv.ServiceName(serviceName))
		}
		h.ServeHTTP(w, r)
	})
}

// TraceID returns the current trace identifier if tracing is active.
func TraceID(ctx context.Context) string {
	if span := oteltrace.SpanFromContext(ctx); span.SpanContext().IsSampled() {
		return span.SpanContext().TraceID().String()
	}
	return ""
}

// TraceIDHandler adds the current trace identifier to the response.
// No header is added if no tracing context exists.
func TraceIDHandler(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		tid := ""
		if tid := TraceID(r.Context()); tid != "" {
			// Set tracing header in addition to using TraceIDWriter.
			// TraceIDWriter does not cover cases where implicit writes are performed.
			w.Header().Set(IDHeader, tid)
		}
		h.ServeHTTP(&TraceIDWriter{w, tid}, r)
	})
}

// setSpanErrorStatus marks span as failed with the given code and description.
func setSpanErrorStatus(span oteltrace.Span, code grpccodes.Code, description string) {
	span.SetAttributes(
		semconv.ErrorTypeKey.String(code.String()),
	)
	span.SetStatus(codes.Error, description)
}

// SetError sets the error status code and message for the given span.
// The helper avoids the string-formatting operation for the error if the span is not recorded.
//
// Deprecated: Use native OpenTelemetry [trace.Span.SetStatus] instead.
// If you really need to attach code to the span, use [SetSpanError].
func SetError(span *trace.Span, statusCode int, message string, err error) {
	if !span.IsRecordingEvents() {
		return
	}
	span.SetStatus(trace.Status{Code: int32(statusCode), Message: fmt.Sprintf("%s: %v", message, err)})
}

// SetSpanError marks span as failed with the given gRPC status code and message.
//
// The helper avoids the string-formatting operation for the error if the span is not recorded.
func SetSpanError(span oteltrace.Span, code grpccodes.Code, message string, err error) {
	if !span.IsRecording() {
		return
	}
	setSpanErrorStatus(span, code, fmt.Sprintf("%s: %v", message, err))
}

// SetErrorf sets the error status code and message for the given span.
// The helper avoids the string-formatting operation for the error if the span is not recorded.
//
// Deprecated: Use native OpenTelemetry [trace.Span.SetStatus] instead.
// If you really need to attach code to the span, use [SetSpanErrorf].
func SetErrorf(span *trace.Span, statusCode int, format string, a ...any) {
	if !span.IsRecordingEvents() {
		return
	}
	span.SetStatus(trace.Status{Code: int32(statusCode), Message: fmt.Sprintf(format, a...)})
}

// SetSpanErrorf marks span as failed with the given gRPC status code and the formatted message.
//
// The helper avoids the string-formatting operation if the span is not recorded.
func SetSpanErrorf(span oteltrace.Span, code grpccodes.Code, format string, a ...any) {
	if !span.IsRecording() {
		return
	}
	setSpanErrorStatus(span, code, fmt.Sprintf(format, a...))
}

// StatusWithError takes span and error and treats error as grpc status
// to set status on the span. Returns error for easy daisy-chaining.
//
// Deprecated: Use [SpanStatusWithError] instead.
func StatusWithError(span *trace.Span, err error) error {
	if err != nil {
		errStat, _ := status.FromError(err)
		span.SetStatus(trace.Status{
			Code:    int32(errStat.Code()),
			Message: errStat.Message(),
		})
	}
	return err
}

// SpanStatusWithError marks span as failed, treating err as a gRPC status to
// derive the error code. Returns err for easy daisy-chaining.
func SpanStatusWithError(span oteltrace.Span, err error) error {
	if err == nil || !span.IsRecording() {
		return err
	}
	// FromError never returns a nil status for a non-nil error.
	errStat, _ := status.FromError(err)
	setSpanErrorStatus(span, errStat.Code(), errStat.Message())
	return err
}

// ContextSpanAddAttributes adds attributes to the current span in the context.
// If there is no current span, this is a no-op.
//
// Deprecated: Use [AddSpanAttributes] instead.
func ContextSpanAddAttributes(ctx context.Context, attributes ...trace.Attribute) {
	span := trace.FromContext(ctx)
	if span == nil {
		return
	}
	span.AddAttributes(attributes...)
}

// AddSpanAttributes adds attributes to the current span in the context.
// If there is no current span, this is a no-op.
func AddSpanAttributes(ctx context.Context, attributes ...attribute.KeyValue) {
	oteltrace.SpanFromContext(ctx).SetAttributes(attributes...)
}

// LocalBased returns a sampler that delegates to a sampler stored in the context
// if one is present, falling back to the provided delegate sampler otherwise.
func LocalBased(delegate sdktrace.Sampler) sdktrace.Sampler {
	return localSampler{
		delegate: delegate,
	}
}

type localSampler struct {
	delegate sdktrace.Sampler
}

func (s localSampler) Description() string {
	return "local sampler wrapping: " + s.delegate.Description()
}

func (s localSampler) ShouldSample(p sdktrace.SamplingParameters) sdktrace.SamplingResult {
	val, ok := p.ParentContext.Value(localSamplerKey{}).(sdktrace.Sampler)
	if ok {
		return val.ShouldSample(p)
	}
	return s.delegate.ShouldSample(p)
}

// WithLocalSampler returns a context containing the specified sampler.
// The LocalBased sampler will use this sampler for sampling decisions.
//
// WARNING: Using WithLocalSampler to force a sampling decision (e.g., with AlwaysSample)
// is highly discouraged for general use because it can break trace continuity (e.g.,
// if a parent span is not sampled but a child is).
//
// For standard production environments, prefer tail-based sampling:
// https://opentelemetry.io/docs/concepts/sampling/#tail-sampling
func WithLocalSampler(ctx context.Context, sampler sdktrace.Sampler) context.Context {
	return context.WithValue(ctx, localSamplerKey{}, sampler)
}

// WithAlwaysSample returns a copy of ctx that forces sampling of every span started from this context.
// Discouraged for general use, see [WithLocalSampler].
func WithAlwaysSample(ctx context.Context) context.Context {
	return WithLocalSampler(ctx, sdktrace.AlwaysSample())
}

// WithNeverSample returns a copy of ctx that prevents sampling of any span started from this context.
// Discouraged for general use, see [WithLocalSampler].
func WithNeverSample(ctx context.Context) context.Context {
	return WithLocalSampler(ctx, sdktrace.NeverSample())
}
