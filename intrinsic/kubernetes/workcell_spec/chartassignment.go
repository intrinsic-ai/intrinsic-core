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

// Package chartassignment has functions for working with chartassignment CRs
package chartassignment

import (
	"bytes"
	"compress/gzip"
	"context"
	"encoding/base64"
	"fmt"
	"io"
	"maps"
	"regexp"
	"slices"
	"strings"
	"time"

	appstat "intrinsic/kubernetes/appstat/inspect"
	"intrinsic/stats/go/telemetry"

	"dario.cat/mergo"
	"github.com/bazelbuild/buildtools/labels"
	backoff "github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"github.com/google/safearchive/tar"
	"github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/googlecloudrobotics/core/src/go/pkg/client/versioned"
	"github.com/pkg/errors"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/propagation"
	oteltrace "go.opentelemetry.io/otel/trace"
	k8serrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/util/wait"
	"k8s.io/client-go/kubernetes"
	kubeyaml "sigs.k8s.io/yaml"

	apipb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
)

const (
	// PartOfAppLabel is the name of the label containing the app name that a chart belongs to.
	PartOfAppLabel = "app.kubernetes.io/part-of"
)

// ErrStatusUpdateTimeout is returned when the status update of the chart assignment timed out.
var ErrStatusUpdateTimeout = errors.New("chart assignment status update timed out")

// kubeBackOff returns a suitable backoff policy for a local Kubernetes apiserver.
func kubeBackOff(ctx context.Context) backoff.BackOff {
	b := backoff.NewExponentialBackOff()
	b.InitialInterval = 10 * time.Millisecond
	return backoff.WithContext(b, ctx)
}

func isDeleted(ctx context.Context, clientset versioned.Interface, kubernetes kubernetes.Interface, name string) (bool, error) {
	ca, err := clientset.AppsV1alpha1().ChartAssignments().Get(ctx, name, metav1.GetOptions{})
	if err == nil {
		if ca.ObjectMeta.DeletionTimestamp == nil {
			// If the chart was deleted & recreated in the meantime, the deletion timestamp
			// switches to nil. We should stop polling then.
			return true, nil
		}
		return false, nil
	}
	if !k8serrors.IsNotFound(err) {
		// There was some error in communicating with K8s, return the error.
		return true, err
	}
	// Workaround for b/194763373: After the chartassignment has been deleted, additionally do a
	// sync wait for all resources that need to be deleted.
	_, err = kubernetes.CoreV1().Namespaces().Get(ctx, "app-"+name, metav1.GetOptions{})
	if err == nil {
		return false, nil
	}
	if k8serrors.IsNotFound(err) {
		return true, nil
	}
	return true, err
}

func isTransient(err error) bool {
	if k8serrors.IsConflict(err) {
		// Optimistic locking failed, so we need to retry.
		return true
	}
	if strings.Contains(err.Error(), "etcdserver: request timed out") {
		// I don't think we can match this better because (a) the etcd error type is not preserved when
		// it passes through client-go and (b) the error has restricted Bazel visibility.
		// http://third_party/golang/go_etcd_io/etcd/v/v0/etcdserver/errors.go;l=26;rcl=576448169
		return true
	}
	return false
}

