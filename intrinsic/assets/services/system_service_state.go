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

// Package systemservicestate provides a service to query and modify the states of running service
// asset instances in a solution.
package systemservicestate

import (
	"context"
	"fmt"
	"sync"
	"time"

	"intrinsic/assets/deploy/render"
	"intrinsic/assets/syncutils"
	"intrinsic/util/status/extstatus"

	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/status"
	corev1 "k8s.io/api/core/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/informers"
	"k8s.io/client-go/kubernetes"
	corelisters "k8s.io/client-go/listers/core/v1"

	servicestatepb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	systemservicestatepb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
	"intrinsic/skills/internal/configmapwatcher"
)

const (
	instanceNameServiceLabel    = "solution.intrinsic.ai/instance"
	serviceMetadataVersionLabel = "solution.intrinsic.ai/service-metadata-version"

	supportsServiceStateAnnotation = "solution.intrinsic.ai/supports-service-state"
	servicePortAnnotation          = "solution.intrinsic.ai/service-port"

	statusComponent = "System service state"

	// The title for the error when the service image cannot be pulled.
	imagePullErrorTitle = "Failed to pull service image."

	// The title for the error when the service image is in crash loop backoff.
	crashLoopBackOffErrorTitle = "Service has crashed."

	selfStateErrorTitle = "Failed to get self-reported state from service."

	// The title for the error when ServiceState client cannot be registered.
	registrationErrorTitle = "Failed to register service state grpc client."

	errImagePull     = "ErrImagePull"
	imagePullBackOff = "ImagePullBackOff"
	crashLoopBackOff = "CrashLoopBackOff"
)

var (
	// The k8s namespace being monitored by the system service state service.
	serviceNamespace = render.ResourceNamespace

	// An arbitrary timeout for getting the self-reported state from a service.
	// If the call to get the self-reported state times out, the state of the service will be surfaced
	// as STATE_CODE_ERROR.
	defaultReportedStateTimeout = 3 * time.Second

	// serverAddress is a function that returns the address of the k8s service. This can be overridden
	// in tests.
	serverAddress = serverAddressDefault
)

func serverAddressDefault(serviceName, port string) string {
	return fmt.Sprintf("%s.%s.svc.cluster.local:%s", serviceName, serviceNamespace, port)
}

// A wrapper around a service state client.
type serviceHandler struct {
	clientConn           *grpc.ClientConn
	ssClient             servicestatepb.ServiceStateClient
	supportsServiceState bool

	registrationErrorMessage string
}

type server struct {
	podLister corelisters.PodLister
	k8sClient kubernetes.Interface

	// A map of service instance name to serviceHandler.
	serviceHandlers syncutils.SyncMap[string, *serviceHandler]

	reportedStateTimeout time.Duration
}

// Options are the values required for creating a new system service state server.
type Options struct {
	K8sClient kubernetes.Interface
	Factory   informers.SharedInformerFactory

	// Only used for testing.
	onConfigMapUpdate    func(cm *corev1.ConfigMap)
	onConfigMapDelete    func(cm *corev1.ConfigMap)
	reportedStateTimeout time.Duration
}

// New creates a new system service state server.
//
// The server will also start running the kubernetes informers to watch over specific k8s resources.
func New(ctx context.Context, opts *Options) (systemservicestatepb.SystemServiceStateServer, error) {
	if opts.Factory == nil {
		return nil, fmt.Errorf("opts.Factory is required")
	}

	s := &server{
		podLister:            opts.Factory.Core().V1().Pods().Lister(),
		k8sClient:            opts.K8sClient,
		reportedStateTimeout: defaultReportedStateTimeout,
	}

	if opts.reportedStateTimeout != 0 {
		s.reportedStateTimeout = opts.reportedStateTimeout
	}

	configmapwatcher.StartConfigMapWatcher(
		opts.Factory,
		configmapwatcher.WithNamespace(serviceNamespace),
		configmapwatcher.WithLabelSelector(instanceNameServiceLabel),
		configmapwatcher.WithUpdaterFn(func(cm *corev1.ConfigMap) {
			s.upsertServiceHandler(ctx, cm)
			if opts.onConfigMapUpdate != nil {
				opts.onConfigMapUpdate(cm)
			}
		}),
		configmapwatcher.WithDeleterFn(func(cm *corev1.ConfigMap) {
			s.removeServiceHandler(cm)
			if opts.onConfigMapDelete != nil {
				opts.onConfigMapDelete(cm)
			}
		}),
	)

	return s, nil
}

