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

package render

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"regexp"
	"slices"
	"strings"

	log "github.com/golang/glog"
	crcv1alpha1 "github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/pkg/errors"
	"golang.org/x/exp/maps"
	"google.golang.org/protobuf/proto"
	kubev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/resource"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	kubeyaml "sigs.k8s.io/yaml"

	"intrinsic/assets/dependencies/resolver"
	"intrinsic/assets/idutils"
	"intrinsic/assets/services/inspection"
	"intrinsic/assets/services/servicevalidate"
	"intrinsic/kubernetes/workcell_spec/chartassignment"
	"intrinsic/kubernetes/workcell_spec/chartbuilder"
	"intrinsic/util/go/pointer"
	"intrinsic/util/go/validate"
	"intrinsic/util/go/xiter"
	"intrinsic/util/proto/names"

	iopb "intrinsic/assets/proto/installation_origin_go_proto"
	sppb "intrinsic/assets/services/proto/service_permissions_go_proto"
	svpb "intrinsic/assets/services/proto/service_volume_go_proto"
	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	sspb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"
	tpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rppb "intrinsic/resources/proto/resource_permissions_go_proto"
	rsdpb "intrinsic/resources/proto/resource_service_definition_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	rvpb "intrinsic/resources/proto/resource_volume_go_proto"
	rtcpb "intrinsic/resources/proto/runtime_context_go_proto"
	sddpb "intrinsic/skills/catalog/proto/skill_deployment_data_go_proto"

	_ "embed"
)

const (
	// resourceChartName is the name given to the resources chart.  This could
	// potentially be a dynamic thing, however, a number of different places
	// throughout our stack currently assume there is a chart named "resources"
	// that they can redeploy independently of opts.Parent.  There may be
	// reasons in the future, similar to b/280121803 for skills, where we want
	// to bundle resources.  At that point we'll need a single service
	// responsible for the naming of charts for resources.  That probably
	// should be the Asset Deployment Service.
	resourceChartName = "resources"

	// ResourceNamespace is the namespace for resource instance deployments.
	ResourceNamespace = "app-resources"

	// CfgMapRuntimeContextKey is the ConfigMap key use to store the RuntimeContext provided to the
	// Service.
	CfgMapRuntimeContextKey = "runtime_config.pb"

	// AssetInstanceHeader is the gRPC metadata header that contains the instance name of the asset.
	AssetInstanceHeader = "x-resource-instance-name"

	cfgMapMountPath = "/etc/intrinsic"
	// configMapSizeMaxBytes limits the size of the ConfigMap that can be
	// generated for resource instance.  The default max size for a ConfigMap
	// is 1MB, but we conservatively set this at 512KiB to match the validation
	// performed in helm_chart.bzl.
	configMapSizeMaxBytes = 524288
	// defaultIngressPort is the default port that we expect the ingress to be
	// served on, as well all custom hosts for simulation.
	defaultIngressPort int32 = 8080
	// defaultNonHostPort is handed out to all resources that do not require
	// use of the host network.  Its value is somewhat arbitrary number,
	// however, it needs to be in an unprivileged range.
	defaultNonHostPort int32 = 9090
	// defaultNonHostHTTPPort is where the resource serves its optional http
	// endpoint.
	defaultNonHostHTTPPort int32 = 9091
	// resourcesBasePort refers to the first port used for resource PODs.
	resourcesBasePort int32 = 17128
	// maxAllowedResourceServices limits the number of services that can be
	// created.
	maxAllowedResourceServices = 256

	maxLengthResourceName = 63

	// httpProxyPathPrefix specifies the path prefix under which services are served
	httpProxyPathPrefix = "/ext/services/"

	// IngressAddress is the address of the ingress gateway.
	IngressAddress = "istio-ingressgateway.app-ingress.svc.cluster.local:80"

	// resourceK8sPrefix is the value prepended to resource pods when not
	// sideloaded.
	resourceK8sPrefix string = "rs-"

	dataFilesLabel      = "data-files"
	dataFilesVolumeName = "/data"
	// dataFilesVolumeSizeLimit limits the size of the data files' volume that is provided for
	// resource instances. We conservatively set this at 10MiB to avoid excessive usage before the
	// data files story is fully fleshed out.
	dataFilesVolumeSizeLimit = "10Mi"

	// SkillChartName is the name given to the Skills chart.
	SkillChartName = "skills"
	// skillChartVersion is the version given to the Skills chart.
	skillChartVersion = "0.0.1"
	// skillChartTemplateFilename is the filename given to the embedded skills.yaml
	// template inside the generated Helm chart's templates/ directory so Helm
	// renders it as a Kubernetes manifest.
	skillChartTemplateFilename = "skills.yaml"
	// skillNamespace is the namespace for Skill deployments.
	skillNamespace = "skills"
	// resourceChartVersion is the version given to the resources chart.
	// Unclear if this should match the version of the parent.  I don't think
	// it actually matters since it's not released.
	resourceChartVersion = "0.0.1"
	// resourceChartTemplateFilename is the filename given to the embedded resources.yaml
	// template inside the generated Helm chart's templates/ directory so Helm
	// renders it as a Kubernetes manifest.
	resourceChartTemplateFilename = "resources.yaml"
	// SkillInstallationOriginKey is the name of the key on skill config maps that denotes the
	// installation origin of the skill.
	// LINT.IfChange(skill_installation_origin_key)
	SkillInstallationOriginKey = "installation_origin"
	// LINT.ThenChange(//intrinsic/assets/deploy/skills.yaml:installation_origin_key)
)

var (
	// This is necessary to resolve b/269447854, however, it doesn't
	// necessarily need to be applied to all resources.  Since there does not
	// appear to be a downside to specifying it for all of them, though, just
	// go ahead and do that.
	// TODO(b/271166278) only specify this on resources that publish topics.
	pubSubAllowList = kubev1.EnvVar{
		Name: "ALLOWED_PUBSUB_IPv4",
		ValueFrom: &kubev1.EnvVarSource{
			FieldRef: &kubev1.ObjectFieldSelector{
				FieldPath: "status.podIP",
			},
		},
	}
	// Avoid C-ARES flakiness: b/373783242
	grpcNativeResolver = kubev1.EnvVar{
		Name:  "GRPC_NATIVE_RESOLVER",
		Value: "native",
	}
	defaultContainerEnvs = []kubev1.EnvVar{
		pubSubAllowList,
		grpcNativeResolver,
	}

	// resource names must be a dot-separated string, where each sub-string consists only of
	// alphanumeric characters and underscores. Each substring must start with a character in [a-z].
	// TODO(b/276649099)
	validResourceNameRegexp = regexp.MustCompile(`^[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*$`)

	// See https://kubernetes.io/docs/concepts/overview/working-with-objects/names/.
	validNamespacedAddressRegexp = regexp.MustCompile(`^[a-z][a-z0-9-]*\.[a-z][a-z0-9-]*\.svc\.cluster\.local:\d+$`)

	// validPOSIXCapabilityRegex matches a valid POSIX capability string.
	validPOSIXCapabilityRegex = regexp.MustCompile(`[A-Z0-9_]+`)

	allowedStandardResourceNames = map[string]struct{}{
		"cpu":    {},
		"memory": {},
	}
	allowedGpuResourceNames = map[string]struct{}{
		// NVidia and AMD GPUs (https://kubernetes.io/docs/tasks/manage-gpus/scheduling-gpus/).
		"nvidia.com/gpu": {},
		"amd.com/gpu":    {},
		// Intel GPUs. Some or all of these may be incorrect, but see
		// https://intel.github.io/intel-device-plugins-for-kubernetes/cmd/gpu_plugin/README.html.
		"intel.com/gpu":      {},
		"intel.com/i915":     {},
		"gpu.intel.com/i915": {},
		"gpu.intel.com/xe":   {},
	}
	allowedEtherCatDriverNames = map[string]struct{}{
	}
	// allowedK8sResourceNames specifies allowed k8s resource labels for resource requirements. Any
	// resource that is not in this list (or matched by allowedK8sResourceNamesRegex) will not be
	// requestable.
	allowedK8sResourceNames = func(ins ...map[string]struct{}) map[string]struct{} {
		out := make(map[string]struct{})
		for _, in := range ins {
			maps.Copy(out, in)
		}
		return out
	}(allowedStandardResourceNames, allowedGpuResourceNames, allowedEtherCatDriverNames)
	allowedSysctlNames = map[string]struct{}{
		"net.ipv4.tcp_timestamps": {},
	}
	// allowedK8sResourceNamesRegex specifies additional regex patterns for allowed k8s resource
	// labels.
	allowedK8sResourceNamesRegex = []*regexp.Regexp{
		regexp.MustCompile(`^hugepages-\d+[A-Za-z]+$`),
	}

	// supportedDynamicReconfigurationVersions specifies the DynamicReconfiguration versions that
	// current runtime (i.e., AssetDeploymentService) supports.
	// LINT.IfChange(supported_dynamic_reconfiguration_versions)
	supportedDynamicReconfigurationVersions = map[drpb.DynamicReconfigurationConfig_ServiceVersion]bool{
		// Supports v1 dynamic reconfiguration from @intrinsic_apis.
		drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION: true,
	}
	// LINT.ThenChange(//intrinsic/assets/deploy/BUILD:dynamic_reconfiguration_versions)

	// supportedServiceStateVersions specifies the ServiceState versions that the current runtime
	// (i.e., SystemServiceStates) supports.
	// LINT.IfChange(supported_service_state_versions)
	supportedServiceStateVersions = map[sspb.ServiceStateConfig_ServiceVersion]bool{
		// Supports v1 service state from @intrinsic_apis.
		sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE: true,
	}
	// LINT.ThenChange(//intrinsic/assets/services/BUILD:service_state_versions)

	errNoInitDataFilesImageProvided = errors.New("no container image reference for the data files downloader was provided")
	errInvalidCASAddress            = errors.New("invalid content-addressable storage service address was provided")
	errMissingTypeInfo              = errors.New("missing type information")
	errUnableToAddResource          = errors.New("unable to generate chart for instance")
	errTooManyServicesOnHostNetwork = fmt.Errorf("Too many resource services on host network (max allowed ports=%d)", maxAllowedResourceServices)
	errInvalidSkillData             = errors.New("invalid skill deployment data")
	errInvalidSkillImage            = errors.New("invalid skill image")
	errInvalidServiceImage          = errors.New("invalid service image")
	errInvalidSysctl                = errors.New("invalid sysctl")
)

