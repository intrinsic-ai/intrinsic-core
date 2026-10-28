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

// Package chartapply contains utilities for applying charts.
package chartapply

import (
	"bytes"
	"compress/gzip"
	"context"
	"crypto/sha256"
	"fmt"
	"io/fs"
	"slices"
	"strconv"
	"strings"
	"sync"

	"intrinsic/kubernetes/config/crcconfig"
	"intrinsic/kubernetes/values"
	"intrinsic/production/imagepublisher"
	"intrinsic/tools/inctl/util/gcpauth"
	"intrinsic/tools/internal/k8sclientutil"

	"cloud.google.com/go/storage"
	"dario.cat/mergo"
	log "github.com/golang/glog"
	"github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	crversioned "github.com/googlecloudrobotics/core/src/go/pkg/client/versioned"
	cloudbuild "google.golang.org/api/cloudbuild/v1"
	"google.golang.org/api/option"
	"helm.sh/helm/v3/pkg/action"
	helmchart "helm.sh/helm/v3/pkg/chart"
	helmchartloader "helm.sh/helm/v3/pkg/chart/loader"
	"helm.sh/helm/v3/pkg/cli"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/apimachinery/pkg/api/errors"
	"k8s.io/apimachinery/pkg/api/meta"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/apis/meta/v1/unstructured"
	"k8s.io/apimachinery/pkg/runtime/schema"
	"k8s.io/apimachinery/pkg/types"
	"k8s.io/apimachinery/pkg/util/yaml"
	"k8s.io/client-go/discovery"
	"k8s.io/client-go/discovery/cached/memory"
	"k8s.io/client-go/dynamic"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/restmapper"
	"k8s.io/client-go/tools/clientcmd"
	clientcmdapi "k8s.io/client-go/tools/clientcmd/api"
)

const (
	imageRegistry       = "us-central1-docker.pkg.dev/intrinsic-artifacts-dev/cloud-chart-images"
	helmDriver          = "memory" // This seems to be the only case imported into google3
	resourceTrackingKey = "argocd.argoproj.io/tracking-id"
)

var protectedNamespaces = []string{"argo", "argocd", "default", "kube-node-lease", "kube-public", "kube-system"}

// SharedGatewayNamespaces defines the shared authentication gateway namespaces
// (e.g., auth-proxy2, token-vendor, zenoh-auth-proxy) where individual charts may deploy out-of-namespace
// Gateway API resources (such as ReferenceGrant).
// Restricting out-of-namespace discovery to these known shared gateway namespaces instead of
// issuing cluster-wide sweeps across all namespaces (metav1.NamespaceAll) prevents multiplying
// API server List queries across ~250+ GVRs for unrelated cluster namespaces (kube-system, argo, etc.).
var SharedGatewayNamespaces = []string{"app-auth-proxy2", "app-token-vendor", "app-zenoh-auth-proxy"}

// targetNamespacesForDelete returns the deduplicated list of namespaces to query when deleting a
// chart's tracked resources: SharedGatewayNamespaces and the primary chart namespace.
func targetNamespacesForDelete(primaryNamespace string) []string {
	if slices.Contains(SharedGatewayNamespaces, primaryNamespace) {
		return SharedGatewayNamespaces
	}
	return append(SharedGatewayNamespaces, primaryNamespace)
}

// allowedDiscoveryFailureResources defines resources (e.g. events) that often fail generic dynamic client List queries due to RBAC or API server behaviors across scopes.
var allowedDiscoveryFailureResources = []string{"events", "events.events.k8s.io"}

// allowedDiscoveryFailureGroups defines API groups (such as metrics groups) that frequently return errors on generic List queries when metrics servers are unconfigured or restricted.
var allowedDiscoveryFailureGroups = []string{"metrics.k8s.io", "custom.metrics.k8s.io", "external.metrics.k8s.io"}

