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

// Package transfersvc provides an implementation of the Transfer gRPC service
// that clients can use to configure a cluster.
package transfersvc

import (
	"context"
	"fmt"
	"net"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"

	"intrinsic/assets/deploy/render"
	"intrinsic/config/environments"
	chartassignment "intrinsic/kubernetes/workcell_spec/chartassignment"
	"intrinsic/stats/go/telemetry"

	"dario.cat/mergo"
	log "github.com/golang/glog"
	"github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/googlecloudrobotics/core/src/go/pkg/client/versioned"
	"github.com/pkg/errors"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	appsv1 "k8s.io/api/apps/v1"
	batchv1 "k8s.io/api/batch/v1"
	corev1 "k8s.io/api/core/v1"
	k8serrors "k8s.io/apimachinery/pkg/api/errors"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/kubernetes"

	sipb "intrinsic/kubernetes/workcell_spec/proto/solution_internal_go_proto"
	apipb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"

	epb "google.golang.org/protobuf/types/known/emptypb"
)

// ClusterInfo contains constant cluster metadata.
type ClusterInfo struct {
	Name                   string
	CanDoPhysicalExecution bool
	CanDoSim               bool
	HasGpu                 bool
	// By fixing GCPProject and Registry here, we rely on the assumption that
	// intrinsic-app-chart is released to the same repository as
	// intrinsic-base.
	// GCPProject is the google cloud project associated with the cluster.
	GCPProject string
	// Registry is the OCI repository to fetch intrinsic-app-chart images from.
	Registry string
}

func makeLabel(s string) string {
	toK8sLabel := regexp.MustCompile(`[^a-zA-Z0-9._-]`)
	return toK8sLabel.ReplaceAllString(s, "_")
}

// TransferService implements the generated gRPC TransferServer interface.
type TransferService struct {
	// crcClient is used to get/put ChartAssignments to the cluster.
	crcClient versioned.Interface

	// coreClient is used to interact with kubernetes apis
	coreClient kubernetes.Interface

	// clusterInfo is used for helm templating
	clusterInfo ClusterInfo
}

// NewServiceWithClients creates a TransferService with given Kubernetes & GCS clients.
// It will start a goroutine to use for later running garbage collection.
// This is suitable for use in tests.
func NewServiceWithClients(crcClient versioned.Interface, coreClient kubernetes.Interface, clusterInfo ClusterInfo) *TransferService {
	ts := &TransferService{
		crcClient:   crcClient,
		coreClient:  coreClient,
		clusterInfo: clusterInfo,
	}
	return ts
}

// updateCommonValues sets (some of) the common values based on the request. It
// avoids overwriting values set in the workcell spec or the ApplyWorkcellSpec
// request.
func (s *TransferService) updateCommonValues(ca *v1alpha1.ChartAssignment) error {
	if err := mergo.Merge(&ca.Spec.Chart.Values, v1alpha1.ConfigValues{
		"robot":    map[string]any{"name": ca.Spec.ClusterName},
		"project":  s.clusterInfo.GCPProject,
		"registry": s.clusterInfo.Registry,
		"domain":   environments.Domain(s.clusterInfo.GCPProject),
	}); err != nil {
		return errors.Wrap(err, "merge common")
	}
	return nil
}

func updateClusterValues(ca *v1alpha1.ChartAssignment, clusterInfo ClusterInfo) error {
	clusterValues := v1alpha1.ConfigValues{
		"can_do_physical_execution": clusterInfo.CanDoPhysicalExecution,
		"has_gpu":                   clusterInfo.HasGpu,
	}
	if err := mergo.Merge(&ca.Spec.Chart.Values, clusterValues, mergo.WithOverride); err != nil {
		return errors.Wrap(err, "merge cluster values")
	}
	return nil
}

// updateChartAssignment writes the ChartAssignment to the cluster, using a
// get/create/update operation along the lines of `kubectl apply`. Actually,
// `kubectl apply` generates and applies a patch, whereas this overwrites the
// metadata/spec: if that turns out to be insufficient, we could copy the JSON
// Merge Patch logic from synk.go:
//
// http://third_party/golang/cloudrobotics/go/pkg/synk/synk.go?l=636&rcl=322750093
func (s *TransferService) updateChartAssignment(ctx context.Context, ca *v1alpha1.ChartAssignment, appName string) (*v1alpha1.ChartAssignment, error) {
	ctx, span := trace.StartSpan(ctx, "transfersvc.updateChartAssignment")
	span.AddAttributes(trace.StringAttribute("ChartName", ca.GetName()))
	defer span.End()

	// Apply invocation-specific updates to the ChartAssignment.
	// TODO(rodrigoq): can we eliminate this to make Apply() hermetic?
	ca.Spec.ClusterName = s.clusterInfo.Name
	if ca.Labels == nil {
		ca.Labels = make(map[string]string)
	}
	ca.Labels["installed-by"] = "transferservice"
	if ca.Name != appName {
		if _, exists := ca.Labels[chartassignment.PartOfAppLabel]; !exists {
			ca.Labels[chartassignment.PartOfAppLabel] = appName
		}
	}
	if err := s.updateCommonValues(ca); err != nil {
		return nil, err
	}
	if err := updateClusterValues(ca, s.clusterInfo); err != nil {
		return nil, err
	}
	log.InfoContextf(ctx, "Starting %s", ca.GetName())

	return chartassignment.CreateOrUpdate(ctx, s.crcClient, ca)
}