// ConfigMapName returns the ConfigMap name to use for the given resource instance.
func ConfigMapName(riName string) string {
	return fmt.Sprintf("rc-%s", sanitizeResourceInstanceName(riName))
}

// ResourceContainerName returns the container name to use for the given resource instance.
func ResourceContainerName(riName string) string {
	return fmt.Sprintf("%s%s", resourceK8sPrefix, sanitizeResourceInstanceName(riName))
}

func sanitizeResourceInstanceName(riName string) string {
	return strings.ReplaceAll(riName, "_", "-")
}

func inAllowlistedDir(fr *tpb.FileReference) bool {
	return strings.HasPrefix(fr.GetSpec().GetPath(), dataFilesVolumeName+"/")
}

// encodeRuntimeContext encodes a RuntimeContext for inclusion in a ConfigMap. It also returns a
// hash that can be used as a pod annotation to induce the pod to restart whenever the
// RuntimeContext changes.
//
// For Services that support dynamic reconfiguration, we do not want config changes to trigger pod
// restarts, so we exclude the config from the generated hash.
//
// NOTE: This function mutates runtimeContext.
func encodeRuntimeContext(runtimeContext *rtcpb.RuntimeContext, supportsDynamicReconfiguration bool) (string, string, error) {
	contextBase64, contextID, err := base64EncodeProto(runtimeContext)
	if err != nil {
		return "", "", fmt.Errorf("unable to encode runtime context for %q: %w", runtimeContext.GetName(), err)
	}
	if supportsDynamicReconfiguration {
		runtimeContext.Config = nil
		if _, contextIDNoConfig, err := base64EncodeProto(runtimeContext); err != nil {
			return "", "", fmt.Errorf("unable to encode runtime context (without config) for %q: %w", runtimeContext.GetName(), err)
		} else {
			contextID = contextIDNoConfig
		}
	}

	return contextBase64, contextID, nil
}

// base64EncodeProto serializes a binary proto and then base64 encodes it
// appropriately so that it can be stored in a ConfigMap.  A size limit is
// enforced below the default max for ConfigMaps.  A hash of the serialized
// proto is also returned for enforcing behaviors based on the uniqueness of
// the data.
func base64EncodeProto(m proto.Message) (string, string, error) {
	// Use deterministic flag to keep the serialized data stable, and to prevent a resource instance
	// from being restarted because its serialized runtime context got changed. See b/306400792
	// for more details.
	b, err := proto.MarshalOptions{Deterministic: true}.Marshal(m)
	if err != nil {
		return "", "", fmt.Errorf("unable to marshal resource instance proto %v", err)
	}
	if len(b) > configMapSizeMaxBytes {
		return "", "", fmt.Errorf("proto binary size of %d is larger than the max of %d", len(b), configMapSizeMaxBytes)
	}
	encoded := base64.StdEncoding.EncodeToString(b)
	hashed := fmt.Sprintf("%x", sha256.Sum256(b))
	return encoded, hashed, nil
}

func buildDockerCfgJSON(registry, user, password string) ([]byte, error) {
	type dockercfg struct {
		Username string `json:"username"`
		Password string `json:"password"`
		Email    string `json:"email"`
		Auth     []byte `json:"auth"`
	}

	m := map[string]any{
		"https://" + registry: dockercfg{
			Username: user,
			Password: password,
			// Email is required, but not used, so we just put in a fake value.
			Email: "not@val.id",
			Auth:  []byte(user + ":" + password),
		},
	}
	b, err := json.Marshal(m)
	if err != nil {
		return nil, fmt.Errorf("unable to create image pull secrets")
	}
	return b, nil
}

func buildDockerCfgBase64FromImage(i *ipb.Image) (string, error) {
	usr := i.GetAuthUser()
	pwd := i.GetAuthPassword()
	if usr == "" || pwd == "" {
		return "", nil
	}
	rs := strings.SplitN(i.Registry, "/", 2)
	registry := rs[0]
	privateRegistryKey, err := buildDockerCfgJSON(registry, usr, pwd)
	if err != nil {
		return "", err
	}
	return base64.StdEncoding.EncodeToString(privateRegistryKey), nil
}

func volume(rv *rvpb.Volume) (*kubev1.Volume, error) {
	if err := validateVolume(rv); err != nil {
		return nil, err
	}

	v := &kubev1.Volume{
		Name: rv.GetName(),
	}
	switch rv.Source.(type) {
	case *rvpb.Volume_EmptyDir:
		v.EmptyDir = &kubev1.EmptyDirVolumeSource{
			Medium: kubev1.StorageMedium(rv.GetEmptyDir().GetMedium()),
		}
	case *rvpb.Volume_HostPath:
		v.HostPath = &kubev1.HostPathVolumeSource{
			Path: rv.GetHostPath().GetPath(),
			Type: pointer.To(kubev1.HostPathType(rv.GetHostPath().GetType())),
		}
	default:
		return nil, fmt.Errorf("volume %q had unexpected source type", rv.GetName())
	}

	return v, nil
}

func validateVolume(rv *rvpb.Volume) error {
	if sv, err := serviceVolumeFrom(rv); err != nil {
		return err
	} else if err = servicevalidate.Volume(sv); err != nil {
		return err
	}

	switch rv.Source.(type) {
	case *rvpb.Volume_HostPath:
		if err := validate.Alphabetic(rv.GetHostPath().GetType()); err != nil {
			return fmt.Errorf("invalid host path type: %w", err)
		}
	}

	return nil
}

