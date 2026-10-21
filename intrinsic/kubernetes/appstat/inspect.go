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

// Package inspect contains the logic for the appstat command:
// http://intrinsic/kubernetes/appstat/README.md
package inspect

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"regexp"
	"strings"
	"syscall"
	"time"

	"intrinsic/kubernetes/appstat/generatedpodsuffix"
	"intrinsic/kubernetes/appstat/pollingspans"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"go.opencensus.io/trace"
	"golang.org/x/sys/unix"
	appsv1 "k8s.io/api/apps/v1"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	apierrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime/schema"
	"k8s.io/client-go/dynamic"
	"k8s.io/client-go/kubernetes"
	tcorev1 "k8s.io/client-go/kubernetes/typed/core/v1"
	"k8s.io/client-go/rest"

	_ "k8s.io/client-go/plugin/pkg/client/auth/gcp"
)

const (
	pollInterval         = 3 * time.Second
	minConditionAge      = 30 * time.Second
	containerTimeToCrash = 5 * time.Second
	jobPodWarningTimeout = 60 * time.Second
	timeFormat           = "15:04:05"
	maxLogLines          = 5

	deploymentKind     = "Deployment"
	podKind            = "Pod"
	jobKind            = "Job"
	virtualServiceKind = "VirtualService"
)

var virtualServiceGVR = schema.GroupVersionResource{
	Group:    "networking.istio.io",
	Version:  "v1beta1",
	Resource: "virtualservices",
}

// timeNow allows stubbing time.Now() in tests.
var timeNow = time.Now

// Stubbed out for testing since the basic fake client does not allow overriding of "fake logs" as
// as the return.
var getLogs = func(pods tcorev1.PodExpansion, name string, opts *corev1.PodLogOptions) *rest.Request {
	return pods.GetLogs(name, opts)
}

// KubeInterface wraps a Kubernetes client so that it can be stubbed for testing.
type KubeInterface interface {
	K8sInterface
	DynInterface
}

// LogMode is a mode readable alternative to `PodLogOptions.Previous bool` that
// says whether to get the current or previous container logs.
type LogMode bool

const (
	// Previous refers to the previously terminated container logs.
	Previous LogMode = true
	// Current refers to the currently running container logs.
	Current LogMode = false
)

// Aliases to allow two interfaces of the same type to be embedded in the
// client struct.
type (
	K8sInterface = kubernetes.Interface
	DynInterface = dynamic.Interface
)

// Client is an implementation of KubeInterface that talks to a Kubernetes apiserver.
type Client struct {
	K8sInterface
	DynInterface
}

// Category classifies the Diagnostics to make it easier to see what needs attention.
type Category int

const (
	// Error indicates that the resource is in a error state.
	Error Category = iota
	// Warning indicates that bringing the resource up triggered warnings.
	Warning
	// Pending indicates that the resource is in a transient unready state.
	Pending
	// Ready shows that the resource has been successfully brought up.
	Ready
)

// Diagnostic provides human-readable information for a potential problem in the
// Kubernetes cluster.
type Diagnostic struct {
	// Kind of the object that this concerns, eg "pod"
	Kind string
	// Name of the object that this concerns.
	Name string
	// Optionally, name of the container that this concerns. Empty for pod-level
	// diagnostics like "not scheduled".
	Container string
	// A short-ish message, e.g., "is not running", "exited with code 1".
	Message string
	// A longer message, possibly with multiple lines, e.g., recent log messages.
	Detail string
	// Category of the diagnostics for visual filtering and highlighting.
	Category Category
	// HasDeletionTimestamp is "true" if the object has a deletion timestamp.
	HasDeletionTimestamp bool
}

// ColorString returns a string with the object identifier (eg pod, container
// name) wrapped in an ansi color code representing the category.
func (d *Diagnostic) ColorString() string {
	kindColors := map[Category]string{
		Error:   "\033[31m", // red
		Warning: "\033[33m", // yellow
		Pending: "",         // no-highlight
		Ready:   "\033[32m", // green
	}
	colorCode := kindColors[d.Category]
	resetCode := ""
	if colorCode != "" {
		resetCode = "\033[0m"
	}

	if d.Container == "" {
		return fmt.Sprintf("%s%s %s%s %s", colorCode, d.Kind, d.Name, resetCode, d.Message)
	}
	return fmt.Sprintf("%s%s %s: container %s%s %s", colorCode, d.Kind, d.Name, d.Container, resetCode, d.Message)
}

// nameForSpan returns a string that can be used as a Cloud Trace name. This
// means that it may not be noisy (e.g. no seconds count), so we omit the
// [Diagnostic.Detail] and [Diagnostic.Message]. Furthermore, we redact the
// pod name so that we don't get replica set and pod hashes. This allows us to
// run efficient BigQuery queries to look for the slowest pods.
func (d *Diagnostic) nameForSpan() string {
	name := d.Name
	if d.Kind == podKind {
		name = generatedpodsuffix.Trim(name)
	}
	res := ""
	if d.Container == "" {
		res = fmt.Sprintf("%s %s", d.Kind, name)
	} else {
		res = fmt.Sprintf("%s %s: container %s", d.Kind, name, d.Container)
	}
	if d.HasDeletionTimestamp {
		res = fmt.Sprintf("REMOVING %s", res)
	}
	return res
}