func isExclusive(ca *v1alpha1.ChartAssignment) bool {
	// This label is the string version of a go boolean generated here:
	// http://intrinsic/kubernetes/workcell_spec/chartassignmentgen.go?l=51
	return ca.Labels["exclusive"] != fmt.Sprintf("%v", false)
}

func caOwnerRef(ca *v1alpha1.ChartAssignment) metav1.OwnerReference {
	return metav1.OwnerReference{
		APIVersion: ca.APIVersion,
		Kind:       ca.Kind,
		Name:       ca.Name,
		UID:        ca.UID,
	}
}

func (s *TransferService) ApplyWorkcellSpec(ctx context.Context, workcellSpec *apipb.WorkcellSpec) error {
	// AlwaysSample as deployments are infrequent and often slow. Add "Top" as we previously had the
	// name inside a loop below
	ctx, span := trace.StartSpan(telemetry.WithAlwaysSample(ctx), "transfersvc.ApplyWorkcellSpec")
	span.AddAttributes(trace.StringAttribute("ClusterName", s.clusterInfo.Name))
	defer span.End()

	chartAssignments, err := chartassignment.FromWorkcellSpec(workcellSpec)
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "failed to parse WorkcellSpec: %v", err)
	}
	if len(chartAssignments) == 0 {
		return status.Error(codes.InvalidArgument, "WorkcellSpec must have a ChartAssignment")
	}
	var exclusiveCA *v1alpha1.ChartAssignment
	for i, ca := range chartAssignments {
		if isExclusive(ca) {
			if exclusiveCA != nil {
				return status.Error(codes.InvalidArgument, "workcell cannot have more than one exclusive ChartAssignment")
			}
			if ca.GetName() != "intrinsic-app-chart" {
				return status.Error(codes.InvalidArgument, "exclusive functionality is deprecated. Do not use.")
			}
			// The transfer service has historically used the convention that
			// the first chart assignment defines the app name.  This is taken
			// from ca.Labels["app"], rather than ca.Name, though these have
			// also historically been the same in chartassignmentgen.  Also,
			// historically, the exclusive chart has been the first chart.  We
			// enforce that here so we can apply them in order, and use the
			// references from the exclusive chart to link the others using
			// owner references.  Enforcing that exclusive == first == app name
			// allows us to pull the app name from the currentCA if no
			// exclusive charts are provided.  ADS does this to update skills
			// and resources. ADS could potentially patch values instead.  This
			// would require skills to be in a single chart; services already
			// are.
			if i != 0 {
				return status.Error(codes.InvalidArgument, "exclusive chart must be first")
			}
			exclusiveCA = ca
		}
	}
	// By convention the first chart defines the app name.
	appName, err := chartassignment.AppName(workcellSpec)
	if err != nil {
		return err
	}
	span.AddAttributes(trace.StringAttribute("AppName", appName))

	currentCA, err := s.CurrentExclusiveChart(ctx)
	if err != nil {
		return err
	}
	// All charts deployed by the transfer service have an owner reference to the
	// exclusive chart.  Here we're determining if we already know which chart we
	// plan to reference (i.e. we're not deploying a new exclusive chart, so we
	// can target currentCA), or if we'll be deploying the chart we plan to
	// reference.  In the latter case, we'll get the information for an owner
	// reference later as we step through the charts to deploy. There will be
	// zero or one references, but an array is the cleanest representation in
	// this case.
	var refsToExclusive []metav1.OwnerReference
	if currentCA != nil {
		if exclusiveCA == nil {
			refsToExclusive = append(refsToExclusive, caOwnerRef(currentCA))
			appName = currentCA.GetName()
		} else if currentCA.GetName() != exclusiveCA.GetName() {
			// We also ensure that there is a single exclusive chart by deleting the
			// existing one.  We only need to do that, though, if the chart name will
			// be different.  Otherwise, the chart assignment controller will
			// update/replace as necessary.
			if err := chartassignment.DeleteAndWait(ctx, s.crcClient, s.coreClient, currentCA.GetName()); err != nil {
				return errors.Wrap(err, "unable to delete existing exclusive chart")
			}
		}
	}

	for _, ca := range chartAssignments {
		log.InfoContextf(ctx, "Applying chart %s", ca.GetName())
		ca.OwnerReferences = append(ca.OwnerReferences, refsToExclusive...)
		if update, err := s.updateChartAssignment(ctx, ca, appName); err != nil {
			return errors.Wrap(err, "update chart assignment")
		} else if isExclusive(ca) {
			// There is probably a way to use update in caOwnerRef, but it's
			// currently returned with empty TypeMeta.  Didn't have a chance to
			// dig into that further.
			update.TypeMeta = ca.TypeMeta
			refsToExclusive = append(refsToExclusive, caOwnerRef(update))
		}
	}

	// NOTE(b/469290429): Clean up legacy per-Skill charts from before consolidation to a single
	// "skills" chart.
	if err := s.pruneLegacySkillCharts(ctx, appName); err != nil {
		return errors.Wrap(err, "prune legacy Skill charts")
	}

	log.InfoContextf(ctx, "Successfully applied workcell spec")
	return nil
}