func serviceVolumeFrom(rv *rvpb.Volume) (*svpb.Volume, error) {
	sv := &svpb.Volume{
		Name: rv.GetName(),
	}

	switch rv.Source.(type) {
	case *rvpb.Volume_EmptyDir:
		sv.Source = &svpb.Volume_EmptyDir{
			EmptyDir: &svpb.EmptyDirVolumeSource{},
		}
		switch rv.GetEmptyDir().GetMedium() {
		case "":
			sv.GetEmptyDir().Medium = svpb.EmptyDirMedium_EMPTY_DIR_MEDIUM_UNSPECIFIED
		case "Memory":
			sv.GetEmptyDir().Medium = svpb.EmptyDirMedium_EMPTY_DIR_MEDIUM_MEMORY
		default:
			return nil, fmt.Errorf("unknown empty dir medium %q", rv.GetEmptyDir().GetMedium())
		}
	case *rvpb.Volume_HostPath:
		sv.Source = &svpb.Volume_HostPath{
			HostPath: &svpb.HostPathVolumeSource{
				Path: rv.GetHostPath().GetPath(),
			},
		}
		switch rv.GetHostPath().GetType() {
		case "":
		case "CharDevice":
			sv.GetHostPath().Type = svpb.HostPathVolumeSourceType_HOST_PATH_VOLUME_SOURCE_TYPE_CHAR_DEVICE.Enum()
		default:
			return nil, fmt.Errorf("unknown host path type %q", rv.GetHostPath().GetType())
		}
	}

	return sv, nil
}

func volumeMount(rvm *rvpb.VolumeMount) (*kubev1.VolumeMount, error) {
	if err := validateVolumeMount(rvm); err != nil {
		return nil, err
	}

	// TODO(b/257676629): check names match volumes in PodSpec.
	return &kubev1.VolumeMount{
		Name:      rvm.GetName(),
		MountPath: rvm.GetMountPath(),
		ReadOnly:  rvm.GetReadOnly(),
	}, nil
}

func validateVolumeMount(rvm *rvpb.VolumeMount) error {
	return servicevalidate.VolumeMount(serviceVolumeMountFrom(rvm))
}

func serviceVolumeMountFrom(rvm *rvpb.VolumeMount) *svpb.VolumeMount {
	return &svpb.VolumeMount{
		Name:      rvm.GetName(),
		MountPath: rvm.GetMountPath(),
		ReadOnly:  rvm.GetReadOnly(),
	}
}

func convertSysctl(p *sppb.Sysctl) (*kubev1.Sysctl, error) {
	if p == nil {
		return nil, nil
	}
	if _, ok := allowedSysctlNames[p.GetName()]; !ok {
		return nil, fmt.Errorf("name %q is not allowed", p.GetName())
	}

	return &kubev1.Sysctl{
		Name:  p.GetName(),
		Value: p.GetValue(),
	}, nil
}

func convertPodSecurityContext(p *sppb.PodSecurityContext) (*kubev1.PodSecurityContext, error) {
	if p == nil {
		return nil, nil
	}
	var sysctls []kubev1.Sysctl
	for _, s := range p.GetSysctls() {
		sysctl, err := convertSysctl(s)
		if err != nil {
			return nil, fmt.Errorf("%w: %w", errInvalidSysctl, err)
		}
		sysctls = append(sysctls, *sysctl)
	}
	return &kubev1.PodSecurityContext{
		Sysctls: sysctls,
	}, nil
}

func validateAndAssignQuantities(resourceQuantities map[string]string) (*kubev1.ResourceList, error) {
	if len(resourceQuantities) == 0 {
		return nil, nil
	}
	resourceList := make(kubev1.ResourceList)
	for name, quantity := range resourceQuantities {
		if err := validateK8sResourceName(name); err != nil {
			return nil, err
		}
		val, err := resource.ParseQuantity(quantity)
		if err != nil {
			return nil, fmt.Errorf("unable to parse value for request %q: %v", name, err)
		}
		resourceList[kubev1.ResourceName(name)] = val
	}
	return &resourceList, nil
}

func normalizeGpuRequirements(requirements *kubev1.ResourceRequirements, clusterParams ClusterParams) error {
	for name := range requirements.Requests {
		if _, ok := allowedGpuResourceNames[name.String()]; !ok {
			continue
		}
		// You cannot specify GPU requests without specifying limits.
		// https://kubernetes.io/docs/tasks/manage-gpus/scheduling-gpus/
		if _, ok := requirements.Limits[name]; !ok {
			return fmt.Errorf("unable to specify GPU requests without specifying limits")
		}
	}
	for name, limit := range requirements.Limits {
		if _, ok := allowedGpuResourceNames[name.String()]; !ok {
			continue
		}
		// You can specify GPU limits without specifying requests, because Kubernetes will use the limit as the request value by default.
		// https://kubernetes.io/docs/tasks/manage-gpus/scheduling-gpus/
		request, ok := requirements.Requests[name]
		if !ok {
			continue
		}
		// You can specify GPU in both limits and requests but these two values must be equal.
		// https://kubernetes.io/docs/tasks/manage-gpus/scheduling-gpus/
		if request == limit {
			continue
		}
		// Special handling by us.
		if !request.IsZero() {
			return fmt.Errorf("GPU request %q is neither zero nor equal to limit %q", request.String(), limit.String())
		}
		if clusterParams.HasGpu {
			// Make requests equal to limits to fulfill the requirements in
			// https://kubernetes.io/docs/tasks/manage-gpus/scheduling-gpus/
			requirements.Requests[name] = limit
		} else {
			// Entirely remove GPU requests and limits if the resource is not available.
			delete(requirements.Requests, name)
			delete(requirements.Limits, name)
		}
	}

	if len(requirements.Requests) == 0 {
		requirements.Requests = nil
	}
	if len(requirements.Limits) == 0 {
		requirements.Limits = nil
	}

	return nil
}

func resources(p *rppb.ResourceRequirements, clusterParams ClusterParams) (*kubev1.ResourceRequirements, error) {
	if p == nil {
		return nil, nil
	}
	k := &kubev1.ResourceRequirements{}

	if requests, err := validateAndAssignQuantities(p.GetRequests()); err != nil {
		return nil, err
	} else if requests != nil {
		k.Requests = *requests
	}
	if limits, err := validateAndAssignQuantities(p.GetLimits()); err != nil {
		return nil, err
	} else if limits != nil {
		k.Limits = *limits
	}
	if err := normalizeGpuRequirements(k, clusterParams); err != nil {
		return nil, err
	}
	return k, nil
}

func convertSecurityContext(rsc *rppb.SecurityContext) (*kubev1.SecurityContext, error) {
	if rsc == nil {
		return nil, nil
	}
	sc := &kubev1.SecurityContext{}

	if capabilities := rsc.GetCapabilities(); capabilities != nil {
		sc.Capabilities = new(kubev1.Capabilities)
		for _, c := range capabilities.GetAdd() {
			if err := validatePOSIXCapability(c); err != nil {
				return nil, err
			}
			sc.Capabilities.Add = append(sc.Capabilities.Add, kubev1.Capability(c))
		}
	}

	if rsc.GetPrivileged() {
		sc.Privileged = proto.Bool(true)
	}

	return sc, nil
}

// TODO(b/263305147): Once all resources correctly split sim and real images,
// return an empty list instead of falling back to non-sim image.
func getSpecWithFallback(rt *rtrpb.ResourceTypeRuntime, level rtcpb.RuntimeContext_Level) *rsdpb.ResourceSpec {
	sd := rt.GetServiceDef()
	if sd == nil {
		return nil
	}

	switch level {
	case rtcpb.RuntimeContext_PHYSICS_SIM:
		if simSpec := sd.GetSimSpec(); simSpec != nil {
			return simSpec
		}
		return sd.GetRealSpec()
	case rtcpb.RuntimeContext_REALITY:
		return sd.GetRealSpec()
	default:
		return sd.GetRealSpec()
	}
}