// Delete deletes chart assignments and optionally waits on their finalizer
func Delete(ctx context.Context, clientset versioned.Interface, kubernetes kubernetes.Interface, name string, waitForFinalizer bool) error {
	// Since there are finalizers on chart assignments, "Delete" will set a deletion
	// timestamp but not actually delete the resource.
	err := clientset.AppsV1alpha1().ChartAssignments().Delete(ctx, name, metav1.DeleteOptions{})
	if err != nil {
		return err
	}
	// Remove all extra charts.
	caList, err := clientset.AppsV1alpha1().ChartAssignments().List(ctx, metav1.ListOptions{LabelSelector: fmt.Sprintf("%s=%s", PartOfAppLabel, name)})
	if err != nil {
		return err
	}
	for _, ca := range caList.Items {
		if err := clientset.AppsV1alpha1().ChartAssignments().Delete(ctx, ca.GetName(), metav1.DeleteOptions{}); err != nil {
			// Ignore NotFound errors. This can race with ownerReferences.
			// TODO(b/277549810): Remove when replacing custom part-of label with ownerReferences.
			if !k8serrors.IsNotFound(err) {
				return err
			}
		}
	}

	if !waitForFinalizer {
		return nil
	}

	// Wait for the resources to be deleted. Otherwise, an immediately following "app start"
	// might not be able to recreate the app.
	chartNames := []string{name}
	for _, ca := range caList.Items {
		chartNames = append(chartNames, ca.GetName())
	}
	for _, cn := range chartNames {
		err = wait.PollImmediate(25*time.Millisecond, 300*time.Second, func() (bool, error) {
			return isDeleted(ctx, clientset, kubernetes, cn)
		})
		if err != nil {
			return errors.Wrap(err, "chart post-delete poll")
		}
	}
	return nil
}

// DeleteAndWait deletes chart assignments and waits on their finalizer
func DeleteAndWait(ctx context.Context, clientset versioned.Interface, kubernetes kubernetes.Interface, name string) error {
	return Delete(ctx, clientset, kubernetes, name, true)
}

func injectTraceContext(ctx context.Context, ca *v1alpha1.ChartAssignment) {
	if !oteltrace.SpanFromContext(ctx).SpanContext().IsValid() {
		return
	}
	if ca.GetAnnotations() == nil {
		ca.SetAnnotations(make(map[string]string))
	}
	(propagation.TraceContext{}).Inject(ctx, propagation.MapCarrier(ca.GetAnnotations()))
}

// CreateOrUpdate creates a new chart assignment or updates an existing one
// When updating an existing resource, optimistic locking means we may need to retry.
func CreateOrUpdate(ctx context.Context, clientset versioned.Interface, ca *v1alpha1.ChartAssignment) (*v1alpha1.ChartAssignment, error) {
	ctx, span := telemetry.StartSpan(ctx, "chartassignment.CreateOrUpdate")
	defer span.End()

	injectTraceContext(ctx, ca)

	// TODO(rodrigoq): Can server-side apply make this simpler?
	caClient := clientset.AppsV1alpha1().ChartAssignments()
	retryFunc := func() (*v1alpha1.ChartAssignment, error) {
		existing, err := caClient.Get(ctx, ca.GetName(), metav1.GetOptions{})
		if k8serrors.IsNotFound(err) {
			ca, err := caClient.Create(ctx, ca, metav1.CreateOptions{})
			if err != nil {
				return nil, backoff.Permanent(err)
			}
			return ca, nil
		}
		if err != nil {
			// An error other than not found is permanent.
			return nil, backoff.Permanent(err)
		}
		// We want to completely overwrite the target, but we need to apply the update
		// to a specific resource version.
		ca.SetResourceVersion(existing.GetResourceVersion())
		ca, err := caClient.Update(ctx, ca, metav1.UpdateOptions{})
		if k8serrors.IsConflict(err) {
			// Optimistic locking failed, so we need to retry.
			return nil, err
		} else if err != nil {
			return nil, backoff.Permanent(err)
		}
		return ca, nil
	}
	var returnCA *v1alpha1.ChartAssignment
	err := backoff.Retry(func() error {
		var err error
		returnCA, err = retryFunc()
		return err
	}, kubeBackOff(ctx))
	return returnCA, err
}