// pruneLegacySkillCharts deletes per-skill ChartAssignments left behind from before
// skill chart consolidation (b/469290429). It only prunes legacy charts if the consolidated
// Skill chart exists on the cluster.
func (s *TransferService) pruneLegacySkillCharts(ctx context.Context, appName string) error {
	selector := "ai.intrinsic/addon-type=skill"
	if appName != "" {
		selector = fmt.Sprintf("%s,%s=%s", selector, chartassignment.PartOfAppLabel, appName)
	}
	caList, err := s.crcClient.AppsV1alpha1().ChartAssignments().List(ctx, metav1.ListOptions{
		LabelSelector: selector,
	})
	if err != nil {
		return errors.Wrap(err, "list Skill chart assignments")
	}

	var hasSkillsChart bool
	for _, ca := range caList.Items {
		if ca.GetName() == render.SkillChartName {
			hasSkillsChart = true
			break
		}
	}
	if !hasSkillsChart {
		return nil
	}

	for _, ca := range caList.Items {
		if ca.GetName() == render.SkillChartName {
			continue
		}
		log.InfoContextf(ctx, "Deleting legacy Skill chart assignment %s", ca.GetName())
		if err := s.crcClient.AppsV1alpha1().ChartAssignments().Delete(ctx, ca.GetName(), metav1.DeleteOptions{}); err != nil && !k8serrors.IsNotFound(err) {
			return errors.Wrapf(err, "delete legacy Skill chart assignment %s", ca.GetName())
		}
	}
	return nil
}

// CurrentExclusiveChart returns the chart assignment labeled as exclusive.
// This should only ever be intrinsic-app-chart.  If for whatever reason there
// is more than one chart, the first is returned.
func (s *TransferService) CurrentExclusiveChart(ctx context.Context) (*v1alpha1.ChartAssignment, error) {
	allCAs, err := s.crcClient.AppsV1alpha1().ChartAssignments().List(ctx, metav1.ListOptions{LabelSelector: "exclusive=true"})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "list exclusive apps: %v", err)
	}
	if len(allCAs.Items) == 0 {
		return nil, nil
	}
	ca := allCAs.Items[0]
	return &ca, nil
}

func IsSolutionRunning(ca *v1alpha1.ChartAssignment) (bool, error) {
	if ca == nil || ca.Spec.Chart.Values == nil {
		return false, nil
	}
	val, solExists := ca.Spec.Chart.Values["solution_deployment_id"]
	solID, ok := val.(string)
	if solExists && !ok {
		return false, fmt.Errorf("solution_deployment_id is not a string")
	}
	val, appExists := ca.Spec.Chart.Values["app_deployment_id"]
	appID, ok := val.(string)
	if appExists && !ok {
		return false, fmt.Errorf("app_deployment_id is not a string")
	}
	return (solExists && solID != "") || (!solExists && appExists && appID != ""), nil
}

func (s *TransferService) DeleteWorkcellSpec(ctx context.Context, wait bool) error {
	ca, err := s.CurrentExclusiveChart(ctx)
	if err != nil {
		return err
	}
	if ca == nil {
		return nil
	}
	// Asynchronously delete the chart. Since ChartAssignments has finalizers, this merely sets
	// the deletion timestamp, but blocking on deletion can cause frontend timeouts (http://b/207671017).
	// Also note that GetWorkcellStatus returns STOPPING if the chart is not yet, but about to be, deleted.
	err = chartassignment.Delete(ctx, s.crcClient, s.coreClient, ca.Name, wait)
	if err != nil {
		return status.Errorf(codes.Internal, "failed to delete %q: %v", ca.Name, err)
	}
	return nil
}