func (s *server) GetInstanceState(ctx context.Context, req *systemservicestatepb.GetInstanceStateRequest) (*systemservicestatepb.InstanceState, error) {
	if req.GetName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "instance name is required")
	}
	serviceHandler, found := s.serviceHandlers.Load(req.GetName())
	if !found {
		log.ErrorContextf(ctx, "service handler not found for %q", req.GetName())
		return nil, status.Errorf(codes.NotFound, "service instance %q not found", req.GetName())
	}
	if serviceHandler.registrationErrorMessage != "" {
		return &systemservicestatepb.InstanceState{
			Name: req.GetName(),
			State: &systemservicestatepb.State{
				StateCode: systemservicestatepb.State_STATE_CODE_ERROR,
				ExtendedStatus: extstatus.New(
					statusComponent,
					uint32(codes.FailedPrecondition),
					extstatus.WithTitle(registrationErrorTitle),
					extstatus.WithUserMessage(serviceHandler.registrationErrorMessage),
				).Proto(),
			},
		}, nil
	}

	state, err := s.containerState(req.GetName())
	if err != nil {
		return nil, err
	}
	if state.StateCode == systemservicestatepb.State_STATE_CODE_ENABLED && serviceHandler.supportsServiceState {
		timeoutCtx, cancel := context.WithTimeout(ctx, s.reportedStateTimeout)
		defer cancel()

		selfState, err := serviceHandler.ssClient.GetState(timeoutCtx, &servicestatepb.GetStateRequest{})
		switch status.Code(err) {
		case codes.OK:
			state = stateFromSelfState(selfState)
		case codes.Unimplemented:
			state = &systemservicestatepb.State{
				StateCode: systemservicestatepb.State_STATE_CODE_ENABLED,
				ExtendedStatus: extstatus.New(
					statusComponent,
					uint32(status.Code(err)),
					extstatus.WithTitle("Service does not implement ServiceState."),
					extstatus.WithUserMessage("The service has not implemented the ServiceState gRPC service, but has 'service_state_config' set in its service manifest."),
					extstatus.WithGrpcCode(status.Code(err)),
				).Proto(),
			}
		case codes.Unavailable, codes.DeadlineExceeded:
			state = &systemservicestatepb.State{
				StateCode: systemservicestatepb.State_STATE_CODE_ERROR,
				ExtendedStatus: extstatus.New(
					statusComponent,
					uint32(status.Code(err)),
					extstatus.WithTitle(selfStateErrorTitle),
					extstatus.WithUserMessage(fmt.Sprintf("The service may be unreachable: %v", err)),
					extstatus.WithGrpcCode(status.Code(err)),
				).Proto(),
			}
		default:
			log.ErrorContextf(ctx, "failed to get state from service %q: %v", req.GetName(), err)
			state = &systemservicestatepb.State{
				StateCode: systemservicestatepb.State_STATE_CODE_ERROR,
				ExtendedStatus: extstatus.New(
					statusComponent,
					uint32(status.Code(err)),
					extstatus.WithTitle(selfStateErrorTitle),
					extstatus.WithUserMessage(fmt.Sprintf("Failed to get state from service: %v", err)),
					extstatus.WithGrpcCode(status.Code(err)),
				).Proto(),
			}
		}
	}

	return &systemservicestatepb.InstanceState{Name: req.GetName(), State: state}, nil
}