// isAllowedDiscoveryFailure checks if a GroupVersionResource is in our known allowlist of resources or groups that may safely fail generic dynamic discovery listing without aborting the deletion process.
func isAllowedDiscoveryFailure(gvr schema.GroupVersionResource) bool {
	if slices.Contains(allowedDiscoveryFailureResources, gvr.Resource) {
		return true
	}
	return slices.Contains(allowedDiscoveryFailureGroups, gvr.Group)
}

type ClusterConfig struct {
	AppsCS            *crversioned.Clientset
	KubeCS            *kubernetes.Clientset
	DynamicClient     dynamic.Interface
	DiscoveryClient   discovery.DiscoveryInterface
	ClusterName       string
	UniqueClusterName string
	Project           string
	Location          string
	CRCConfig         crcconfig.Config
}

type resourceInfo struct {
	gvr       schema.GroupVersionResource
	namespace string
	name      string
}

// Need to wrap this with a function that takes variadic interface instead of any
func loggingFunction(format string, v ...interface{}) {
	log.InfoContextf(context.Background(), format, v...)
}

// CreateGCRClient creates a GCR client for pushing images.
func CreateGCRClient(ctx context.Context, opts []option.ClientOption) (imagepublisher.Publisher, error) {
	gcsClient, err := storage.NewClient(ctx, opts...)
	if err != nil {
		return nil, err
	}
	auth, err := gcpauth.AuthOption(ctx)
	if err != nil {
		return nil, fmt.Errorf("authentication for GCP services: %w", err)
	}
	cb, err := cloudbuild.NewService(ctx, opts...)
	if err != nil {
		return nil, err
	}
	gcrClient := imagepublisher.NewGCRPublisher(auth, gcsClient, cb)
	return gcrClient, nil
}

// generateRuntimeValues merges values from various sources into a single map, and validates that
// any custom value overrides passed in match ones already in values.yaml.
func generateRuntimeValues(ctx context.Context, clusterConfig *ClusterConfig, chart *helmchart.Chart, commaSeparatedValues string) (v1alpha1.ConfigValues, error) {
	runtimeValues, err := values.BuildValues(clusterConfig.Project, clusterConfig.UniqueClusterName, clusterConfig.Location, commaSeparatedValues, clusterConfig.CRCConfig)
	if err != nil {
		return nil, err
	}

	if err := values.ValidateFlagValuesChart(chart, runtimeValues); err != nil {
		return nil, err
	}
	return runtimeValues, nil
}

// LoadChart loads a chart from a tarball.
func LoadChart(ctx context.Context, runfilesFS fs.FS, chartPath string) (*helmchart.Chart, error) {
	raw, err := runfilesFS.Open(chartPath)
	if err != nil {
		return nil, err
	}
	defer raw.Close()

	c, err := helmchartloader.LoadArchive(raw)
	if err != nil {
		if err == gzip.ErrHeader {
			return nil, fmt.Errorf("file '%s' does not appear to be a valid chart file (details: %s)", chartPath, err)
		}
	}
	return c, err
}

// PushImages pushes images to the target registry and returns the hybrid build images.
func PushImages(ctx context.Context, runfilesFS fs.FS, images []string, publisher imagepublisher.Publisher, flagDryRun bool) ([]*imagepublisher.ImageInfo, error) {
	if flagDryRun {
		fmt.Printf("Dry run, not pushing images to %s\n", imageRegistry)
		return nil, nil
	}
	fmt.Printf("Pushing images to %s\n", imageRegistry)
	// empty lfs paths for now
	var lfsPaths []string
	// New images are specifically hybrid build images
	newImages, err := imagepublisher.Publish(ctx, publisher, imageRegistry, images, lfsPaths, runfilesFS)
	if err != nil {
		return nil, fmt.Errorf("upload images: %w", err)
	}
	return newImages, nil
}

func GetClusterConfig(ctx context.Context, opts []option.ClientOption, clusterName string) (*ClusterConfig, error) {
	restConfig, err := k8sclientutil.KubernetesConfig(ctx, clusterName)
	if err != nil {
		return nil, fmt.Errorf("get rest config: %w", err)
	}
	return buildClusterConfig(ctx, opts, restConfig, clusterName, "", "")
}