// DetailString returns a string with detailed information, possibly multiple
// lines, e.g.,
//
//	"Summary.\n" +
//	"    Detailed information\n" +
//	"    More detailed information"
func (d *Diagnostic) DetailString() string {
	if d.Detail == "" {
		return d.ColorString()
	}
	indentedDetail := strings.ReplaceAll(strings.TrimSpace(d.Detail), "\n", "\n    ")
	return fmt.Sprintf("%s\n    %s", d.ColorString(), indentedDetail)
}

// isOldEvent filters out events that are likely to no longer apply. Since
// the timestamp update frequency varies for different events, the maximum
// age depends on the event reason.
func isOldEvent(e corev1.Event) bool {
	age := timeNow().Sub(e.LastTimestamp.Time).Round(time.Second)
	if e.Reason == "Unhealthy" {
		// Unhealthy events indicate that a readiness probe failed. These events can
		// occur transiently if initialDelaySeconds is too small, so forget them
		// sooner.
		// https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/
		return age > 30*time.Second
	}
	// For other events, we are more conservative. In particular, the "Failed"
	// warning occurs whenever image pull fails, which backs off to ~5 minutes.
	return age > 10*time.Minute
}

// condition returns the value of the given pod condition.
func condition(pod corev1.Pod, conditionType corev1.PodConditionType) bool {
	for _, c := range pod.Status.Conditions {
		if c.Type == conditionType {
			return c.Status == corev1.ConditionTrue
		}
	}
	return false
}

// isNoisyEvent filters out events that don't help users debug.
func isNoisyEvent(e corev1.Event, pod corev1.Pod) bool {
	if e.Reason == "DNSConfigForming" && strings.HasPrefix(e.Message, "Search Line limits were exceeded") {
		// This event appears for all pods in clusters on lab machines.
		return true
	}
	if e.Reason == "FailedMount" && strings.HasPrefix(e.Message, "Unable to attach or mount volumes") {
		// https://github.com/kubernetes/kubernetes/issues/88226
		return true
	}
	if e.Reason == "FailedMount" && strings.HasSuffix(e.Message, "cache: timed out waiting for the condition") {
		// b/161330120
		return true
	}
	if e.Reason == "FailedMount" && pod.Status.Phase != corev1.PodPending {
		// The pod is no longer pending, so this event must be stale.
		return true
	}
	if e.Reason == "BackOff" {
		// This just tells you that the container has crashed, but not why.
		return true
	}
	if e.Reason == "FailedScheduling" {
		// This information is already extracted from the pod status.
		return true
	}
	isImagePullErr := e.Reason == "Failed" && (e.Message == "Error: ErrImagePull" ||
		e.Message == "Error: ImagePullBackOff" ||
		strings.HasPrefix(e.Message, "Failed to pull image"))
	if isImagePullErr && pod.Status.Phase != corev1.PodPending {
		// b/178582636
		// The pod is no longer pending, so the pull error must have been resolved.
		return true
	}
	if e.Reason == "FailedCreatePodSandBox" && strings.HasSuffix(e.Message, "putEndpointIdTooManyRequests") {
		// The rate limiter warning is transient. b/359104994 tracks investigation of whether these
		// warnings indicate solution start is slower than necessary.
		return true
	}
	return false
}

// eventDiagnostic aggregates all recent warning events into a diagnostic. If
// no events match, it returns ok=false.
func eventDiagnostic(events *corev1.EventList, pod corev1.Pod) (*Diagnostic, bool) {
	var found bool
	var sb strings.Builder
	for _, e := range events.Items {
		if e.InvolvedObject.Name != pod.Name || e.Type != corev1.EventTypeWarning || isOldEvent(e) || isNoisyEvent(e, pod) {
			continue
		}
		if condition(pod, corev1.PodReady) || pod.Status.Phase == corev1.PodSucceeded {
			log.V(1).Infof("Suppressing event %q with message %q because pod is ready or succeeded", e.Reason, e.Message)
			continue
		}
		if !found {
			found = true
			sb.WriteString(e.Message)
		} else {
			sb.WriteByte('\n')
			sb.WriteString(e.Message)
		}
		if e.Reason == "UnexpectedAdmissionError" {
			sb.WriteString("\nTry restarting with inctl solution stop then app start (b/150675787).")
		}
	}
	if !found {
		return nil, false
	}
	if strings.Contains(sb.String(), "/globalmount") {
		sb.WriteString("\nTry this workaround: (b/174784536)")
		sb.WriteString("\n  sudo umount /var/lib/kubelet/plugins/kubernetes.io/csi/pv/workcell-spec/globalmount")
	}
	return &Diagnostic{
		Kind:     podKind,
		Name:     pod.Name,
		Message:  "has recent warning events",
		Detail:   sb.String(),
		Category: Warning,
	}, true
}