func (s *server) EnableService(ctx context.Context, req *systemservicestatepb.EnableServiceRequest) (*systemservicestatepb.EnableServiceResponse, error) {
	if req.GetName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "instance name is required")
	}
	serviceHandler, found := s.serviceHandlers.Load(req.GetName())
	if !found {
		log.ErrorContextf(ctx, "service handler not found for %q", req.GetName())
		return nil, status.Errorf(codes.NotFound, "service instance %q not found", req.GetName())
	}
	if serviceHandler.registrationErrorMessage != "" {
		return nil, status.Errorf(codes.FailedPrecondition, "service state gRPC client could not be created")
	}

	state, err := s.containerState(req.GetName())
	if err != nil {
		return nil, status.Errorf(status.Code(err), "failed to get k8s container state: %v", err)
	}
	if state.StateCode == systemservicestatepb.State_STATE_CODE_STOPPED {
		return nil, status.Errorf(codes.FailedPrecondition, "service cannot be enabled from stopped state")
	}
	if state.StateCode == systemservicestatepb.State_STATE_CODE_ERROR {
		return nil, status.Errorf(codes.FailedPrecondition, "service cannot be enabled from the current error state: %v", extstatus.FromProto(state.GetExtendedStatus()).Err())
	}

	if !serviceHandler.supportsServiceState {
		return nil, status.Errorf(codes.Unimplemented, "service does not implement ServiceState")
	}

	if _, err = serviceHandler.ssClient.Enable(ctx, &servicestatepb.EnableRequest{}); err != nil {
		log.ErrorContextf(ctx, "failed to enable service %q: %v", req.GetName(), err)
		return nil, status.Errorf(status.Code(err), "failed to enable service: %v", err)
	}
	return &systemservicestatepb.EnableServiceResponse{}, nil
}

func (s *server) DisableService(ctx context.Context, req *systemservicestatepb.DisableServiceRequest) (*systemservicestatepb.DisableServiceResponse, error) {
	if req.GetName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "instance name is required")
	}
	serviceHandler, found := s.serviceHandlers.Load(req.GetName())
	if !found {
		log.ErrorContextf(ctx, "service handler not found for %q", req.GetName())
		return nil, status.Errorf(codes.NotFound, "service instance %q not found", req.GetName())
	}
	if serviceHandler.registrationErrorMessage != "" {
		return nil, status.Errorf(codes.FailedPrecondition, "service state gRPC client could not be created")
	}

	state, err := s.containerState(req.GetName())
	if err != nil {
		return nil, status.Errorf(status.Code(err), "failed to get k8s container state: %v", err)
	}
	if state.StateCode == systemservicestatepb.State_STATE_CODE_STOPPED {
		return nil, status.Errorf(codes.FailedPrecondition, "service cannot be disabled from stopped state")
	}
	if state.StateCode == systemservicestatepb.State_STATE_CODE_ERROR {
		return nil, status.Errorf(codes.FailedPrecondition, "service cannot be disabled from the current error state: %v", extstatus.FromProto(state.GetExtendedStatus()).Err())
	}

	if !serviceHandler.supportsServiceState {
		return nil, status.Errorf(codes.Unimplemented, "service does not implement ServiceState")
	}

	if _, err = serviceHandler.ssClient.Disable(ctx, &servicestatepb.DisableRequest{}); err != nil {
		log.ErrorContextf(ctx, "failed to disable service %q: %v", req.GetName(), err)
		return nil, status.Errorf(status.Code(err), "failed to disable service: %v", err)
	}
	return &systemservicestatepb.DisableServiceResponse{}, nil
}

func (s *server) RestartService(ctx context.Context, req *systemservicestatepb.RestartServiceRequest) (*systemservicestatepb.RestartServiceResponse, error) {
	if req.GetName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "instance name is required")
	}
	if _, found := s.serviceHandlers.Load(req.GetName()); !found {
		log.ErrorContextf(ctx, "service handler not found for %q", req.GetName())
		return nil, status.Errorf(codes.NotFound, "service instance %q not found", req.GetName())
	}

	containerName := render.ResourceContainerName(req.GetName())
	pod, err := s.k8sPodForService(containerName)
	if err != nil {
		return nil, status.Errorf(status.Code(err), "failed to get k8s container state: %v", err)
	}
	// Delete the pod and rely on the StatefulSet controller to recreate it.
	if err := s.k8sClient.CoreV1().Pods(serviceNamespace).Delete(ctx, pod.Name, metav1.DeleteOptions{}); err != nil {
		log.ErrorContextf(ctx, "failed to delete pod %q: %v", pod.Name, err)
		return nil, status.Errorf(codes.Internal, "failed to restart service: %v", err)
	}
	return &systemservicestatepb.RestartServiceResponse{}, nil
}