// podSpecForResource builds the v1 kubeapi PodSpec for the given resource
// container to be used when rendering the chart for resources.
func podSpecForResource(ri *ripb.ResourceInstance, rt *rtrpb.ResourceTypeRuntime, clusterParams ClusterParams, prefix string, level rtcpb.RuntimeContext_Level) (*kubev1.PodSpec, []string, error) {
	rsName := sanitizeResourceInstanceName(ri.GetName())
	instanceName := fmt.Sprintf("%s%s", prefix, rsName)
	cfgMapName := fmt.Sprintf("rc-%s", rsName)

	rs := getSpecWithFallback(rt, level)
	if rs == nil {
		return nil, nil, nil
	}

	requiredHostName := ri.GetSchedulingConfig().GetRequiredNodeHostname()
	if err := validate.UserString(requiredHostName); err != nil {
		return nil, nil, err
	}

	var nodeSelector map[string]string
	if level == rtcpb.RuntimeContext_REALITY && ri.SchedulingConfig != nil && requiredHostName != "" {
		nodeSelector = map[string]string{
			"kubernetes.io/hostname": ri.GetSchedulingConfig().GetRequiredNodeHostname(),
		}
	}

	if numImages := len(rs.GetImage()); numImages < 1 {
		idVersion := idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion())
		return nil, nil, fmt.Errorf("resource type %q doesn't contain at least one image", idVersion)
	}
	var containers []kubev1.Container
	var privateRegistryKeys []string
	var imagePullSecrets []kubev1.LocalObjectReference
	anyImageRequiresRtpcNode := false
	for imageIdx, image := range rs.GetImage() {
		if err := validate.Image(image.GetImage()); err != nil {
			return nil, nil, fmt.Errorf("invalid image: %w", err)
		}
		imageName := fmt.Sprintf("%s/%s%s", image.GetImage().GetRegistry(), image.GetImage().GetName(), image.GetImage().GetTag())
		anyImageRequiresRtpcNode = anyImageRequiresRtpcNode || image.GetRequiresRtpcNode()

		var err error
		privateRegistryKey, err := buildDockerCfgBase64FromImage(image.GetImage())
		if err != nil {
			return nil, nil, fmt.Errorf("%w: invalid private auth: %v", errInvalidServiceImage, err)
		}
		if privateRegistryKey != "" {
			imagePullSecrets = append(imagePullSecrets, kubev1.LocalObjectReference{
				Name: fmt.Sprintf("%s-private-registry-key-%d", instanceName, len(privateRegistryKeys)),
			})
			privateRegistryKeys = append(privateRegistryKeys, privateRegistryKey)
		}
		var envs []kubev1.EnvVar
		for _, ev := range image.GetEnvVars() {
			envs = append(envs, kubev1.EnvVar{
				Name:  ev.GetName(),
				Value: ev.GetValue(),
			})
		}
		containerName := instanceName
		if imageIdx > 0 {
			containerName = fmt.Sprintf("%s-%d", instanceName, imageIdx)
		}
		containers = append(containers, kubev1.Container{
			Name:  containerName,
			Image: imageName,
			Env:   envs,
			Args:  image.GetArgs(),
		})
	}
	var initContainers []kubev1.Container
	for imageIdx, image := range rs.GetInitContainers() {
		if err := validate.Image(image.GetImage()); err != nil {
			return nil, []string{}, fmt.Errorf("invalid image: %w", err)
		}
		imageName := fmt.Sprintf("%s/%s%s", image.GetImage().GetRegistry(), image.GetImage().GetName(), image.GetImage().GetTag())

		privateRegistryKey, err := buildDockerCfgBase64FromImage(image.GetImage())
		if err != nil {
			return nil, []string{}, fmt.Errorf("%w: invalid private auth: %v", errInvalidServiceImage, err)
		}
		if privateRegistryKey != "" {
			imagePullSecrets = append(imagePullSecrets, kubev1.LocalObjectReference{
				Name: fmt.Sprintf("%s-private-registry-key-%d", instanceName, len(privateRegistryKeys)),
			})
			privateRegistryKeys = append(privateRegistryKeys, privateRegistryKey)
		}
		var envs []kubev1.EnvVar
		for _, ev := range image.GetEnvVars() {
			envs = append(envs, kubev1.EnvVar{
				Name:  ev.GetName(),
				Value: ev.GetValue(),
			})
		}
		initContainers = append(initContainers, kubev1.Container{
			Name:  fmt.Sprintf("%s-init-%d", instanceName, imageIdx),
			Image: imageName,
			Env:   envs,
			Args:  image.GetArgs(),
		})
	}
	var tolerations []kubev1.Toleration
	var affinity *kubev1.Affinity
	if anyImageRequiresRtpcNode {
		tolerations = []kubev1.Toleration{{
			Key:      "rtpc",
			Operator: kubev1.TolerationOpExists,
			Effect:   kubev1.TaintEffectNoSchedule,
		}}

		affinity = &kubev1.Affinity{
			NodeAffinity: &kubev1.NodeAffinity{
				RequiredDuringSchedulingIgnoredDuringExecution: &kubev1.NodeSelector{
					NodeSelectorTerms: []kubev1.NodeSelectorTerm{
						{
							MatchExpressions: []kubev1.NodeSelectorRequirement{
								{
									Key:      "intrinsic.ai/node-role",
									Values:   []string{"rtpc"},
									Operator: kubev1.NodeSelectorOpIn,
								},
							},
						},
					},
				},
			},
		}
	}

	securityContext, err := convertPodSecurityContext(rs.GetSecurityContext())
	if err != nil {
		return nil, nil, fmt.Errorf("invalid pod security context: %w", err)
	}

	spec := &kubev1.PodSpec{
		DNSPolicy:        kubev1.DNSClusterFirstWithHostNet,
		NodeSelector:     nodeSelector,
		Tolerations:      tolerations,
		Affinity:         affinity,
		Containers:       containers,
		InitContainers:   initContainers,
		ImagePullSecrets: imagePullSecrets,
		SecurityContext:  securityContext,
	}

	// TODO(b/257676629): check that this doesn't conflict with something
	// specified by the user.
	cfgMapVolume := kubev1.Volume{
		Name: cfgMapName,
		VolumeSource: kubev1.VolumeSource{
			ConfigMap: &kubev1.ConfigMapVolumeSource{
				LocalObjectReference: kubev1.LocalObjectReference{
					Name: cfgMapName,
				},
			},
		},
	}

	cfgMapVolumeMount := kubev1.VolumeMount{
		// Mount the configMap to expose the runtime context
		// and the resource instance to the resource container.
		Name:      cfgMapName,
		MountPath: cfgMapMountPath,
		ReadOnly:  true,
	}

	spec.HostNetwork = rs.GetHostNetwork()
	for _, rv := range rs.GetVolumes() {
		v, err := volume(rv)
		if err != nil {
			return nil, nil, fmt.Errorf("problem with volume in resource instance %q: %v", ri.GetName(), err)
		}
		spec.Volumes = append(spec.Volumes, *v)
	}
	spec.Volumes = append(spec.Volumes, cfgMapVolume)

	for idx := range spec.Containers {
		for _, rvm := range rs.GetImage()[idx].GetVolumeMounts() {
			vm, err := volumeMount(rvm)
			if err != nil {
				return nil, nil, fmt.Errorf("problem with volume mount in resource instance %q: %v", ri.GetName(), err)
			}
			spec.Containers[idx].VolumeMounts = append(spec.Containers[idx].VolumeMounts, *vm)
		}
		spec.Containers[idx].VolumeMounts = append(spec.Containers[idx].VolumeMounts, cfgMapVolumeMount)
		spec.Containers[idx].Env = append(spec.Containers[idx].Env, defaultContainerEnvs...)

		sc, err := convertSecurityContext(rs.GetImage()[idx].GetSecurityContext())
		if err != nil {
			return nil, nil, fmt.Errorf("problem with security context in resource instance %q: %v", ri.GetName(), err)
		}
		spec.Containers[idx].SecurityContext = sc

		rr, err := resources(rs.GetImage()[idx].GetResources(), clusterParams)
		if err != nil {
			return nil, nil, fmt.Errorf("problem with image resources in resource instance %q: %v", ri.GetName(), err)
		}
		if rr != nil {
			spec.Containers[idx].Resources = *rr
		}
	}
	for idx := range spec.InitContainers {
		for _, rvm := range rs.GetInitContainers()[idx].GetVolumeMounts() {
			vm, err := volumeMount(rvm)
			if err != nil {
				return nil, []string{}, fmt.Errorf("problem with volume mount in resource instance %q: %v", ri.GetName(), err)
			}
			spec.InitContainers[idx].VolumeMounts = append(spec.InitContainers[idx].VolumeMounts, *vm)
		}
		spec.InitContainers[idx].VolumeMounts = append(spec.InitContainers[idx].VolumeMounts, cfgMapVolumeMount)
		spec.InitContainers[idx].Env = append(spec.InitContainers[idx].Env, defaultContainerEnvs...)
	}
	return spec, privateRegistryKeys, nil
}