// updateDiagnostic checks for a deployment update that has not been handled by
// kube-controller-manager. https://stackoverflow.com/a/66318891
func updateDiagnostic(deployment appsv1.Deployment) (*Diagnostic, bool) {
	if deployment.Generation == deployment.Status.ObservedGeneration &&
		deployment.Status.Replicas == deployment.Status.UpdatedReplicas {
		return nil, false
	}
	return &Diagnostic{
		Kind:     deploymentKind,
		Name:     deployment.Name,
		Message:  "has not updated yet",
		Category: Pending,
	}, true
}

// schedulingDiagnostic checks for a pod that hasn't been scheduled. This occurs
// if the pod needs hardware that is not available, or if all nodes are tainted
// due to disk pressure or similar problems.
func schedulingDiagnostic(pod corev1.Pod) (*Diagnostic, bool) {
	for _, c := range pod.Status.Conditions {
		if c.Type == corev1.PodScheduled && c.Status == corev1.ConditionFalse {
			log.V(1).Infof("pod %s not scheduled for reason %q: %s", pod.Name, c.Reason, c.Message)
			d := &Diagnostic{
				Kind:     podKind,
				Name:     pod.Name,
				Message:  "has not been assigned to a cluster node",
				Category: Pending,
			}
			if timeNow().Sub(c.LastTransitionTime.Time) >= minConditionAge {
				// This could be a transient issue, eg if a device plugin is slow to start,
				// so we only upgrade it to an error after a while. It's tempting to
				// suppress the diagnostic entirely, but that could lead to appstat
				// terminating early while the pod is still pending.
				d.Category = Error
				d.Detail = c.Message
			}
			return d, true
		}
	}
	return nil, false
}

// deletionDiagnostic checks for a pod that is being terminated. For more info on
// the deletion process:
// https://kubernetes.io/docs/concepts/workloads/pods/pod/#termination-of-pods
func deletionDiagnostic(pod corev1.Pod) (*Diagnostic, bool) {
	if pod.DeletionTimestamp == nil {
		return nil, false
	}
	log.V(1).Infof("pod %s has deletion timestamp %s", pod.Name, pod.DeletionTimestamp)
	interval := pod.DeletionTimestamp.Time.Sub(timeNow()).Round(time.Second)
	if interval < 0 {
		return &Diagnostic{
			Kind:     podKind,
			Name:     pod.Name,
			Message:  fmt.Sprintf("was killed %s ago", -interval),
			Category: Ready,
		}, true
	}
	return &Diagnostic{
		Kind:                 podKind,
		Name:                 pod.Name,
		Message:              fmt.Sprintf("will be killed in %s", interval),
		Category:             Pending,
		HasDeletionTimestamp: true,
	}, true
}

// inspectJobStatus checks for status of batch jobs:
//   - job failure
//   - job with failed pods
//   - job that hasn't started, yet
//   - job that are running, and that are running a long time, this is useful
//     information in the context of a job because pod termination is a condition
//     for successful job execution.
func inspectJobStatus(job batchv1.Job) (*Diagnostic, bool) {
	for _, condition := range job.Status.Conditions {
		if condition.Type == batchv1.JobFailed && condition.Status == corev1.ConditionTrue {
			return &Diagnostic{
				Kind:     jobKind,
				Name:     job.Name,
				Message:  fmt.Sprintf("has failed (%s)", condition.Message),
				Category: Error,
			}, true
		}
	}
	if job.Status.Failed > 0 {
		return &Diagnostic{
			Kind:     jobKind,
			Name:     job.Name,
			Message:  fmt.Sprintf("has %d failed pods", job.Status.Failed),
			Category: Error,
		}, true
	}
	if job.Status.Active == 0 && job.Status.Succeeded == 0 {
		return &Diagnostic{
			Kind:     jobKind,
			Name:     job.Name,
			Message:  "has not started pods, yet",
			Category: Pending,
		}, true
	}
	if job.Status.Active > 0 && job.Status.Succeeded > 0 {
		return &Diagnostic{
			Kind:     jobKind,
			Name:     job.Name,
			Message:  fmt.Sprintf("has %d running pods (%d already succeeded)", job.Status.Active, job.Status.Succeeded),
			Category: Pending,
		}, true
	}
	if job.Status.Active > 0 {
		return &Diagnostic{
			Kind:     jobKind,
			Name:     job.Name,
			Message:  fmt.Sprintf("has %d running pods", job.Status.Active),
			Category: Pending,
		}, true
	}

	if job.Status.CompletionTime != nil {
		completedFor := timeNow().Sub(job.Status.CompletionTime.Time).Round(time.Second)
		if completedFor <= containerTimeToCrash {
			// Show completed job for a little while to give user confirmation
			return &Diagnostic{
				Kind:     jobKind,
				Name:     job.Name,
				Message:  fmt.Sprintf("finished %s ago", completedFor),
				Category: Ready,
			}, true
		}
	}

	return nil, false
}