func (s *server) ListInstanceStates(ctx context.Context, req *systemservicestatepb.ListInstanceStatesRequest) (*systemservicestatepb.ListInstanceStatesResponse, error) {
	mu := sync.Mutex{}
	var states []*systemservicestatepb.InstanceState

	// Concurrently get the state of each service instance. This implementation may be changed to get
	// the last known states from a cache.
	eg, egCtx := errgroup.WithContext(ctx)
	// Limit the number of concurrent goroutines to something "reasonable". This is arbitrarily chosen
	// should absolutely be changed up or down based on any evidence whatsoever.
	eg.SetLimit(10)

	s.serviceHandlers.Range(func(name string, _ *serviceHandler) bool {
		eg.Go(func() error {
			res, err := s.GetInstanceState(egCtx, &systemservicestatepb.GetInstanceStateRequest{Name: name})
			if err != nil {
				// An error here means that we cannot get the state of the service. This state of this
				// service instance will not be reported.
				log.ErrorContextf(egCtx, "failed to get state from service %q: %v", name, err)
				return nil
			}
			mu.Lock()
			defer mu.Unlock()
			states = append(states, res)
			return nil
		})
		return true
	})

	if err := eg.Wait(); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to get states from services: %v", err)
	}
	return &systemservicestatepb.ListInstanceStatesResponse{States: states}, nil
}

func (s *server) upsertServiceHandler(ctx context.Context, cm *corev1.ConfigMap) {
	// Filter for configmaps that have a service instance label, since this function
	// may be called for all configmaps in the configured namespace.
	version, ok := cm.Labels[serviceMetadataVersionLabel]
	if !ok || version != "1" {
		return
	}

	instanceName, ok := cm.Labels[instanceNameServiceLabel]
	if !ok {
		log.ErrorContextf(ctx, "configmap %q has no %q label. Not creating service handler.", cm.Name, instanceNameServiceLabel)
		return
	}

	// Delete the service handler for this service instance if it exists.
	if sh, found := s.serviceHandlers.LoadAndDelete(instanceName); found {
		if sh.clientConn != nil {
			sh.clientConn.Close()
		}
	}
	sh := &serviceHandler{
		supportsServiceState: cm.Annotations[supportsServiceStateAnnotation] == "true",
	}
	if !sh.supportsServiceState {
		s.serviceHandlers.Store(instanceName, sh)
		return
	}

	servicePort, ok := cm.Annotations[servicePortAnnotation]
	if !ok {
		sh.registrationErrorMessage = fmt.Sprintf("could not find service port for %q. Not creating service handler.", instanceName)
		s.serviceHandlers.Store(instanceName, sh)
		return
	}

	clientConn, err := grpc.NewClient(
		serverAddress(render.ResourceContainerName(instanceName), servicePort),
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		sh.registrationErrorMessage = fmt.Sprintf("failed to create ServiceState client for %q: %v.", instanceName, err.Error())
		s.serviceHandlers.Store(instanceName, sh)
		return
	}

	sh.clientConn = clientConn
	sh.ssClient = servicestatepb.NewServiceStateClient(clientConn)
	s.serviceHandlers.Store(instanceName, sh)
}

func (s *server) removeServiceHandler(cm *corev1.ConfigMap) {
	instanceName, ok := cm.Labels[instanceNameServiceLabel]
	if !ok {
		log.Errorf("configmap %q has no %q label. Not removing service handler.", cm.Name, instanceNameServiceLabel)
		return
	}
	if sh, found := s.serviceHandlers.LoadAndDelete(instanceName); found {
		if sh.clientConn != nil {
			sh.clientConn.Close()
		}
	}
}