// GetClusterConfigFromFlags builds a ClusterConfig from project, cluster, and inCloud flags.
// It does not require the cluster context to be already configured in local kubeconfig.
func GetClusterConfigFromFlags(ctx context.Context, opts []option.ClientOption, project string, cluster string, inCloud bool) (*ClusterConfig, error) {
	var apiConfig *clientcmdapi.Config
	var err error
	var location string
	var contextName string

	if inCloud {
		c, err := k8sclientutil.FindClusterInCloud(ctx, project, cluster)
		if err != nil {
			return nil, fmt.Errorf("find cluster in cloud: %w", err)
		}
		location = c.Location
		contextName = fmt.Sprintf("gke_%s_%s_%s", project, location, cluster)
		apiConfig, err = k8sclientutil.CloudConfig(ctx, project, cluster)
		if err != nil {
			return nil, fmt.Errorf("get cloud config: %w", err)
		}
	} else {
		contextName = cluster
		apiConfig = k8sclientutil.OnPremConfig(project, cluster)
	}

	clientConfig := clientcmd.NewDefaultClientConfig(*apiConfig, &clientcmd.ConfigOverrides{})
	restConfig, err := clientConfig.ClientConfig()
	if err != nil {
		return nil, fmt.Errorf("get rest config: %w", err)
	}

	return buildClusterConfig(ctx, opts, restConfig, contextName, project, location)
}

// buildClusterConfig is a helper that constructs all clientsets and retrieves missing config parameters
// (project, location, unique cluster name, crc config) to populate the ClusterConfig.
func buildClusterConfig(ctx context.Context, opts []option.ClientOption, restConfig *rest.Config, contextName string, project string, location string) (*ClusterConfig, error) {
	appsCS, err := crversioned.NewForConfig(restConfig)
	if err != nil {
		return nil, fmt.Errorf("get apps clientset: %w", err)
	}
	kubeCS, err := kubernetes.NewForConfig(restConfig)
	if err != nil {
		return nil, fmt.Errorf("get kube clientset: %w", err)
	}
	dynamicClient, err := dynamic.NewForConfig(restConfig)
	if err != nil {
		return nil, fmt.Errorf("get dynamic client: %w", err)
	}
	discoveryClient, err := discovery.NewDiscoveryClientForConfig(restConfig)
	if err != nil {
		return nil, fmt.Errorf("get discovery client: %w", err)
	}

	if project == "" {
		project, err = k8sclientutil.GCPProject(ctx, appsCS, contextName)
		if err != nil {
			return nil, fmt.Errorf("get GCP project: %w", err)
		}
	}

	if location == "" {
		location, err = k8sclientutil.ClusterLocation(ctx, contextName)
		if err != nil {
			return nil, fmt.Errorf("get cluster location: %w", err)
		}
	}

	uniqueClusterName, err := k8sclientutil.UniqueClusterName(ctx, appsCS, contextName)
	if err != nil {
		return nil, fmt.Errorf("get unique cluster name: %w", err)
	}

	crcConfig, err := crcconfig.New(ctx, project, opts...)
	if err != nil {
		return nil, fmt.Errorf("get cloud robotics config: %w", err)
	}

	return &ClusterConfig{
		AppsCS:            appsCS,
		KubeCS:            kubeCS,
		DynamicClient:     dynamicClient,
		DiscoveryClient:   discoveryClient,
		ClusterName:       contextName,
		UniqueClusterName: uniqueClusterName,
		Project:           project,
		Location:          location,
		CRCConfig:         crcConfig,
	}, nil
}