// WaitForHealthy waits until the chart assignment is considered healthy.  This
// always checks if the chart-assignment controller has applied objects.
// Additionally it will run diagnostics on those objects if a contextName is
// provided and skipAppstat is not set.
func WaitForHealthy(ctx context.Context, clientset versioned.Interface, name string, clients appstat.KubeInterface, updates io.Writer) error {
	// TODO(b/249235890): Replace this waitForChartAssignment mechanism by a version based
	// on the deploy service to get a pure gRPC based system.
	ctx, span := telemetry.StartSpan(ctx, "waitForChartAssignment", oteltrace.WithAttributes(
		attribute.String("chart_assignment_name", name),
	))
	defer span.End()
	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()
	var numPrintedLines int
	var nErrors int
	var namespaceName string
	for {
		select {
		case <-ctx.Done():
			return ErrStatusUpdateTimeout
		case <-ticker.C:
		}

		ca, err := clientset.AppsV1alpha1().ChartAssignments().Get(ctx, name, metav1.GetOptions{})
		if err != nil {
			if isTransient(err) && nErrors < 10 {
				nErrors = nErrors + 1
				continue
			}
			return errors.Wrap(err, "getting ChartAssignment")
		}
		namespaceName = ca.Spec.NamespaceName

		msg, updated := getStatus(ca)
		clearPrintedLines(updates, numPrintedLines)
		fmt.Fprintf(updates, "\nApp status for %q at %s: %s\n", name, time.Now().Format("2006-01-02 15:04:05"), msg)
		numPrintedLines = strings.Count(msg, "\n") + 2

		if updated {
			if ca.Status.Phase == v1alpha1.ChartAssignmentPhaseFailed {
				return fmt.Errorf("updating chartassignment %q: %s", name, msg)
			}
			break
		}
	}
	clearPrintedLines(updates, numPrintedLines)

	// Skip appstat if no clients are provided.
	if clients == nil {
		return nil
	}
	if err := appstat.StatApp(ctx, clients, &appstat.Options{
		Namespace: namespaceName,
		Oneshot:   false,
	}); err != nil {
		return fmt.Errorf("StatApp: %w", err)
	}
	fmt.Fprint(updates, "\n")

	return nil
}

// clearPrintedLines clears numLines of output to stdout. This assumes
// that the current line is empty, i.e. that the last printed char was a '\n'.
func clearPrintedLines(w io.Writer, numLines int) {
	// Attempting to clear 0 lines is not a no-op but will clear 1 line.
	if numLines <= 0 {
		return
	}
	fmt.Fprintf(w, "\x1b[%dF", numLines) // Move cursor up numLines.
	fmt.Fprint(w, "\x1b[J")              // Clear from cursor until end of screen.
}

const chartAssignmentDeleteTemplate = `

		Try:

		$ kubectl --context %s delete chartassignment %s

		You might need to delete the chart assignment in the cloud cluster as well.
		Ask in go/intrinsic-platform-support-chat for further help.

`

// getStatus identifies whether the ChartAssignment controller has
// updated the ChartAssignment to the latest spec. It returns a human-readable
// message describing the status, as well as a bool indicating whether the
// ChartAssignment is in a terminal phase (Settled/Ready/Failed).
func getStatus(ca *v1alpha1.ChartAssignment) (string, bool) {
	if ca.Status.ObservedGeneration != ca.Generation {
		// ChartAssignment controller has not updated the status since the last spec update.
		return "Waiting for status update", false
	}
	// Naively concat and print all messages from the Status conditions.
	// TODO(alexanderfaxa): Identify the relevant condition.
	messages := make([]string, len(ca.Status.Conditions))
	for i, c := range ca.Status.Conditions {
		messages[i] = c.Message
		if strings.Contains(messages[i], "owner conflict:") {
			messages[i] = messages[i] + fmt.Sprintf(chartAssignmentDeleteTemplate, ca.Spec.ClusterName, ca.Name)
		}
	}
	msg := strings.TrimSpace(strings.Join(messages, "\n"))
	if msg == "" {
		msg = "Creating"
	}

	updated := ca.Status.Phase == v1alpha1.ChartAssignmentPhaseSettled ||
		ca.Status.Phase == v1alpha1.ChartAssignmentPhaseReady ||
		ca.Status.Phase == v1alpha1.ChartAssignmentPhaseFailed

	return msg, updated
}