// waitingMessage returns a message like "failed to pull its image", as well as
// a possibly-empty detail string.
func waitingMessage(waiting *corev1.ContainerStateWaiting) (string, string, Category) {
	switch {
	case waiting == nil:
		return "is waiting to start", "", Pending
	case waiting.Reason == "ErrImagePull" || waiting.Reason == "ImagePullBackOff":
		return "failed to pull its image", waiting.Message, Error
	case waiting.Reason == "ContainerCreating":
		// This occurs when the image is being pulled (but not if there are init
		// containers). Some issue reports suggest that pods can get stuck in
		// ContainerCreating, but I can't reproduce that.
		return "is being created", waiting.Message, Pending
	case waiting.Reason == "PodInitializing":
		// This appears to mean that a precondition is not fulfilled. Often, a warning
		// event explains why, such as when a volume can't be mounted.
		return "is initializing", waiting.Message, Pending
	default:
		log.Warningf("unrecognized waiting.Reason %q: %s", waiting.Reason, waiting.Message)
		return "is waiting to start", fmt.Sprintf("%q: %s", waiting.Reason, waiting.Message), Pending
	}
}

// terminationMessage returns a message like "exited with code 1, 1m32s ago",
// as well as a possibly-empty detail string.
func terminationMessage(terminated *corev1.ContainerStateTerminated) (string, string) {
	interval := timeNow().Sub(terminated.FinishedAt.Time).Round(time.Second)
	if terminated.Reason == "ContainerCannotRun" {
		return "cannot start", terminated.Message
	} else if terminated.Reason == "OOMKilled" {
		return fmt.Sprintf("killed due to RAM shortage, %s ago", interval), ""
	} else if terminated.Signal != 0 {
		// In the cases I've seen, Signal is unset and ExitCode > 128, but `kubectl`
		// handles Signal != 0, so it seems worth handling here too.
		return fmt.Sprintf("killed by signal number %d, %s ago", terminated.Signal, interval), ""
	} else if signame := unix.SignalName(syscall.Signal(terminated.ExitCode - 128)); signame != "" {
		return fmt.Sprintf("killed by signal %s, %s ago", signame, interval), ""
	} else {
		return fmt.Sprintf("exited with code %d, %s ago", terminated.ExitCode, interval), ""
	}
}

// terminatedRecently returns true if the pod terminated recently enough that it
// could be in a crashloop.
func terminatedRecently(container corev1.ContainerStatus) bool {
	if container.LastTerminationState.Terminated == nil {
		return false
	}
	if container.State.Running == nil {
		return true
	}
	// Container is running but crashed before. If it's been running a long time,
	// the crash was probably a one-off.
	return timeNow().Sub(container.State.Running.StartedAt.Time) < containerTimeToCrash
}

// isPodOptedOutofErrorChecking returns true if the pod is opted out of error
// checking. This checks the pod's labels to verify that the pod is opted out.
func isPodOptedOutofErrorChecking(pod *corev1.Pod) bool {
	return pod.Labels["intrinsic.ai/opt-out-error-checking"] == "true"
}

// isDeploymentOptedOutofErrorChecking returns true if the deployment is opted out of error
// checking. This checks the pod templates's labels to verify that the pod is opted out.
func isDeploymentOptedOutofErrorChecking(deployment *appsv1.Deployment) bool {
	return deployment.Spec.Template.Labels["intrinsic.ai/opt-out-error-checking"] == "true"
}

// inspectContainerStatus looks for container failure reasons, returning diagnostics if:
// - the container hasn't started yet
// - the container has crashed recently
// or a nil slice if the container is running or has succeeded.
func inspectContainerStatus(client KubeInterface, pod corev1.Pod, container corev1.ContainerStatus) []*Diagnostic {
	if container.State.Running != nil && !terminatedRecently(container) {
		log.V(1).Infof("pod %s container %s running since %s", pod.Name, container.Name, container.State.Running.StartedAt)
		return nil
	}
	if pod.Status.Phase == corev1.PodSucceeded {
		log.V(1).Infof("pod %s with container %s succeeded", pod.Name, container.Name)
		return nil
	}
	if pod.Spec.RestartPolicy != corev1.RestartPolicyAlways && container.State.Terminated != nil && container.State.Terminated.Reason == "Completed" {
		log.V(1).Infof("container %s in pod %s succeeded", container.Name, pod.Name)
		return nil
	}

	d := &Diagnostic{
		Kind:      podKind,
		Name:      pod.Name,
		Container: container.Name,
		Message:   "is not running", // We'll overwrite this with a better message.
		Category:  Error,
	}

	if container.State.Terminated != nil {
		// Container has terminated unexpectedly.
		d.Message, d.Detail = terminationMessage(container.State.Terminated)
		if d.Detail == "" {
			d.Detail = fetchLogDetail(client, pod, container.Name, Current)
		}
		return []*Diagnostic{d}
	} else if container.LastTerminationState.Terminated != nil {
		// A crashlooping container shows up briefly as "running" before crashing.
		// Give a diagnostic for the last crash.
		d.Message, d.Detail = terminationMessage(container.LastTerminationState.Terminated)
		if d.Detail == "" {
			d.Detail = fetchLogDetail(client, pod, container.Name, Previous)
		}
		return []*Diagnostic{d}
	}
	d.Message, d.Detail, d.Category = waitingMessage(container.State.Waiting)
	return []*Diagnostic{d}
}