func finalizeChart(ctx context.Context, chart *helmchart.Chart, images []*imagepublisher.ImageInfo, runtimeValues v1alpha1.ConfigValues, project string, featureOptions string) error {
	fmt.Printf("Finalizing chart: %s\n", chart.Name())
	// Set the target registry for the chart images
	chart.Values["registry"] = imageRegistry
	// Set the feature options overrides for the chart
	values.SetFeatureOptions(project, featureOptions, chart.Values)
	// Set the hybrid build images values for the chart
	for _, image := range images {
		imagesMap, ok := chart.Values["images"].(map[string]any)
		if !ok || imagesMap == nil {
			imagesMap = make(map[string]any)
			chart.Values["images"] = imagesMap
		}
		imagesMap[image.ImageAbstractionName] = image.ImageReference()
	}
	// Convert type in order to merge with same type
	var overrideValues map[string]interface{} = runtimeValues

	// Merge the runtime values with the chart values
	if err := mergo.Merge(&chart.Values, overrideValues, mergo.WithOverride); err != nil {
		return fmt.Errorf("merging values.yaml with runtime values: %w", err)
	}
	return nil
}

func generateManifest(ctx context.Context, chart *helmchart.Chart, overrideNamespace string) (string, error) {
	// Generate a random release name for the chart. Not semver and thus not compatible with ArgoCD.
	releaseName := "placeholder" // this is not used in dry run mode
	namespace := "app-" + chart.Name()
	if overrideNamespace != "" {
		namespace = overrideNamespace
	}
	settings := cli.New()
	actionConfig := new(action.Configuration)
	if err := actionConfig.Init(
		settings.RESTClientGetter(),
		namespace,
		helmDriver,
		loggingFunction); err != nil {
		return "", fmt.Errorf("failed to initialize action config: %w", err)
	}

	installClient := action.NewInstall(actionConfig)

	installClient.ReleaseName = releaseName
	installClient.Namespace = namespace
	installClient.Wait = true
	installClient.ClientOnly = true
	installClient.DryRun = true
	installClient.IncludeCRDs = true

	// TODO: Add values instead of nil
	release, err := installClient.Run(chart, nil)
	if err != nil {
		return "", fmt.Errorf("failed to run install: %w", err)
	}

	return release.Manifest, nil
}

// buildApplicationName builds a unique application name for the chart when deployed to the project
// cluster, matching ArgoCD's logic.
func buildApplicationName(clusterConfig *ClusterConfig, chartName string) (string, error) {
	// TODO(b/455689093): Pipe the cluster hash through the ArgoCD cluster management ConfigMap instead of deriving it here.
	// This is the same logic used in the deployment repo to generate hash ids for clusters.
	k8sClusterName, err := k8sclientutil.ClusterName(clusterConfig.ClusterName)
	if err != nil {
		return "", fmt.Errorf("get kubernetes cluster name: %w", err)
	}
	s := fmt.Sprintf("%s_%s", clusterConfig.Project, k8sClusterName)
	hashBytes := sha256.Sum256([]byte(s))
	hashString := fmt.Sprintf("%x", hashBytes)
	hashID := hashString[:10]

	// ApplicationSets should follow format {{name}}-{{.metadata.labels.hashId}} in their templates
	return fmt.Sprintf("%s-%s", chartName, hashID), nil
}

func buildResourceTrackingValue(applicationName string, resource unstructured.Unstructured) string {
	gvk := resource.GroupVersionKind()
	return fmt.Sprintf("%s:%s/%s:%s/%s", applicationName, gvk.Group, gvk.Kind, resource.GetNamespace(), resource.GetName())
}