func setEnvironmentVariables(spec *kubev1.PodSpec, ri *ripb.ResourceInstance, httpPort int32, httpPathPrefix string) {
	if spec == nil || spec.Containers == nil {
		return
	}

	for i := range spec.Containers {
		container := &spec.Containers[i]
		if httpPort != 0 { // HTTP enabled
			container.Env = append(container.Env,
				kubev1.EnvVar{
					Name:  "INTRINSIC_HTTP_PATH_PREFIX",
					Value: httpPathPrefix,
				},
			)
			container.Env = append(container.Env,
				kubev1.EnvVar{
					Name:  "INTRINSIC_HTTP_PORT",
					Value: fmt.Sprintf("%d", httpPort),
				},
			)
		}
	}
}

// maybeAddInitContainerForDataFiles takes a Kubernetes pod spec. If the spec is nil, or if there
// are no relevant data files, it returns without doing any changes. Otherwise, it modifies the
// Kubernetes pod spec in-place so that the data files are provided to the resource instance's
// containers during pod initialization. To this end, this function adds an init container, a
// memory-backed volume, and volume mounts. If the spec already has init containers, the data files
// downloader is prepended to the list of the init containers so that it is executed in the very
// beginning.
//
// This function does not mount the data files volume to other init containers, should there be any.
// Feel free to implement this if there is a need.
//
// This function returns an error if the provided image reference or the CAS address are invalid.
func maybeAddInitContainerForDataFiles(spec *kubev1.PodSpec, ri *ripb.ResourceInstance, prefix, image, casAddress string, enableCASPeerMetadata bool) error {
	if spec == nil {
		return nil
	}
	if !slices.ContainsFunc(ri.GetDataFiles().GetFiles(), func(fr *tpb.FileReference) bool {
		return strings.HasPrefix(fr.GetSpec().GetPath(), dataFilesVolumeName+"/")
	}) {
		return nil
	}
	if image == "" {
		return errNoInitDataFilesImageProvided
	}
	if !validNamespacedAddressRegexp.MatchString(casAddress) {
		return fmt.Errorf("%w: got %q, must match %v", errInvalidCASAddress, casAddress, validNamespacedAddressRegexp.String())
	}

	cfgMapName := ConfigMapName(ri.GetName())
	cfgMapVolumeMount := kubev1.VolumeMount{
		// Mount the configMap to expose the runtime context
		// and the resource instance to the resource container.
		Name:      cfgMapName,
		MountPath: cfgMapMountPath,
		ReadOnly:  true,
	}

	// Should the spec already have some init containers, make sure to add the data files container
	// as the first one.
	spec.InitContainers = append(
		[]kubev1.Container{
			{
				Name:  fmt.Sprintf("%sinit-%s", prefix, dataFilesLabel),
				Image: image,
				Args: []string{
					"/intrinsic/storage/download_data_files/init_resource_instance_data_files",
					fmt.Sprintf("--cas_address=%s", casAddress),
					fmt.Sprintf("--runtime_context=%s/%s", cfgMapMountPath, CfgMapRuntimeContextKey),
					fmt.Sprintf("--enable_cas_peer_metadata=%t", enableCASPeerMetadata),
				},
				VolumeMounts: []kubev1.VolumeMount{
					cfgMapVolumeMount,
					{
						Name:      dataFilesLabel,
						MountPath: dataFilesVolumeName,
						ReadOnly:  false, // The init container must be able to write files.
					},
				},
			},
		},
		spec.InitContainers...,
	)

	dataFilesVolumeLimit := resource.MustParse(dataFilesVolumeSizeLimit)
	dataFilesVolume := kubev1.Volume{
		Name: dataFilesLabel,
		VolumeSource: kubev1.VolumeSource{
			EmptyDir: &kubev1.EmptyDirVolumeSource{
				Medium:    kubev1.StorageMediumMemory,
				SizeLimit: &dataFilesVolumeLimit,
			},
		},
	}
	spec.Volumes = append(spec.Volumes, dataFilesVolume)

	dataFilesVolumeMount := kubev1.VolumeMount{
		Name:      dataFilesLabel,
		MountPath: dataFilesVolumeName,
		ReadOnly:  true,
	}
	for idx := range spec.Containers {
		spec.Containers[idx].VolumeMounts = append(spec.Containers[idx].VolumeMounts, dataFilesVolumeMount)
	}
	return nil
}

func appPodAddress(hostName string, appName string) string {
	// Simulation may run in a different namespace which requires setting the
	// full host name.
	return fmt.Sprintf("%s.app-%s.svc.cluster.local", hostName, appName)
}

// serviceValuesForResource generates the values required for the service
// section of a resource yaml template.
func serviceValuesForResource(ri *ripb.ResourceInstance, rt *rtrpb.ResourceTypeRuntime, level rtcpb.RuntimeContext_Level, port int32, appName string) (map[string]any, error) {
	if !requiresGRPCPort(rt) {
		return nil, nil
	}
	protoPrefixes := rt.GetServiceDef().GetServiceProtoPrefixes()
	for _, protoPrefix := range protoPrefixes {
		if err := validateProtoNameOrPrefix(protoPrefix); err != nil {
			return nil, fmt.Errorf("invalid proto prefix: %w", err)
		}
	}

	hostName, hostPort := ResourceContainerName(ri.GetName()), port
	// For backwards compatibility with older resource definitions that use "noop_image" in physics simulation,
	// route camera service calls to the centralized "camera-sim" service.
	if level == rtcpb.RuntimeContext_PHYSICS_SIM && requiresDirectIngressRoutingToGazebo(rt.GetServiceDef().GetSimSpec(), protoPrefixes) {
		hostName, hostPort = appPodAddress("camera-sim", appName), defaultIngressPort
	}

	m := map[string]any{
		"port":                   port,
		"proto_prefixes":         protoPrefixes,
		"host_name":              hostName,
		"host_port":              hostPort,
		"supports_service_state": supportsServiceState(rt),
	}
	return m, nil
}

func requiresDirectIngressRoutingToGazebo(simSpec *rsdpb.ResourceSpec, protoPrefixes []string) bool {
	if simSpec == nil || len(simSpec.GetImage()) != 1 || !strings.HasSuffix(simSpec.GetImage()[0].GetImage().GetName(), ".noop_image") {
		return false
	}
	return slices.Contains(protoPrefixes, "/intrinsic_proto.perception.v1.CameraService/")
}

func validateProtoNameOrPrefix(protoNameOrPrefix string) error {
	err := names.ValidateProtoPrefix(protoNameOrPrefix)
	if errors.Is(err, names.ErrInvalidProtoPrefix) {
		return names.ValidateProtoName(protoNameOrPrefix)
	}
	return err
}

// httpValuesForResource generates the values required for the http section of
// a resource yaml template.
func httpValuesForResource(ri *ripb.ResourceInstance, rt *rtrpb.ResourceTypeRuntime, port int32, pathPrefix string) map[string]any {
	if !rt.GetServiceDef().GetProvidesHttpEndpoint() && rt.GetServiceDef().GetHttpConfig() == nil {
		return nil
	}

	return map[string]any{
		"port":        port,
		"host_name":   ResourceContainerName(ri.GetName()),
		"path_prefix": pathPrefix,
	}
}