// inspectInitContainers detects when an init container is blocking a pod from
// starting, returning diagnostics if:
// - an init container can't start or has crashed recently
// - an init container is still running
// or a nil slice if all init containers are complete.
func inspectInitContainers(client KubeInterface, pod corev1.Pod) []*Diagnostic {
	for _, container := range pod.Status.InitContainerStatuses {
		if container.State.Terminated != nil && container.State.Terminated.ExitCode == 0 {
			// This init container has succeeded.
			continue
		}

		if diagnostics := inspectContainerStatus(client, pod, container); len(diagnostics) > 0 {
			// The init container can't start or has crashed.
			return diagnostics
		}
		// The init container is still running.
		return []*Diagnostic{{
			Kind:      podKind,
			Name:      pod.Name,
			Container: container.Name,
			Message:   "is still running",
			Detail:    fetchLogDetail(client, pod, container.Name, Current),
			Category:  Pending,
		}}
	}
	return nil
}

// jobPodDiagnostic checks for a pod that belongs to a batch job.
func jobPodDiagnostic(pod corev1.Pod) (*Diagnostic, bool) {
	//  Pod of the job failed
	if pod.Status.Phase == corev1.PodFailed {
		d := &Diagnostic{
			Kind:     podKind,
			Name:     pod.Name,
			Message:  fmt.Sprintf("of job %s has failed", pod.Labels["job-name"]),
			Category: Error,
		}
		return d, true
	}

	// This job is taking considerable time to complete, give the user a heads-up
	if pod.Status.StartTime != nil {
		runningFor := timeNow().Sub(pod.Status.StartTime.Time).Round(time.Second)
		if pod.Status.Phase == corev1.PodRunning && runningFor > jobPodWarningTimeout {
			d := &Diagnostic{
				Kind:     podKind,
				Name:     pod.Name,
				Message:  fmt.Sprintf("of job %s is still running after %s", pod.Labels["job-name"], runningFor),
				Category: Pending,
			}
			return d, true
		}
	}

	if pod.Status.Phase == corev1.PodRunning {
		d := &Diagnostic{
			Kind:     podKind,
			Name:     pod.Name,
			Message:  fmt.Sprintf("of job %s is still running", pod.Labels["job-name"]),
			Category: Pending,
		}
		return d, true
	}
	return nil, false
}

// recentStartDiagnostic checks for a container that started recently and might
// still crash.
func recentStartDiagnostic(pod corev1.Pod, container corev1.ContainerStatus) (*Diagnostic, bool) {
	if container.State.Running == nil || terminatedRecently(container) {
		return nil, false
	}
	runningFor := timeNow().Sub(container.State.Running.StartedAt.Time).Round(time.Second)
	if runningFor > containerTimeToCrash {
		return nil, false
	}
	// Container hasn't been running long and may still crash.
	return &Diagnostic{
		Kind:      podKind,
		Name:      pod.Name,
		Container: container.Name,
		Message:   fmt.Sprintf("started %s ago", runningFor),
		Category:  Ready,
	}, true
}

func removeEmptyLines(lines []string) []string {
	var result []string
	for _, line := range lines {
		if line != "" {
			result = append(result, line)
		}
	}
	return result
}

func tail(lines []string, n int) []string {
	if len(lines) <= n {
		return lines
	}
	return lines[len(lines)-n:]
}

// removeInfoLogs removes lines starting with "I", if there are more than
// `maxLogLines` in the logs.
func removeInfoLogs(lines []string) []string {
	if len(lines) <= maxLogLines {
		// No need to remove info logs.
		return lines
	}
	var result []string
	for _, line := range lines {
		if line[0] != 'I' {
			result = append(result, line)
		}
	}
	if len(result) == 0 {
		// Turns out it's all info.
		return tail(lines, maxLogLines)
	}
	return tail(result, maxLogLines)
}

// addSuggestionToLogs looks for third-party errors and adds suggestions to remedy them.
func addSuggestionToLogs(lines []string) []string {
	re := regexp.MustCompile(`bind.*:(\d+)}.*Address already in use`)
	for _, line := range lines {
		if submatches := re.FindStringSubmatch(line); len(submatches) != 0 {
			return append(
				lines,
				"Is another process (such as kubectl port-forward) using the port?",
				fmt.Sprintf("Try running: sudo netstat -ntlp | grep %s", submatches[1]),
			)
		}
	}
	return lines
}