// applyManifest takes a YAML manifest string and applies it to a Kubernetes cluster
func applyManifest(ctx context.Context, manifest string, chartName string, clusterConfig *ClusterConfig, flagDryRun bool, overrideNamespace string) error {
	dynamicClient := clusterConfig.DynamicClient
	kubeClient := clusterConfig.KubeCS
	discoveryClient := clusterConfig.DiscoveryClient

	mapper := restmapper.NewDeferredDiscoveryRESTMapper(memory.NewMemCacheClient(discoveryClient))

	// The manifest can contain multiple YAML documents, so we split them.
	decoder := yaml.NewYAMLOrJSONDecoder(bytes.NewReader([]byte(manifest)), 4096)

	// Make the default namespace
	defaultNamespace := "app-" + chartName
	if overrideNamespace != "" {
		defaultNamespace = overrideNamespace
	}
	checkedNamespaces := make(map[string]bool)

	applicationName, err := buildApplicationName(clusterConfig, chartName)
	if err != nil {
		return fmt.Errorf("failed to build application name: %w", err)
	}

	// Apply each document to the cluster
	for {
		// Unmarshal the next document into a generic Unstructured object
		var obj unstructured.Unstructured
		if err := decoder.Decode(&obj); err != nil {
			if err.Error() == "EOF" {
				break // End of manifest
			}
			return fmt.Errorf("failed to decode YAML: %w", err)
		}

		// Skip empty objects
		if obj.Object == nil {
			continue
		}

		// Map GVK (GroupVersionKind) to GVR (GroupVersionResource)
		gvk := obj.GroupVersionKind()
		// This may write an error to stdout about not being able to get resource list for external.metrics.k8s.io/v1beta1, but we can ignore it.
		mapping, err := mapper.RESTMapping(gvk.GroupKind(), gvk.Version)
		if err != nil {
			return fmt.Errorf("failed to get REST mapping for %s: %w", gvk, err)
		}

		// Set the default namespace if not explicitly specified for namespaced resources
		if mapping.Scope.Name() == meta.RESTScopeNameNamespace && obj.GetNamespace() == "" {
			obj.SetNamespace(defaultNamespace)
		}

		// Add annotation to the object
		annotations := obj.GetAnnotations()
		if annotations == nil {
			annotations = make(map[string]string)
		}
		annotations[resourceTrackingKey] = buildResourceTrackingValue(applicationName, obj)
		obj.SetAnnotations(annotations)

		// Create the namespace if it doesn't exist
		if mapping.Scope.Name() == meta.RESTScopeNameNamespace {
			targetNamespace := obj.GetNamespace()
			if targetNamespace != "" && !checkedNamespaces[targetNamespace] {
				// Check if the namespace exists
				_, err := kubeClient.CoreV1().Namespaces().Get(ctx, targetNamespace, metav1.GetOptions{})
				if err != nil && errors.IsNotFound(err) {
					// Create the namespace if it doesn't exist
					// TODO(b/454090421): Migrate console output (fmt.Printf) to structured logging across chartapply.
					fmt.Printf("Namespace '%s' not found.\n", targetNamespace)
					nsSpec := &corev1.Namespace{ObjectMeta: metav1.ObjectMeta{Name: targetNamespace}}
					if flagDryRun {
						fmt.Printf("Dry run, not creating namespace '%s'.\n", targetNamespace)
					} else {
						fmt.Printf("Creating namespace '%s'.\n", targetNamespace)
						if _, createErr := kubeClient.CoreV1().Namespaces().Create(ctx, nsSpec, metav1.CreateOptions{}); createErr != nil {
							return fmt.Errorf("failed to create namespace %s: %w", targetNamespace, createErr)
						}
					}
				} else if err != nil {
					return fmt.Errorf("failed to check for namespace %s: %w", targetNamespace, err)
				}
				checkedNamespaces[targetNamespace] = true
			}
		}

		// Get the resource interface for the REST mapping
		var dr dynamic.ResourceInterface
		if mapping.Scope.Name() == meta.RESTScopeNameNamespace {
			dr = dynamicClient.Resource(mapping.Resource).Namespace(obj.GetNamespace())
		} else {
			dr = dynamicClient.Resource(mapping.Resource)
		}

		// Apply the resource
		data, err := obj.MarshalJSON()
		if err != nil {
			return fmt.Errorf("failed to marshal object to JSON: %w", err)
		}

		if flagDryRun {
			fmt.Printf("Dry run, not applying %s: %s\n", gvk.Kind, obj.GetName())
		} else {
			fmt.Printf("Applying %s: %s\n", gvk.Kind, obj.GetName())
			forcePatch := true // Must force patch to override ArgoCD control of resources
			_, err = dr.Patch(ctx, obj.GetName(), types.ApplyPatchType, data, metav1.PatchOptions{
				FieldManager: "helm-template-apply-script",
				Force:        &forcePatch,
			})
			if err != nil {
				return fmt.Errorf("failed to apply resource %s/%s: %w", gvk.Kind, obj.GetName(), err)
			}
		}
	}
	return nil
}