// Install installs the given chart assignment in the target cluster.
func Install(ctx context.Context, clusterName, installUser string, ca *v1alpha1.ChartAssignment, values v1alpha1.ConfigValues, appsCS versioned.Interface, dryRun bool) error {
	// TODO(mikhailpak): Pass user as a parameter.
	ca.GetLabels()["installed-by"] = installUser
	injectTraceContext(ctx, ca)
	ca.Spec.ClusterName = clusterName
	// Merge values from the released chart assignment (contains feature options) with runtime based values.
	if err := mergo.Merge(&ca.Spec.Chart.Values, values, mergo.WithOverride); err != nil {
		return errors.Wrapf(err, "merging values.yaml with %s chart.Spec.Chart.Values", ca.Name)
	}

	// If the chart doesn't exist in the cluster, create it.
	// If it does, update it if has a different version.
	// When updating an existing resource, optimistic locking means we may need to retry.
	caClient := appsCS.AppsV1alpha1().ChartAssignments()
	return backoff.Retry(func() error {
		existing, err := caClient.Get(ctx, ca.GetName(), metav1.GetOptions{})
		if k8serrors.IsNotFound(err) {
			log.InfoContextf(ctx, "No previous version of %q found. Installing it.", ca.Name)
			if dryRun {
				log.InfoContextf(ctx, "DRY RUN: Not creating chart %s on %s", ca.Name, clusterName)
				return nil
			}
			if _, err := caClient.Create(ctx, ca, metav1.CreateOptions{}); err != nil {
				return checkInstallationError(err)
			}
			return nil
		}
		if err != nil {
			// An error other than not found is permanent.
			return backoff.Permanent(err)
		}

		if ca.ObjectMeta.DeletionTimestamp != nil {
			log.InfoContextf(ctx, "Waiting for %q to be fully deleted.", ca.Name)
			return fmt.Errorf("saw deletion timestamp %v on %q", ca.ObjectMeta.DeletionTimestamp, ca.Name)
		}
		log.InfoContextf(ctx, "Updating to a new version of %q.", ca.Name)
		// We want to completely overwrite the target, but we need to apply the update
		// to a specific resource version.
		ca.SetResourceVersion(existing.GetResourceVersion())
		if dryRun {
			log.InfoContextf(ctx, "DRY RUN: Not installing chart %s to %s", ca.Name, clusterName)
			return nil
		}
		if _, err := caClient.Update(ctx, ca, metav1.UpdateOptions{}); k8serrors.IsConflict(err) {
			// Optimistic locking failed, so we need to retry.
			log.Infof("optimistic locking failed, retrying: %v", err)
			return err
		} else if err != nil {
			return checkInstallationError(err)
		}
		return nil
	}, kubeBackOff(ctx))
}

func checkInstallationError(err error) error {
	if strings.Contains(err.Error(), "failed calling webhook") {
		// TODO(b/198282031): We're commonly seeing nodes breaking when under I/O pressure.
		// This can also happen if the chart-assignment-controller was just installed and
		// has not come up yet.
		log.Infof("transient error, retrying: %v", err)
		return err
	}
	return backoff.Permanent(err)
}

// NormalizedTargetName takes a target and returns a normalized version of it.
func NormalizedTargetName(target string) string {
	label := labels.Parse(target)
	repo := label.Repository
	if repo == "" {
		return fmt.Sprintf("//%s:%s", label.Package, label.Target)
	}
	return fmt.Sprintf("@%s//%s:%s", repo, label.Package, label.Target)
}

// ChartName extracts the name of the chart for the given workcell spec target
// by querying for all chart assignments in the current context and inspecting
// their annotations.
func ChartName(ctx context.Context, cs versioned.Interface, target string) (string, error) {
	cas, err := cs.AppsV1alpha1().ChartAssignments().List(ctx, metav1.ListOptions{})
	if err != nil {
		return "", errors.Wrap(err, "list chart assignments")
	}
	normalizedTargetName := NormalizedTargetName(target)
	var allTargets []string
	for _, ca := range cas.Items {
		wst := ca.Annotations["workcell-spec-target"]
		if wst == normalizedTargetName {
			return ca.Name, nil
		}
		allTargets = append(allTargets, wst)
	}
	return "", fmt.Errorf("did not find deployed chart assignment for %q. Available targets: %v", normalizedTargetName, allTargets)
}