// fetchLogDetail tries to get useful information from the logs of the given
// container. mode determines whether to use the current or previous run of the
// container. Errors are logged and not returned to avoid crashes when pods are
// deleted during inspection.
func fetchLogDetail(client KubeInterface, pod corev1.Pod, container string, mode LogMode) string {
	fetchLines := int64(100)
	opts := &corev1.PodLogOptions{
		Container: container,
		TailLines: &fetchLines,
		Previous:  bool(mode),
	}

	req := getLogs(client.CoreV1().Pods(pod.Namespace), pod.Name, opts)
	logs, err := req.Stream(context.TODO())
	if err != nil {
		log.Errorf("Failed to get logs: %v", err)
		return ""
	}
	defer logs.Close()

	buf := new(bytes.Buffer)
	if _, err = io.Copy(buf, logs); err != nil {
		log.Errorf("Failed to copy logs to buffer: %v", err)
		return ""
	}
	lines := removeInfoLogs(removeEmptyLines(strings.Split(buf.String(), "\n")))
	lines = addSuggestionToLogs(lines)
	return strings.Join(lines, "\n")
}

// inspectSpecContainers looks for common mistakes in the YAML spec containers.
func inspectSpecContainers(pod corev1.Pod) []*Diagnostic {
	result := []*Diagnostic{}
	for _, container := range pod.Spec.Containers {
		for _, arg := range container.Args {
			// Raise a warning if an arg value is quoted. This is a common mistake
			// when writing YAML files.
			// This only handles `--arg=value` type syntax for now.
			argAndValue := strings.SplitN(arg, "=", 2)
			if len(argAndValue) != 2 {
				continue
			}
			value := argAndValue[1]

			if strings.HasPrefix(value, "\"") && strings.HasSuffix(value, "\"") {
				result = append(result, &Diagnostic{
					Kind:      podKind,
					Name:      pod.Name,
					Container: container.Name,
					Message:   "has quotes in an arg value",
					Detail:    arg,
					Category:  Warning,
				})
			}
		}
	}
	return result
}

// virtualServicePortDiagnostic checks that a port referred to by a
// VirtualService has a valid protocol.
// https://istio.io/latest/docs/ops/configuration/traffic-management/protocol-selection/
func virtualServicePortDiagnostic(vs unstructured.Unstructured, service corev1.Service, port corev1.ServicePort) (*Diagnostic, bool) {
	// TODO(rodrigoq): check AppProtocol when third_party catches up to k8s 1.18.
	prefixes := []string{"grpc", "http", "tcp"}
	if port.Name == "" {
		return &Diagnostic{
			Kind:     virtualServiceKind,
			Name:     vs.GetName(),
			Message:  fmt.Sprintf("refers to Service %q with an unnamed port (%d)", service.Name, port.Port),
			Detail:   fmt.Sprintf("Ports must be named with one of these prefixes: %v (go/intrinsic-ingress)", prefixes),
			Category: Error,
		}, true
	}

	prefix := strings.Split(port.Name, "-")[0]
	for _, p := range prefixes {
		if prefix == p {
			return nil, false
		}
	}
	return &Diagnostic{
		Kind:     virtualServiceKind,
		Name:     vs.GetName(),
		Message:  fmt.Sprintf("refers to Service %q with an invalid port name %q", service.Name, port.Name),
		Detail:   fmt.Sprintf("Ports must be named with one of these prefixes: %v (go/intrinsic-ingress)", prefixes),
		Category: Error,
	}, true
}

// inspectVirtualServices looks for invalid references from VirtualServices to
// Services, either because the service is missing or because it has an invalid
// port name.
func inspectVirtualServices(virtualServices []unstructured.Unstructured, services []corev1.Service) []*Diagnostic {
	if len(virtualServices) == 0 {
		return []*Diagnostic{}
	}
	namespace := virtualServices[0].GetNamespace()
	// So we can later look up and tell the user about available services, create a
	// map & comma-separated list.
	serviceMap := make(map[string]corev1.Service)
	var serviceNames []string
	var serviceNamesSameNS []string // Service names in the same namespace.
	for _, s := range services {
		if s.Namespace == namespace {
			serviceMap[s.Name] = s
			serviceNamesSameNS = append(serviceNamesSameNS, s.Name)
		}
		fullName := fmt.Sprintf("%s.%s.svc.cluster.local", s.Name, s.Namespace)
		serviceMap[fullName] = s
		serviceNames = append(serviceNames, fullName)
	}
	serviceNames = append(serviceNamesSameNS, serviceNames...)

	result := []*Diagnostic{}
	for _, vs := range virtualServices {
		// Parsing the VirtualService is tricky because it's a deeply nested
		// "unstructured" object, because Istio's client-go hasn't been imported to
		// third_party. Make life easier by ignoring VirtualServices with an
		// unexpected structure.
		http, found, err := unstructured.NestedSlice(vs.Object, "spec", "http")
		if !found || err != nil || len(http) != 1 {
			log.V(1).Infof("ignoring VirtualService %q: spec.http not found or not a slice of length 1", vs.GetName())
			continue
		}
		http0, ok := http[0].(map[string]any)
		if !ok {
			log.V(1).Infof("ignoring VirtualService %q: spec.http[0] not an object", vs.GetName())
			continue
		}
		routes, found, err := unstructured.NestedSlice(http0, "route")
		if !found || err != nil || len(routes) != 1 {
			log.V(1).Infof("ignoring VirtualService %q: spec.http[0].route not found or not a slice of length 1", vs.GetName())
			continue
		}
		route0, ok := routes[0].(map[string]any)
		if !ok {
			log.V(1).Infof("ignoring VirtualService %q: spec.http[0].route[0] not an object", vs.GetName())
			continue
		}
		host, found, err := unstructured.NestedString(route0, "destination", "host")
		if !found || err != nil || len(routes) != 1 {
			log.V(1).Infof("ignoring VirtualService %q: spec.http[0].route[0].destination.host not found or not a string", vs.GetName())
			continue
		}
		// This is a VirtualService with the expected form (a single host/service).
		service, ok := serviceMap[host]
		if !ok {
			detailMessage := "available services: "
			var sn []string
			if strings.Contains(host, ".") {
				// host is not a local service, so we report back all available services
				sn = serviceNames
			} else {
				sn = serviceNamesSameNS
			}
			if len(sn) > 0 {
				detailMessage += strings.Join(sn, ", ")
			} else {
				detailMessage += "<none>"
			}
			result = append(result, &Diagnostic{
				Kind:     virtualServiceKind,
				Name:     vs.GetName(),
				Message:  fmt.Sprintf("routes to Service %q, which was not found", host),
				Detail:   detailMessage,
				Category: Error,
			})
			continue
		}
		for _, port := range service.Spec.Ports {
			if diagnostic, ok := virtualServicePortDiagnostic(vs, service, port); ok {
				result = append(result, diagnostic)
			}
		}
	}
	return result
}