// k8sResourceNameUnchecked returns a potential identifier for a name of a
// resource, such as a chart.  No validation is done on the length or format
// under the presumption that validation will be done by requesting the
// resource.
func k8sResourceNameUnchecked(name string) string {
	return strings.ReplaceAll(strings.ReplaceAll(name, "_", "-"), ".", "-")
}

// k8sResourceName returns a valid kubernetes resource name for the given name+type.
//
// Returns an error if the provided name cannot form a valid resource name.
func k8sResourceName(name, resourceType string) (string, error) {
	if len(name) > maxLengthResourceName {
		return "", fmt.Errorf("invalid %s name: %q. The max length is %d characters", resourceType, name, maxLengthResourceName)
	}
	if !validResourceNameRegexp.MatchString(name) {
		return "", fmt.Errorf("invalid %s name: %q", resourceType, name)
	}
	return k8sResourceNameUnchecked(name), nil
}

// maxSkillK8sResourceNameLength is the maximum length of a sanitized Skill name.
//
// We limit this to 50 characters to ensure that prefixes (such as "sc-") added to form k8s resource
// names like ConfigMaps do not exceed the 63-character limit.
const maxSkillK8sResourceNameLength = 50

// SkillContainerName returns the deterministic, unique, and length-safe Kubernetes
// container name for a Skill ID or IDVersion (e.g. "ai.intrinsic.attach_object_to_robot")
// formatted as <name>-<package>-<hash>.
func SkillContainerName(id string) (string, error) {
	name, err := idutils.NameFrom(id)
	if err != nil {
		return "", err
	}
	pkg, err := idutils.PackageFrom(id)
	if err != nil {
		return "", err
	}
	combined := k8sResourceNameUnchecked(fmt.Sprintf("%s-%s", name, pkg))
	if len(combined) <= maxSkillK8sResourceNameLength {
		return combined, nil
	}
	hash := sha256.Sum256([]byte(combined))
	hashHex := hex.EncodeToString(hash[:])[:8]
	truncated := strings.TrimRight(combined[:maxSkillK8sResourceNameLength-len(hashHex)-1], "-")
	return fmt.Sprintf("%s-%s", truncated, hashHex), nil
}

// servicePorts contains the ports assigned to a Service instance.
type servicePorts struct {
	GRPC int32
	HTTP int32
}

// resourceInstanceChartValueOpts holds options for resourceInstanceChartValue.
type resourceInstanceChartValueOpts struct {
	Instance                           *ripb.ResourceInstance
	Type                               *rtrpb.ResourceTypeRuntime
	AppName                            string
	Simulated                          bool
	ServicePorts                       *servicePorts
	InitDataFilesImage                 string
	InitDataFilesCASAddress            string
	InitDataFilesEnableCASPeerMetadata bool
	ClusterParams
}

// resourceInstanceChartValue returns chart values for a given resource instance.
func resourceInstanceChartValue(opts *resourceInstanceChartValueOpts) (map[string]any, error) {
	svcDef := opts.Type.GetServiceDef()
	if svcDef == nil {
		return nil, fmt.Errorf("resource instance %q does not define any services", opts.Instance.GetName())
	}

	var grpcPort, httpPort int32
	if opts.ServicePorts != nil {
		grpcPort = opts.ServicePorts.GRPC
		httpPort = opts.ServicePorts.HTTP
	}

	level := rtcpb.RuntimeContext_REALITY
	if opts.Simulated {
		level = rtcpb.RuntimeContext_PHYSICS_SIM
	}

	var httpPathPrefix string
	if opts.Type.GetServiceDef().GetProvidesHttpEndpoint() || opts.Type.GetServiceDef().GetHttpConfig() != nil {
		httpPathPrefix = httpProxyPathPrefix + opts.Instance.GetName()
	}

	var serviceInspectionTopic *string
	if topic, ok := inspection.Topic(opts.Instance.GetName(), opts.Type); ok {
		serviceInspectionTopic = &topic
	}

	config := opts.Instance.GetConfiguration()
	if resolvedConfig, err := resolver.New(IngressAddress, AssetInstanceHeader).ResolveConfigDependencies(config, opts.Type.GetMetadata().GetFileDescriptorSet()); err != nil {
		log.Errorf("failed to resolve config dependencies for instance %q: %v", opts.Instance.GetName(), err)
	} else {
		config = resolvedConfig
	}

	files := slices.Collect(xiter.Filter(inAllowlistedDir, slices.Values(opts.Instance.GetDataFiles().GetFiles())))
	contextBase64, contextID, err := encodeRuntimeContext(&rtcpb.RuntimeContext{
		Port:                    grpcPort,
		HttpPort:                httpPort,
		HttpPathPrefix:          httpPathPrefix,
		Level:                   level,
		SimulationServerAddress: "simulation-server.app-" + opts.AppName + ".svc.cluster.local",
		ProductServiceAddress:   IngressAddress,
		Name:                    opts.Instance.Name,
		Config:                  config,
		Files:                   files,
		ServiceInspectionTopic:  serviceInspectionTopic,
	}, supportsDynamicReconfiguration(opts.Type))
	if err != nil {
		return nil, err
	}

	serviceValues, err := serviceValuesForResource(opts.Instance, opts.Type, level, grpcPort, opts.AppName)
	if err != nil {
		return nil, fmt.Errorf("unable to generate service values for resource instance %q: %w", opts.Instance.GetName(), err)
	}
	httpValues := httpValuesForResource(opts.Instance, opts.Type, httpPort, httpPathPrefix)

	spec, privateRegistryKeys, err := podSpecForResource(opts.Instance, opts.Type, opts.ClusterParams, resourceK8sPrefix, level)
	if err != nil {
		return nil, fmt.Errorf("unable to create pod spec: %w", err)
	}
	setEnvironmentVariables(spec, opts.Instance, httpPort, httpPathPrefix)
	if err := maybeAddInitContainerForDataFiles(spec, opts.Instance, resourceK8sPrefix, opts.InitDataFilesImage, opts.InitDataFilesCASAddress, opts.InitDataFilesEnableCASPeerMetadata); err != nil {
		return nil, fmt.Errorf("unable to add init container for data files: %w", err)
	}
	specMarshalled, err := kubeyaml.Marshal(spec)
	if err != nil {
		return nil, fmt.Errorf("unable to marshal pod spec: %w", err)
	}

	return map[string]any{
		"name":                      sanitizeResourceInstanceName(opts.Instance.GetName()),
		"instance_name":             opts.Instance.GetName(),
		"context_id":                contextID,
		"service":                   serviceValues,
		"http":                      httpValues,
		"runtime_context_pb_base64": contextBase64,
		"spec":                      string(specMarshalled),
		"private_registry_keys":     privateRegistryKeys,
	}, nil
}

// ClusterParams contains information about the cluster that is needed for rendering.
type ClusterParams struct {
	// HasGpu indicates whether the cluster has at least one GPU.
	HasGpu bool
	// NumSkillPods specifies the number of pods into which Skills are grouped.
	// Must be > 0.
	NumSkillPods int
}

// InitDataFilesParams contain the information required for the init containers that prepare
// resource instances' data files.
type InitDataFilesParams struct {
	// Image is a fully qualified container image reference for the data files downloader.
	Image string
	// CASAddress is the address of the content-addressable storage (CAS) service or its onprem proxy.
	// IMPORTANT: This address MUST include the namespace where the CAS service is running, as it is
	// passed to pods running in other namespaces.
	CASAddress string

	// EnableCASPeerMetadata enables CAS peer metadata for the init container.
	EnableCASPeerMetadata bool
}

// ResourceChartOptions are the inputs to generate a resource chart
type ResourceChartOptions struct {
	Instances []*ripb.ResourceInstance
	Types     map[string]*rtrpb.ResourceTypeRuntime
	Parent    string
	Simulated bool
	ClusterParams
	InitDataFilesParams
}