// FromWorkcellSpec extracts all chart assignments from a [apipb.WorkcellSpec]. Returns an error if there
// were no chart assignments in the workcell spec or if any of them could not be deserialized from
// YAML.
func FromWorkcellSpec(spec *apipb.WorkcellSpec) ([]*v1alpha1.ChartAssignment, error) {
	var cas []*v1alpha1.ChartAssignment
	for _, s := range spec.GetItems() {
		caPB := s.GetChartAssignment()
		if caPB == nil {
			continue
		}
		ca := new(v1alpha1.ChartAssignment)
		if err := kubeyaml.Unmarshal(caPB.GetYaml(), ca); err != nil {
			return nil, errors.Wrap(err, "parse ChartAssignment")
		}
		cas = append(cas, ca)
	}
	return cas, nil
}

// AppName of the workcell spec, which is the "app" label on the first chart.
// Using the app label matches the transfer service, but this is equivalent to
// the name field in metadata of the chartassignment, as chartassignmentgen
// sets both values to be the same.
func AppName(spec *apipb.WorkcellSpec) (string, error) {
	for _, s := range spec.GetItems() {
		caPB := s.GetChartAssignment()
		if caPB == nil {
			continue
		}
		ca := new(v1alpha1.ChartAssignment)
		if err := kubeyaml.Unmarshal(caPB.GetYaml(), ca); err != nil {
			return "", errors.Wrap(err, "parse ChartAssignment")
		}
		if name, ok := ca.Labels["app"]; ok {
			return name, nil
		}
		return "", errors.New("chart assignment is missing app label")
	}
	return "", errors.New("workcell spec is missing a chart assignment")
}

// SetAnnotation sets an annotation on the chart assignment.
func SetAnnotation(ca *v1alpha1.ChartAssignment, key, value string) {
	if ca.GetAnnotations() == nil {
		ca.SetAnnotations(make(map[string]string))
	}
	ca.GetAnnotations()[key] = value
}

// SetLabel sets a label on the chart assignment.
func SetLabel(ca *v1alpha1.ChartAssignment, key string, value any) {
	if ca.GetLabels() == nil {
		ca.SetLabels(make(map[string]string))
	}
	ca.GetLabels()[key] = fmt.Sprintf("%v", value)
}

// SetValue sets a value on the chart assignment.
func SetValue(ca *v1alpha1.ChartAssignment, key string, value any) {
	if ca.Spec.Chart.Values == nil {
		ca.Spec.Chart.Values = make(v1alpha1.ConfigValues)
	}
	ca.Spec.Chart.Values[key] = value
}

// MergeValues merges values onto the chart assignment.
func MergeValues(ca *v1alpha1.ChartAssignment, values v1alpha1.ConfigValues) error {
	if ca.Spec.Chart.Values == nil {
		ca.Spec.Chart.Values = make(v1alpha1.ConfigValues)
	}
	if err := mergo.Merge(&ca.Spec.Chart.Values, values, mergo.WithOverride); err != nil {
		return errors.Wrap(err, "merging")
	}
	return nil
}

// InlineChart returns an inline chart with the given files. It's intended for testing.
func InlineChart(content map[string][]byte) string {
	outputWriter := &bytes.Buffer{}
	base64Writer := base64.NewEncoder(base64.StdEncoding, outputWriter)
	gzipWriter := gzip.NewWriter(base64Writer)
	tarWriter := tar.NewWriter(gzipWriter)

	for _, name := range slices.Sorted(maps.Keys(content)) {
		tarWriter.WriteHeader(&tar.Header{
			Name: name,
			Size: int64(len(content[name])),
		})
		tarWriter.Write(content[name])
	}

	tarWriter.Close()
	gzipWriter.Close()
	base64Writer.Close()
	return outputWriter.String()
}

// FileModifier is a function that modifies the content of a file in an inline Helm chart.
type FileModifier func(content []byte) ([]byte, error)