// InspectNamespace looks at the objects in the given namespace, and returns a
// list of possible problems.
func InspectNamespace(client KubeInterface, namespace string) ([]*Diagnostic, error) {
	log.V(1).Infof("Inspecting namespace %q", namespace)
	deployments, err := client.AppsV1().Deployments(namespace).List(context.TODO(), metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list deployments")
	}
	pods, err := client.CoreV1().Pods(namespace).List(context.TODO(), metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list pods")
	}
	jobs, err := client.BatchV1().Jobs(namespace).List(context.TODO(), metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list jobs")
	}
	eventClient := client.CoreV1().Events(namespace)
	fs := eventClient.GetFieldSelector(nil, &namespace, nil, nil).String()
	events, err := eventClient.List(context.TODO(), metav1.ListOptions{FieldSelector: fs})
	if err != nil {
		return nil, errors.Wrap(err, "list events")
	}
	ns, err := client.CoreV1().Namespaces().List(context.TODO(), metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list namespaces")
	}
	if len(ns.Items) == 0 {
		return nil, errors.New("No namespaces found")
	}
	// Gather services from all available namespaces.
	var services []corev1.Service
	for _, n := range ns.Items {
		s, err := client.CoreV1().Services(n.Name).List(context.TODO(), metav1.ListOptions{})
		if err != nil {
			return nil, errors.Wrap(err, "list services")
		}
		services = append(services, s.Items...)
	}
	virtualServices, err := client.Resource(virtualServiceGVR).Namespace(namespace).List(context.TODO(), metav1.ListOptions{})
	if apierrors.IsNotFound(err) {
		// VirtualService CRD not found. Either we're in the cloud cluster where it's not expected, or
		// the chart-assignment-controller will already have failed to apply the chart. We can ignore
		// the error here.
		virtualServices = &unstructured.UnstructuredList{}
	} else if err != nil {
		return nil, errors.Wrap(err, "list VirtualServices")
	}

	result := []*Diagnostic{}
	if diagnostics := inspectVirtualServices(virtualServices.Items, services); len(diagnostics) > 0 {
		result = append(result, diagnostics...)
	}

	for _, pod := range pods.Items {
		if isPodOptedOutofErrorChecking(&pod) {
			log.V(1).Infof("Pod %s opted out of error checking", pod.Name)
			continue
		}
		if diagnostic, ok := deletionDiagnostic(pod); ok {
			result = append(result, diagnostic)
			continue
		}
		if diagnostic, ok := schedulingDiagnostic(pod); ok {
			result = append(result, diagnostic)
			continue
		}
		if diagnostics := inspectInitContainers(client, pod); len(diagnostics) > 0 {
			result = append(result, diagnostics...)
			continue
		}

		// Look for events such as "missing volume" or "image pull" errors.
		// Since it's hard to know when these problems have been resolved (the
		// events stick around), we emit them as a separate diagnostic.
		// TODO(rodrigoq): suppress `is waiting to start` when FailedMount occurred recently.
		if diagnostic, ok := eventDiagnostic(events, pod); ok {
			result = append(result, diagnostic)
		}

		if diagnostics := inspectSpecContainers(pod); len(diagnostics) > 0 {
			result = append(result, diagnostics...)
		}

		// Add diagnostics for all problematic containers.
		for _, container := range pod.Status.ContainerStatuses {
			diagnostics := inspectContainerStatus(client, pod, container)
			result = append(result, diagnostics...)
			if diagnostic, ok := recentStartDiagnostic(pod, container); ok {
				result = append(result, diagnostic)
			}
		}

		// Diagnostics for pods that are running for a job
		if _, isJobPod := pod.Labels["job-name"]; isJobPod {
			if diagnostic, ok := jobPodDiagnostic(pod); ok {
				result = append(result, diagnostic)
			}
		}

		// If we got this far, hopefully nothing is wrong.
	}
	for _, job := range jobs.Items {
		if diagnostic, ok := inspectJobStatus(job); ok {
			result = append(result, diagnostic)
			continue
		}
	}
	log.V(1).Infof("Found %d diagnostics on %d VirtualServices, %d pods, and %d jobs", len(result), len(virtualServices.Items), len(pods.Items), len(jobs.Items))

	// Deployments don't yield helpful errors, they just tell us that the pods
	// haven't been updated yet. Only look at them if we have nothing more useful
	// to share.
	if len(result) == 0 {
		for _, deployment := range deployments.Items {
			if isDeploymentOptedOutofErrorChecking(&deployment) {
				log.V(1).Infof("Deployment %s opted out of error checking", deployment.Name)
				continue
			}
			if diagnostic, ok := updateDiagnostic(deployment); ok {
				result = append(result, diagnostic)
			}
		}
		log.V(1).Infof("Found %d diagnostics in %d deployments", len(result), len(deployments.Items))
	}

	return result, nil
}