// containerState returns the state of the k8s container that runs the registered Service instance
// with the provided name. An instance without a pod or container counts as stopped, e.g. while its
// pod is being scheduled.
//
// Callers must check that the instance is registered (has a service handler) before calling this,
// since a NotFound error from k8s is only a stopped instance if the instance itself exists.
func (s *server) containerState(instanceName string) (*systemservicestatepb.State, error) {
	cs, err := s.k8sContainerState(render.ResourceContainerName(instanceName))
	if status.Code(err) == codes.NotFound {
		return &systemservicestatepb.State{StateCode: systemservicestatepb.State_STATE_CODE_STOPPED}, nil
	}
	if err != nil {
		return nil, err
	}
	return stateFromContainerState(cs, instanceName), nil
}

// k8sContainerState is a helper for getting the state of the k8s container that is running the
// service with the provided name.
//
// It is assumed that there is only a single pod with the provided service instance name in the
// cluster.
//
// This method also returns errors as gRPC status errors.
func (s *server) k8sContainerState(name string) (*corev1.ContainerState, error) {
	pod, err := s.k8sPodForService(name)
	if err != nil {
		return nil, err
	}

	for _, container := range pod.Status.ContainerStatuses {
		if container.Name != name {
			continue
		}
		return &container.State, nil
	}
	return nil, status.Errorf(codes.NotFound, "no running kubernetes container found for service %s", name)
}

func (s *server) k8sPodForService(name string) (*corev1.Pod, error) {
	label, err := metav1.ParseToLabelSelector(fmt.Sprintf("%s=%s", "app", name))
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to parse label selector: %v", err)
	}
	selector, err := metav1.LabelSelectorAsSelector(label)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to convert label selector: %v", err)
	}

	pods, err := s.podLister.Pods(serviceNamespace).List(selector)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list pods: %v", err)
	}

	if len(pods) == 0 {
		log.Errorf("no pod found for service %s", name)
		return nil, status.Errorf(codes.NotFound, "no running kubernetes container found for service %s", name)
	}
	return pods[0], nil
}

// stateFromContainerState converts the state of the running k8s container of the service to an
// appropriate system service state.
func stateFromContainerState(containerState *corev1.ContainerState, serviceName string) *systemservicestatepb.State {
	state := &systemservicestatepb.State{}
	if containerState.Terminated != nil {
		state.StateCode = systemservicestatepb.State_STATE_CODE_STOPPED
		return state
	}

	if waiting := containerState.Waiting; waiting != nil {
		switch waiting.Reason {
		case errImagePull, imagePullBackOff:
			state.StateCode = systemservicestatepb.State_STATE_CODE_ERROR
			state.ExtendedStatus = extstatus.New(
				statusComponent,
				uint32(codes.Internal),
				extstatus.WithTitle(imagePullErrorTitle),
				extstatus.WithUserMessage(fmt.Sprintf("Failed to pull service image for %q: %s", serviceName, waiting.Message)),
			).Proto()
		case crashLoopBackOff:
			state.StateCode = systemservicestatepb.State_STATE_CODE_ERROR
			state.ExtendedStatus = extstatus.New(
				statusComponent,
				uint32(codes.Internal),
				extstatus.WithTitle(crashLoopBackOffErrorTitle),
				extstatus.WithUserMessage(fmt.Sprintf("Service %q has crashed: %s", serviceName, waiting.Message)),
			).Proto()
		default:
			state.StateCode = systemservicestatepb.State_STATE_CODE_STOPPED
		}
		return state
	}

	state.StateCode = systemservicestatepb.State_STATE_CODE_ENABLED
	return state
}

// stateFromSelfState converts SelfState to State.
func stateFromSelfState(selfState *servicestatepb.SelfState) *systemservicestatepb.State {
	return &systemservicestatepb.State{
		StateCode:      systemservicestatepb.State_StateCode(selfState.GetStateCode()),
		ExtendedStatus: selfState.GetExtendedStatus(),
	}
}