// ModifyInlineChart decodes an inline Helm chart, modifies a specific file using the provided
// modifier function, and returns a new inline Helm chart that can replace the previous one.
// It fails if the inline chart is empty or can't be decoded.
func ModifyInlineChart(ca *v1alpha1.ChartAssignment, targetFileName *regexp.Regexp, modifierFunc FileModifier) error {
	if ca.Spec.Chart.Inline == "" {
		return fmt.Errorf("ChartAssignment %q has no inline chart", ca.Name)
	}

	// The inline chart is a base64-encoded gzipped tarball, so we need a chain of readers and writers
	// to modify it.
	inputReader := bytes.NewBufferString(ca.Spec.Chart.Inline)
	binaryReader := base64.NewDecoder(base64.StdEncoding, inputReader)
	gzipReader, err := gzip.NewReader(binaryReader)
	if err != nil {
		return fmt.Errorf("gzip.NewReader: %w", err)
	}
	defer gzipReader.Close()
	tarReader := tar.NewReader(gzipReader)

	outputWriter := &bytes.Buffer{}
	base64Writer := base64.NewEncoder(base64.StdEncoding, outputWriter)
	defer base64Writer.Close()
	gzipWriter := gzip.NewWriter(base64Writer)
	defer gzipWriter.Close()
	tarWriter := tar.NewWriter(gzipWriter)
	defer tarWriter.Close()

	// Iterate through the entries in the input tarball, copying all files except the target file,
	// which gets modified.
	modified := false
	for {
		header, err := tarReader.Next()
		if err == io.EOF {
			break // End of archive
		}
		if err != nil {
			return fmt.Errorf("read tar header: %w", err)
		}

		content, err := io.ReadAll(tarReader)
		if err != nil {
			return fmt.Errorf("read file content from tar: %w", err)
		}

		if targetFileName.MatchString(header.Name) {
			modifiedContent, err := modifierFunc(content)
			if err != nil {
				return fmt.Errorf("modify file %q: %w", targetFileName, err)
			}
			content = modifiedContent
			header.Size = int64(len(modifiedContent))
			modified = true
		}

		// Write the (potentially modified) header+content to the new tarball.
		if err := tarWriter.WriteHeader(header); err != nil {
			return fmt.Errorf("write tar header: %w", err)
		}
		if _, err := tarWriter.Write(content); err != nil {
			return fmt.Errorf("write content: %w", err)
		}
	}

	if !modified {
		return fmt.Errorf("target file %q not found", targetFileName)
	}
	// Close writers to make sure they flush before we read from the buffer. The deferred calls will
	// repeat Close() but that's harmless.
	tarWriter.Close()
	gzipWriter.Close()
	base64Writer.Close()
	ca.Spec.Chart.Inline = outputWriter.String()
	return nil
}

// UpdateWorkcellSpec applies the given handler to all chart assignments in the workcell spec.
func UpdateWorkcellSpec(handler func(i int, ca *v1alpha1.ChartAssignment) error, spec *apipb.WorkcellSpec) error {
	for i, item := range spec.Items {
		caPB := item.GetChartAssignment()
		if caPB == nil {
			continue
		}
		ca := &v1alpha1.ChartAssignment{}
		if err := kubeyaml.Unmarshal(caPB.GetYaml(), ca); err != nil {
			return errors.Wrap(err, "unmarshal ChartAssignment")
		}

		if err := handler(i, ca); err != nil {
			return err
		}

		b, err := kubeyaml.Marshal(ca)
		if err != nil {
			return errors.Wrap(err, "marshall ChartAssignment")
		}
		caPB.Yaml = b
		return nil
	}
	return nil
}

// IsExclusive returns whether or not the chart is marked as exclusive.
// The original behavior of this function was preserved, however, that may be
// surprising in the absence of labels or invalid ones.  This function should
// probably be made more strict and return more than just a bool.  Options
// would be (bool, error) or an enum indicating invalid, not exclusive,
// exclusive.
func IsExclusive(ca *v1alpha1.ChartAssignment) bool {
	// This label is the string version of a go boolean generated here:
	// http://intrinsic/kubernetes/workcell_spec/chartassignmentgen.go?l=51
	return ca.Labels["exclusive"] != fmt.Sprintf("%v", false)
}