// BuildErrorString builds and returns a string to be printed to STDOUT given a list of diagnostics.
func BuildErrorString(diagnostics []*Diagnostic) string {
	s := fmt.Sprintf("\nApp status at %s:\n", timeNow().Format(timeFormat))
	for _, d := range diagnostics {
		if d.Detail == "" {
			s += fmt.Sprintf("  %s\n", d.ColorString())
			continue
		}
		s += fmt.Sprintf("  %s\n\n", d.DetailString())
	}
	return s
}

// PrintAppStatus prints the current app status and returns how many lines of
// output was printed.
func PrintAppStatus(diagnostics []*Diagnostic) int {
	s := BuildErrorString(diagnostics)
	fmt.Print(s)
	return strings.Count(s, "\n")
}

// Options defines the options StatApp can be configured to run with.
type Options struct {
	// Namespace, e.g., "app-glue-demo". Must be set.
	Namespace string
	// If true, the app status will only be polled once and appstat will terminate
	// immediately afterwards.
	Oneshot bool
}

// StatApp is a utility that inspects a Kubernetes namespace and looks for
// common errors, printing its findings to stdout. It repeats the inspection
// until nothing is found.
func StatApp(ctx context.Context, client KubeInterface, options *Options) error {
	if options == nil {
		return errors.New("options must be non-nil")
	}
	if len(options.Namespace) == 0 {
		return fmt.Errorf("Namespace must be set: %+v", options)
	}
	if client == nil {
		return fmt.Errorf("client must be set: %v", options)
	}

	ctx, span := trace.StartSpan(ctx, "inspect.StatApp")
	span.AddAttributes(trace.StringAttribute("namespace", options.Namespace))
	defer span.End()

	if options.Oneshot {
		diagnostics, err := InspectNamespace(client, options.Namespace)
		if err != nil {
			log.ErrorContextf(ctx, "Failed to inspect namespace %s: %v", options.Namespace, err)
		}
		PrintAppStatus(diagnostics)
		return nil
	}

	// TODO(rodrigoq): test the looping logic.
	tick := time.Tick(pollInterval)
	var numPrintedLines int
	spansTracker := pollingspans.New()
	for {
		diagnostics, err := InspectNamespace(client, options.Namespace)
		if err != nil {
			log.ErrorContextf(ctx, "Failed to inspect namespace %s: %v", options.Namespace, err)
		}

		spansTracker.BeforeRegistration()
		for _, d := range diagnostics {
			m := d.nameForSpan()
			spansTracker.RegisterEvent(m, func() pollingspans.SpanLike {
				_, span := trace.StartSpan(ctx, m)
				span.AddAttributes(trace.StringAttribute("appstat_full_name", d.Name))
				return span
			})
		}
		spansTracker.AfterRegistration()

		// Clear output from previous iteration.
		if numPrintedLines > 0 {
			fmt.Printf("\x1b[%dF", numPrintedLines) // Move cursor up numPrintedLines.
			fmt.Print("\x1b[J")                     // Clear from cursor until end of screen.
		}

		if len(diagnostics) == 0 {
			break
		}

		numPrintedLines = PrintAppStatus(diagnostics)
		<-tick
	}
	spansTracker.End()

	PrintAppStatus(
		[]*Diagnostic{{
			Kind:     "ChartAssignment",
			Name:     options.Namespace,
			Message:  "All pods are running; maybe they're working, maybe not! 🙃\n",
			Category: Ready,
		}})
	return nil
}