// ApplyChart applies a chart to a cluster.
func ApplyChart(ctx context.Context, chart *helmchart.Chart, newImages []*imagepublisher.ImageInfo, clusterConfig *ClusterConfig, flagValues string, flagFeatureOptions string, flagDryRun bool, overrideNamespace string) error {
	// Build and validate runtime values.
	// Should these merge before or after feature options?
	runtimeValues, err := generateRuntimeValues(ctx, clusterConfig, chart, flagValues)
	if err != nil {
		return fmt.Errorf("generate runtime values: %w", err)
	}

	// Set registry, feature options, and hybrid build images values for the chart
	err = finalizeChart(ctx, chart, newImages, runtimeValues, clusterConfig.Project, flagFeatureOptions)
	if err != nil {
		return fmt.Errorf("unable to finalize chart: %w", err)
	}

	// Inflate the chart now that we've set the values properly.
	manifest, err := generateManifest(ctx, chart, overrideNamespace)
	if err != nil {
		return fmt.Errorf("unable to generate manifest: %w", err)
	}

	// Apply the chart to the cluster
	err = applyManifest(ctx, manifest, chart.Name(), clusterConfig, flagDryRun, overrideNamespace)
	if err != nil {
		return fmt.Errorf("unable to apply manifest: %w", err)
	}
	if flagDryRun {
		fmt.Printf("Dry run, did not apply chart to project %s\n", clusterConfig.Project)
	} else {
		fmt.Printf("Applied chart to project %s\n", clusterConfig.Project)
	}

	return nil
}

// hasAnnotationWithPrefix checks if the unstructured object has an annotation matching the key and starting with any of the given prefixes.
func hasAnnotationWithPrefix(item unstructured.Unstructured, annotationKey string, prefixes []string) bool {
	if len(prefixes) == 0 {
		return false
	}
	annotations := item.GetAnnotations()
	if annotations == nil {
		return false
	}
	resourceTrackingAnnotation, ok := annotations[annotationKey]
	if !ok {
		return false
	}
	for _, prefix := range prefixes {
		if strings.HasPrefix(resourceTrackingAnnotation, prefix) {
			return true
		}
	}
	return false
}

// isDiscoverableAPIResource checks if an APIResource is namespaced, not a subresource, and has list and delete verbs.
func isDiscoverableAPIResource(res metav1.APIResource) bool {
	if !res.Namespaced || strings.Contains(res.Name, "/") {
		return false
	}
	hasList, hasDelete := false, false
	for _, verb := range res.Verbs {
		if verb == "list" {
			hasList = true
		} else if verb == "delete" {
			hasDelete = true
		}
	}
	return hasList && hasDelete
}

// discoverResourcesForGVR lists and filters items matching tracking prefixes for a specific GroupVersionResource across target namespaces.
func discoverResourcesForGVR(ctx context.Context, dynamicClient dynamic.Interface, gvr schema.GroupVersionResource, namespaces []string, prefixes []string, verbose bool) ([]resourceInfo, error) {
	var results []resourceInfo
	for _, namespace := range namespaces {
		items, err := dynamicClient.Resource(gvr).Namespace(namespace).List(ctx, metav1.ListOptions{})
		if err != nil {
			// We log a warning and continue if the resource or group is in our allowlist of known dynamic discovery listing exceptions.
			if isAllowedDiscoveryFailure(gvr) {
				fmt.Printf("Warning: failed to list %s in namespace %s: %v\n", gvr, namespace, err)
				continue
			}
			return nil, fmt.Errorf("failed to list %s in namespace %s: %w", gvr, namespace, err)
		}

		for _, item := range items.Items {
			if len(prefixes) > 0 && !hasAnnotationWithPrefix(item, resourceTrackingKey, prefixes) {
				continue
			}
			if verbose {
				fmt.Printf("Found resource: %s/%s in namespace %s (%s)\n", item.GetKind(), item.GetName(), item.GetNamespace(), gvr.GroupResource())
			}
			ns := item.GetNamespace()
			if ns == "" {
				ns = namespace
			}
			results = append(results, resourceInfo{
				gvr:       gvr,
				namespace: ns,
				name:      item.GetName(),
			})
		}
	}
	return results, nil
}