// getServicePorts gets ports to assign to the given resource instances.
func getServicePorts(ris []*ripb.ResourceInstance, rtrs map[string]*rtrpb.ResourceTypeRuntime, simulated bool) (map[string]*servicePorts, error) {
	portOffset := int32(0)

	ports := make(map[string]*servicePorts)
	for _, ri := range ris {
		rtr, ok := rtrs[ri.GetTypeIdVersion()]
		if !ok {
			return nil, fmt.Errorf("no type %q for service instance %q: %w", ri.GetTypeIdVersion(), ri.GetName(), errMissingTypeInfo)
		}

		var grpcPort, httpPort int32
		if rtr.GetServiceDef() != nil {
			level := rtcpb.RuntimeContext_REALITY
			if simulated {
				level = rtcpb.RuntimeContext_PHYSICS_SIM
			}

			rs := getSpecWithFallback(rtr, level)

			if requiresGRPCPort(rtr) {
				if rs.GetHostNetwork() {
					grpcPort = resourcesBasePort + portOffset
					portOffset++
				} else {
					grpcPort = defaultNonHostPort
				}
			}
			if rtr.ServiceDef.GetProvidesHttpEndpoint() || rtr.ServiceDef.GetHttpConfig() != nil {
				if rs.GetHostNetwork() {
					httpPort = resourcesBasePort + portOffset
					portOffset++
				} else {
					httpPort = defaultNonHostHTTPPort
				}
			}
		}

		ports[ri.GetName()] = &servicePorts{
			GRPC: grpcPort,
			HTTP: httpPort,
		}
	}

	if portOffset > maxAllowedResourceServices {
		return nil, errTooManyServicesOnHostNetwork
	}

	return ports, nil
}

// resourceChartValues defines resource-specific variables which brings up
// services needed for the current set of resources.
func resourceChartValues(opts ResourceChartOptions) (crcv1alpha1.ConfigValues, error) {
	var instances []crcv1alpha1.ConfigValues

	servicePorts, err := getServicePorts(opts.Instances, opts.Types, opts.Simulated)
	if err != nil {
		return nil, fmt.Errorf("failed to assign service ports: %w", err)
	}

	for _, ri := range opts.Instances {
		rt, ok := opts.Types[ri.GetTypeIdVersion()]
		if !ok {
			return nil, fmt.Errorf("problem with resource instance %q, of type %q: %w", ri.GetName(), ri.GetTypeIdVersion(), errMissingTypeInfo)
		}
		if rt.ServiceDef == nil {
			continue
		}

		servicePorts, ok := servicePorts[ri.GetName()]
		if !ok {
			return nil, fmt.Errorf("no service ports for resource instance %q", ri.GetName())
		}

		value, err := resourceInstanceChartValue(
			&resourceInstanceChartValueOpts{
				Instance:                           ri,
				Type:                               rt,
				AppName:                            opts.Parent,
				Simulated:                          opts.Simulated,
				ServicePorts:                       servicePorts,
				InitDataFilesImage:                 opts.InitDataFilesParams.Image,
				InitDataFilesCASAddress:            opts.InitDataFilesParams.CASAddress,
				InitDataFilesEnableCASPeerMetadata: opts.InitDataFilesParams.EnableCASPeerMetadata,
				ClusterParams:                      opts.ClusterParams,
			})
		if err != nil {
			return nil, fmt.Errorf("problem with %q: %w: %v", ri.GetName(), errUnableToAddResource, err)
		}

		instances = append(instances, value)
	}

	values := make(crcv1alpha1.ConfigValues)
	if instances != nil {
		values["resource_instances"] = instances
	}
	return values, nil
}

// validatePOSIXCapability validates a POSIX capability user string.
func validatePOSIXCapability(input string) error {
	if !validPOSIXCapabilityRegex.MatchString(input) {
		return fmt.Errorf("invalid POSIX capability: %q", input)
	}

	return nil
}

func validateK8sResourceName(input ...string) error {
	for _, s := range input {
		if _, ok := allowedK8sResourceNames[s]; !ok {
			matchFound := false
			for _, pat := range allowedK8sResourceNamesRegex {
				if pat.MatchString(s) {
					matchFound = true
					break
				}
			}
			if !matchFound {
				return fmt.Errorf("invalid Kubernetes resource name: %v", s)
			}
		}
	}
	return nil
}

//go:embed skills.yaml
var skillChartTemplate []byte

//go:embed resources.yaml
var resourceChartTemplate []byte

// resourceChart creates a chart assignment from the provided options.
func resourceChart(opts ResourceChartOptions) (*crcv1alpha1.ChartAssignment, error) {
	inline, err := basicInlineChart(resourceChartName, resourceChartVersion, resourceChartTemplateFilename, resourceChartTemplate)
	if err != nil {
		return nil, errors.Wrap(err, "basicInlineChart")
	}
	values, err := resourceChartValues(opts)
	if err != nil {
		return nil, errors.Wrap(err, "resourceChartValues")
	}

	return &crcv1alpha1.ChartAssignment{
		TypeMeta: metav1.TypeMeta{
			Kind:       "ChartAssignment",
			APIVersion: "apps.cloudrobotics.com/v1alpha1",
		},
		ObjectMeta: metav1.ObjectMeta{
			Name: resourceChartName,
			Labels: map[string]string{
				"exclusive":                    "false",
				"app":                          resourceChartName,
				chartassignment.PartOfAppLabel: opts.Parent,
			},
			Annotations: map[string]string{
				"version": resourceChartVersion,
			},
		},
		Spec: crcv1alpha1.ChartAssignmentSpec{
			Chart: crcv1alpha1.AssignedChart{
				Inline: inline,
				Values: values,
			},
			NamespaceName: ResourceNamespace,
		},
	}, nil
}

// SkillDeploymentRuntime represents a skill to be installed as part of a skill chart.
type SkillDeploymentRuntime struct {
	DeploymentData     *sddpb.SkillDeploymentData
	InstallationOrigin iopb.InstallationOrigin
}

// skillPodIndex returns the index in [0, numPods) of the pod group to which a Skill with the given
// ID should be assigned.
//
// Using SHA-256 provides a strict avalanche effect, guaranteeing that Skills are uniformly
// distributed across the pods even when their IDs share long common prefixes (such as
// "ai.intrinsic."). It also ensures that a Skill's pod assignment is deterministic and independent
// of other Skills in the Solution, so adding or removing a Skill only affects its assigned pod.
func skillPodIndex(id string, numPods int) int {
	hash := sha256.Sum256([]byte(id))
	// SHA-256 produces 32 bytes (256 bits). We take the first 8 bytes (64 bits) to decode as a uint64
	// using BigEndian, which provides cross-platform determinism regardless of host architecture and
	// sufficient entropy for uniform modulo distribution across pods without measurable bias.
	return int(binary.BigEndian.Uint64(hash[:8]) % uint64(numPods))
}