func (s *TransferService) GetWorkcellStatus(ctx context.Context, req *epb.Empty) (*sipb.GetWorkcellStatusResponse, error) {
	ca, err := s.CurrentExclusiveChart(ctx)
	if err != nil {
		return nil, err
	}
	if ca == nil {
		return nil, status.Errorf(codes.NotFound, "no workcellspec installed")
	}

	response := &sipb.GetWorkcellStatusResponse{
		ClusterName: s.clusterInfo.Name,
	}

	if running, err := IsSolutionRunning(ca); err != nil {
		return nil, fmt.Errorf("failed to check running state of exclusive chart: %w", err)
	} else if !running {
		response.Status = sipb.GetWorkcellStatusResponse_UNKNOWN
		response.ErrorReason = "CA Controller: Only initialization chart but no solution is applied"
		return response, nil
	}

	if ca.ObjectMeta.DeletionTimestamp != nil {
		response.Status = sipb.GetWorkcellStatusResponse_STOPPING
		response.ErrorReason = "CA Controller: Chart is about to be deleted"
		return response, nil
	}

	// List app service for the status check and so that the notebook can enable/disable the relevant clients.
	platformServiceList, err := s.coreClient.CoreV1().Services("app-intrinsic-base").List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "listing platform services")
	}
	ns := ca.Spec.NamespaceName
	appServiceList, err := s.coreClient.CoreV1().Services(ns).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "listing app services")
	}
	const serviceSuffix = ".svc.cluster.local"
	addresses := make(map[string]string)
	for _, svc := range append(append([]corev1.Service{}, platformServiceList.Items...), appServiceList.Items...) {
		if isServiceOptedOut(&svc) /* Skip third-party manifest or opted out services as we can't make sure that all advertised ports eventually open. */ {
			continue
		}
		for _, port := range svc.Spec.Ports {
			address := fmt.Sprintf("%s.%s%s:%d", svc.Name, svc.Namespace, serviceSuffix, port.Port)
			switch port.Protocol {
			case "", corev1.ProtocolTCP:
				addresses[address] = "tcp"
			case corev1.ProtocolUDP:
				addresses[address] = "udp"
			default:
				log.WarningContextf(ctx, "Unknown protocol for %q: %q", address, port.Protocol)
				continue
			}
		}
	}

	version, ok := ca.Annotations["version"]
	if !ok {
		version = "unknown"
	}
	response.Version = version

	// A number of checks are done to assess workcell status:
	//   - ChartAssignment.ObservedGeneration = ChartAssignment.Generation
	//   - ChartAssignment.Status = Settled or Ready
	//   - all deployments have ObservedGeneration = Generation
	//   - all pods have condition Ready, are terminating, or have terminated
	//   - all Services' ports are open
	//   - all jobs have completed
	// Start by setting Status to pending, and exit early if any of the checks fail.
	// TODO(intrinsic-cloud): Pull in diagnostics for determining ERROR state.
	response.Status = sipb.GetWorkcellStatusResponse_PENDING

	if ca.Status.ObservedGeneration < ca.ObjectMeta.Generation {
		response.ErrorReason = "CA Controller: PollingForUpdates\nMake sure your compute devices are turned on and connected"
		return response, nil
	}
	if ca.Status.Phase == v1alpha1.ChartAssignmentPhaseFailed {
		reason := "CA Controller: Failed"
		for _, condition := range ca.Status.Conditions {
			// This normally means that Helm templating failed due to a bug in the templates or bad
			// chart values. Normally, these errors are exposed by app start, but if app release created a
			// ChartAssignment with bad values, it might first pop up here.
			if condition.Type == v1alpha1.ChartAssignmentConditionSettled && condition.Status == corev1.ConditionFalse {
				reason = reason + fmt.Sprintf(" (%s)", condition.Message)
			}
		}
		response.ErrorReason = reason
		return response, nil
	}
	if ca.Status.Phase != v1alpha1.ChartAssignmentPhaseSettled && ca.Status.Phase != v1alpha1.ChartAssignmentPhaseReady {
		response.ErrorReason = fmt.Sprintf("CA Controller: %v", ca.Status.Phase)
		return response, nil
	}

	deployments, err := s.coreClient.AppsV1().Deployments(ns).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list deployments")
	}
	for _, deployment := range deployments.Items {
		if isDeploymentOptedOut(&deployment) {
			continue
		}
		if deployment.Status.ObservedGeneration < deployment.ObjectMeta.Generation {
			response.ErrorReason = "Deployment Controller: PollingForUpdates"
			return response, nil
		}
	}

	// Check that all pods have Condition Ready.
	pods, err := s.coreClient.CoreV1().Pods(ns).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list pods")
	}
	var notReadyPods []string
	for _, pod := range pods.Items {
		if isPodOptedOut(&pod) {
			continue
		}
		if pod.DeletionTimestamp != nil || pod.Status.Phase == corev1.PodFailed || pod.Status.Phase == corev1.PodSucceeded {
			// Ignore terminating and terminated pods.
			// Note that if strategy=Recreate this can cause a false negative,
			// which the subsequent check for open ports should then correct.
			continue
		}
		podReady := false
		for _, condition := range pod.Status.Conditions {
			if condition.Type == corev1.PodReady && condition.Status == corev1.ConditionTrue {
				podReady = true
			}
		}
		if !podReady {
			notReadyPods = append(notReadyPods, pod.Name)
		}
	}
	sort.Strings(notReadyPods)
	if len(notReadyPods) > 0 {
		response.ErrorReason = fmt.Sprintf("Pods not ready: %v", strings.Join(notReadyPods, ","))
		return response, nil
	}

	// Check that all jobs have completed
	jobs, err := s.coreClient.BatchV1().Jobs(ns).List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list jobs")
	}
	var notReadyJobs []string
	failedJobs := map[string]string{}
	for _, job := range jobs.Items {
		jobReady := false
		for _, condition := range job.Status.Conditions {
			if condition.Type == batchv1.JobComplete && condition.Status == corev1.ConditionTrue {
				jobReady = true
			}
			if condition.Type == batchv1.JobFailed && condition.Status == corev1.ConditionTrue {
				jobReady = true
				failedJobs[job.Name] = condition.Message
			}
		}
		if !jobReady {
			notReadyJobs = append(notReadyJobs, job.Name)
		}
	}
	sort.Strings(notReadyJobs)
	if len(notReadyJobs) > 0 {
		response.ErrorReason = fmt.Sprintf("Jobs not ready: %v", strings.Join(notReadyJobs, ","))
		return response, nil
	}
	if len(failedJobs) > 0 {
		msgs := make([]string, 0, len(failedJobs))
		for name := range failedJobs {
			msgs = append(msgs, fmt.Sprintf("%s (%s)", name, failedJobs[name]))
		}
		sort.Strings(msgs)
		response.ErrorReason = fmt.Sprintf("Jobs failed: %v", strings.Join(msgs, ","))
		response.Status = sipb.GetWorkcellStatusResponse_ERROR
		return response, nil
	}

	// Check that all ports are open.
	var waitGroup sync.WaitGroup
	waitGroup.Add(len(addresses))
	notReadyAddrs := []string{}
	mutex := &sync.RWMutex{}
	for addr, protocol := range addresses {
		go func(addr, protocol string) {
			c, err := net.DialTimeout(protocol, addr, 1*time.Second)
			if err == nil {
				c.Close()
			} else {
				mutex.Lock()
				// Hide .svc.cluster.local boilerplate from human-readable message.
				notReadyAddrs = append(notReadyAddrs, strings.ReplaceAll(addr, serviceSuffix, ""))
				mutex.Unlock()
			}
			waitGroup.Done()
		}(addr, protocol)
	}
	waitGroup.Wait()
	sort.Strings(notReadyAddrs)
	if len(notReadyAddrs) > 0 {
		response.ErrorReason = fmt.Sprintf("Ports not open: %v", strings.Join(notReadyAddrs, ","))
		return response, nil
	}

	response.Status = sipb.GetWorkcellStatusResponse_HEALTHY
	return response, nil
}

// Check whether a resource is opted out from these checks via labels.
func isLabelsMapOptedOut(resource *metav1.ObjectMeta) bool {
	return resource.Labels["intrinsic.ai/opt-out-error-checking"] == "true" ||
		resource.Labels["cloudrobotics.com/opt-out-error-checking"] == "true"
}

func isServiceOptedOut(svc *corev1.Service) bool {
	return isLabelsMapOptedOut(&svc.ObjectMeta)
}

func isPodOptedOut(pod *corev1.Pod) bool {
	return isLabelsMapOptedOut(&pod.ObjectMeta)
}

func isDeploymentOptedOut(deployment *appsv1.Deployment) bool {
	return isLabelsMapOptedOut(&deployment.ObjectMeta)
}