// discoverResources polls all API groups to find namespaced resources
// that are list-able/delete-able and match the resource tracking prefix.
func discoverResources(ctx context.Context, clusterConfig *ClusterConfig, namespaces []string, resourceTrackingPrefixes []string, verbose bool) ([]resourceInfo, error) {
	// Get all server resource lists
	apiResourceLists, err := clusterConfig.DiscoveryClient.ServerPreferredResources()
	if err != nil {
		// This can happen if an aggregated API server is down
		fmt.Printf("Warning: failed to get all server preferred resources: %v. Continuing...", err)
	}

	var discoveredResources []resourceInfo
	for _, list := range apiResourceLists {
		gv, err := schema.ParseGroupVersion(list.GroupVersion)
		if err != nil {
			fmt.Printf("Warning: skipping invalid group version '%s': %v", list.GroupVersion, err)
			continue
		}

		for _, res := range list.APIResources {
			if !isDiscoverableAPIResource(res) {
				continue
			}
			gvr := gv.WithResource(res.Name)
			gvrResults, err := discoverResourcesForGVR(ctx, clusterConfig.DynamicClient, gvr, namespaces, resourceTrackingPrefixes, verbose)
			if err != nil {
				return nil, err
			}
			discoveredResources = append(discoveredResources, gvrResults...)
		}
	}
	return discoveredResources, nil
}

func deleteResources(ctx context.Context, clusterConfig *ClusterConfig, resources []resourceInfo) error {
	var wg sync.WaitGroup
	var mu sync.Mutex
	var errs []string

	for _, res := range resources {
		wg.Add(1)
		go func(r resourceInfo) {
			defer wg.Done()
			deletePolicy := metav1.DeletePropagationBackground
			fmt.Printf("Deleting %s/%s in namespace %s...\n", r.gvr.Resource, r.name, r.namespace)
			err := clusterConfig.DynamicClient.Resource(r.gvr).Namespace(r.namespace).Delete(ctx, r.name, metav1.DeleteOptions{
				PropagationPolicy: &deletePolicy,
			})
			if err != nil && !errors.IsNotFound(err) {
				fmt.Printf("Error deleting %s/%s in namespace %s: %v\n", r.gvr.Resource, r.name, r.namespace, err)
				mu.Lock()
				errs = append(errs, fmt.Sprintf("%s/%s in namespace %s: %v", r.gvr.Resource, r.name, r.namespace, err))
				mu.Unlock()
			}
		}(res)
	}

	wg.Wait()
	if len(errs) > 0 {
		return fmt.Errorf("failed to delete one or more resources: %s", strings.Join(errs, "; "))
	}
	return nil
}

func deleteNamespace(ctx context.Context, clusterConfig *ClusterConfig, namespaceName string) error {
	fmt.Printf("Deleting namespace '%s'...", namespaceName)

	// All namespaces should start with "app-" so we should never be deleting a protected namespace, but better to check.
	if slices.Contains(protectedNamespaces, namespaceName) {
		return fmt.Errorf("namespace '%s' is protected, not deleting it", namespaceName)
	}

	err := clusterConfig.KubeCS.CoreV1().Namespaces().Delete(ctx, namespaceName, metav1.DeleteOptions{})
	if err != nil {
		if strings.Contains(err.Error(), "not found") {
			fmt.Printf("Namespace '%s' already deleted or not found.\n", namespaceName)
			return nil
		}
		return err
	}
	return nil
}