// skillChartValues generates the values for the skills.yaml template given a
// list of deployment data for skills.
func skillChartValues(opts SkillChartOptions) (crcv1alpha1.ConfigValues, error) {
	if opts.NumSkillPods <= 0 {
		return nil, fmt.Errorf("NumSkillPods must be > 0, got %d", opts.NumSkillPods)
	}
	numPods := opts.NumSkillPods

	// Group Skills into pods.
	skillGroups := make([]map[string]*SkillDeploymentRuntime, numPods)
	for _, skill := range opts.Skills {
		if image := skill.DeploymentData.GetImage(); image == nil {
			// We only generate a chart when the Skill deployment data contains an image. Otherwise it is
			// a PBT, which just consists of proto data.
			continue
		} else if err := validate.Image(image); err != nil {
			return nil, fmt.Errorf("%w: %v", errInvalidSkillImage, err)
		}

		parts, err := idutils.NewIDVersionParts(skill.DeploymentData.GetIdVersion())
		if err != nil {
			return nil, fmt.Errorf("%w: %v", errInvalidSkillData, err)
		}

		idx := skillPodIndex(parts.ID(), numPods)
		if skillGroups[idx] == nil {
			skillGroups[idx] = make(map[string]*SkillDeploymentRuntime)
		}
		containerName, err := SkillContainerName(parts.ID())
		if err != nil {
			return nil, fmt.Errorf("%w: %v", errInvalidSkillData, err)
		}
		skillGroups[idx][containerName] = skill
	}

	var pods []crcv1alpha1.ConfigValues
	var skills []crcv1alpha1.ConfigValues

	for groupIdx, skillGroup := range skillGroups {
		if len(skillGroup) == 0 {
			continue
		}
		podName := fmt.Sprintf("skill-group-%02d", groupIdx)

		// Sort Skills within each pod group by name, so port assignments are deterministic.
		skillResourceNames := maps.Keys(skillGroup)
		slices.Sort(skillResourceNames)

		var containers []crcv1alpha1.ConfigValues
		var imagePullSecrets []string

		for skillIdx, skillResourceName := range skillResourceNames {
			skill := skillGroup[skillResourceName]

			id, err := idutils.RemoveVersionFrom(skill.DeploymentData.GetIdVersion())
			if err != nil {
				return nil, fmt.Errorf("%w: %v", errInvalidSkillData, err)
			}

			privateRegistryKey, err := buildDockerCfgBase64FromImage(skill.DeploymentData.GetImage())
			if err != nil {
				return nil, fmt.Errorf("%w: invalid private auth: %v", errInvalidSkillImage, err)
			}
			if privateRegistryKey != "" {
				imagePullSecrets = append(imagePullSecrets, skillResourceName)
			}

			containerPort := 8003 + skillIdx
			installationOrigin := iopb.InstallationOrigin_name[int32(skill.InstallationOrigin)]
			containers = append(containers, map[string]any{
				"asset":               id,
				"image_ref":           fmt.Sprintf("%s%s", skill.DeploymentData.GetImage().GetName(), skill.DeploymentData.GetImage().GetTag()),
				"installation_origin": installationOrigin,
				"metrics_port":        9101 + skillIdx,
				"name":                skillResourceName,
				"port":                containerPort,
				"registry":            skill.DeploymentData.GetImage().GetRegistry(),
				"skill_id_version":    skill.DeploymentData.GetIdVersion(),
			})
			skills = append(skills, map[string]any{
				"asset":                id,
				"installation_origin":  installationOrigin,
				"name":                 skillResourceName,
				"pod_name":             podName,
				"port":                 containerPort,
				"private_registry_key": privateRegistryKey,
				"skill_id_version":     skill.DeploymentData.GetIdVersion(),
			})
		}

		pod := map[string]any{
			"name":       podName,
			"containers": containers,
		}
		if len(imagePullSecrets) > 0 {
			pod["image_pull_secrets"] = imagePullSecrets
		}
		pods = append(pods, pod)
	}

	values := make(crcv1alpha1.ConfigValues)
	if len(pods) > 0 {
		values["pods"] = pods
	}
	if len(skills) > 0 {
		values["skills"] = skills
	}
	return values, nil
}

// supportsDynamicReconfiguration returns whether the specified Asset supports
// DynamicReconfiguration in a compatible way with the current runtime.
func supportsDynamicReconfiguration(rtr *rtrpb.ResourceTypeRuntime) bool {
	if rtr.GetServiceDef().GetSupportsDynamicReconfiguration() && supportedDynamicReconfigurationVersions[drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION] {
		return true
	}

	if drc := rtr.GetServiceDef().GetDynamicReconfigurationConfig(); drc != nil {
		for _, version := range drc.GetServiceVersions() {
			if supportedDynamicReconfigurationVersions[version] {
				return true
			}
		}
	}

	return false
}

// supportsServiceState returns whether the specified Asset supports ServiceState in a compatible
// way with the current runtime.
func supportsServiceState(rtr *rtrpb.ResourceTypeRuntime) bool {
	if rtr.GetServiceDef().GetSupportsServiceState() && supportedServiceStateVersions[sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE] {
		return true
	}

	if ssc := rtr.GetServiceDef().GetServiceStateConfig(); ssc != nil {
		for _, version := range ssc.GetServiceVersions() {
			if supportedServiceStateVersions[version] {
				return true
			}
		}
	}

	return false
}

func requiresGRPCPort(rtr *rtrpb.ResourceTypeRuntime) bool {
	sd := rtr.GetServiceDef()

	// We assume that if an optional service config is defined, the Service intends to support that
	// optional service.
	return len(sd.GetServiceProtoPrefixes()) != 0 ||
		sd.GetDynamicReconfigurationConfig() != nil || sd.GetSupportsDynamicReconfiguration() ||
		sd.GetServiceStateConfig() != nil || sd.GetSupportsServiceState()
}

// basicInlineChart creates a base64 encoded chart assignment that
// can be inlined in AssignedChart.  It does not specify any values
func basicInlineChart(name, version, filename string, content []byte) (string, error) {
	var cd bytes.Buffer
	cb, err := chartbuilder.New(&cd, name, version)
	if err != nil {
		return "", errors.Wrap(err, "chart builder")
	}

	if err := cb.AddFileFromReader("templates", filename, bytes.NewReader(content)); err != nil {
		return "", errors.Wrap(err, "template")
	}

	if err := cb.AddValuesYaml(map[string]any{}); err != nil {
		return "", errors.Wrap(err, "values yaml")
	}
	cb.Build()

	return base64.StdEncoding.EncodeToString(cd.Bytes()), nil
}

// skillChart creates a chart assignment from the provided options.
func skillChart(opts SkillChartOptions) (*crcv1alpha1.ChartAssignment, error) {
	inline, err := basicInlineChart(SkillChartName, skillChartVersion, skillChartTemplateFilename, skillChartTemplate)
	if err != nil {
		return nil, errors.Wrap(err, "basicInlineChart")
	}
	values, err := skillChartValues(opts)
	if err != nil {
		return nil, errors.Wrap(err, "SkillChartValues")
	}

	return &crcv1alpha1.ChartAssignment{
		TypeMeta: metav1.TypeMeta{
			Kind:       "ChartAssignment",
			APIVersion: "apps.cloudrobotics.com/v1alpha1",
		},
		ObjectMeta: metav1.ObjectMeta{
			Name: SkillChartName,
			Labels: map[string]string{
				"exclusive":                    "false",
				"app":                          SkillChartName,
				chartassignment.PartOfAppLabel: opts.Parent,
				"ai.intrinsic/addon-type":      "skill",
			},
			Annotations: map[string]string{
				"version": skillChartVersion,
			},
		},
		Spec: crcv1alpha1.ChartAssignmentSpec{
			Chart: crcv1alpha1.AssignedChart{
				Inline: inline,
				Values: values,
			},
			NamespaceName: skillNamespace,
		},
	}, nil
}

// SkillInstallationOrigin returns the installation origin of a skill from the given k8s ConfigMap.
// Returns an unspecified installation origin if the relevant data entry is missing or invalid. This
// function safely handles a nil config map and data.
func SkillInstallationOrigin(cm *kubev1.ConfigMap) iopb.InstallationOrigin {
	if cm == nil || cm.Data == nil || cm.Data[SkillInstallationOriginKey] == "" {
		return iopb.InstallationOrigin_INSTALLATION_ORIGIN_UNSPECIFIED
	}
	return iopb.InstallationOrigin(iopb.InstallationOrigin_value[cm.Data[SkillInstallationOriginKey]])
}

// ResourceTypeRuntimeToSkillDeploymentData converts a skill stored in a
// runtime message into a skill deployment, if it applicable.  It returns nil
// otherwise.  It assumes the runtime message has been validated already.
func ResourceTypeRuntimeToSkillDeploymentData(rt *rtrpb.ResourceTypeRuntime) *SkillDeploymentRuntime {
	image := rt.GetSkill().GetAssets().GetImage()
	if image == nil {
		return nil
	}
	return &SkillDeploymentRuntime{
		DeploymentData: &sddpb.SkillDeploymentData{
			IdVersion: idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion()),
			DeploymentType: &sddpb.SkillDeploymentData_Image{
				Image: image,
			},
		},
		InstallationOrigin: rt.GetInstallationOrigin(),
	}
}