// removeDefaultResources removes resources that are not specific to a chart and are added by default when creating a new namespace.
func removeDefaultResources(resources []resourceInfo) []resourceInfo {
	var filteredResources []resourceInfo
	for _, res := range resources {
		if res.gvr.Resource == "serviceaccounts" && res.name == "default" {
			continue
		}
		if res.gvr.Resource == "configmaps" && res.name == "kube-root-ca.crt" {
			continue
		}
		if res.gvr.Resource == "events" || res.gvr.Resource == "endpoints" || res.gvr.Resource == "endpointslices" {
			continue
		}
		if res.gvr.Resource == "secrets" && strings.HasPrefix(res.name, "default-token-") {
			continue
		}
		filteredResources = append(filteredResources, res)
	}
	return filteredResources
}

// DeleteChart deletes a chart and corresponding resources from a cluster.
func DeleteChart(ctx context.Context, chartName string, clusterConfig *ClusterConfig, flagDryRun bool, overrideNamespace string) error {
	namespace := "app-" + chartName
	if overrideNamespace != "" {
		namespace = overrideNamespace
	}
	applicationName, err := buildApplicationName(clusterConfig, chartName)
	if err != nil {
		return fmt.Errorf("failed to build application name: %w", err)
	}
	trackingPrefixes := []string{
		applicationName + ":",
		clusterConfig.Project + "-" + chartName + ":",
	}

	// Discover resources across the primary chart namespace and known shared gateway namespaces (SharedGatewayNamespaces) where routes may exist.
	targetNamespaces := targetNamespacesForDelete(namespace)
	resources, err := discoverResources(ctx, clusterConfig, targetNamespaces, trackingPrefixes, true)
	if err != nil {
		return fmt.Errorf("failed to discover resources: %w", err)
	}
	if len(resources) == 0 {
		fmt.Printf("No resources found matching resource tracking prefixes %v. Nothing to delete.\n", trackingPrefixes)
	}
	if flagDryRun {
		fmt.Printf("Dry run, not deleting resources.\n")
		return nil
	}

	// Delete resources matching the selector
	if err := deleteResources(ctx, clusterConfig, resources); err != nil {
		return fmt.Errorf("failed to delete resources: %w", err)
	}
	fmt.Printf("Deleted resource(s).\n")

	// Check if the primary namespace is now empty. Only query the primary chart namespace here!
	resources, err = discoverResources(ctx, clusterConfig, []string{namespace}, nil, false)
	if err != nil {
		return fmt.Errorf("failed to discover resources: %w", err)
	}

	// Ignore default resources.
	resources = removeDefaultResources(resources)

	if len(resources) == 0 {
		fmt.Printf("Namespace %s is now empty, deleting it.\n", namespace)
		err = deleteNamespace(ctx, clusterConfig, namespace)
		if err != nil {
			return fmt.Errorf("failed to delete namespace: %w", err)
		}
		fmt.Printf("Deleted namespace %s.\n", namespace)
	}

	return nil
}

// IsArgoCDManagedCluster checks if the cluster is managed by ArgoCD.
func IsArgoCDManagedCluster(ctx context.Context, clusterConfig *ClusterConfig) (bool, error) {
	kubeClient := clusterConfig.KubeCS

	namespace := "app-cluster-management"
	configMapName := "argocd-management"

	fmt.Printf("Reading ConfigMap '%s' in namespace '%s'...\n", configMapName, namespace)

	configMap, err := kubeClient.CoreV1().ConfigMaps(namespace).Get(ctx, configMapName, metav1.GetOptions{})
	if err != nil {
		return false, fmt.Errorf("failed to get ConfigMap '%s': %w", configMapName, err)
	}

	// The data is stored in a map[string]string.
	fmt.Println("Data from ConfigMap:")
	for key, value := range configMap.Data {
		fmt.Printf("\t%s: %s\n", key, value)
	}
	managed, err := strconv.ParseBool(configMap.Data["argocdManaged"])
	if err != nil {
		return false, fmt.Errorf("failed to parse argocdManaged value: %w", err)
	}
	return managed, nil
}
