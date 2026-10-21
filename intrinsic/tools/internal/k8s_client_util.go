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

// Package k8sclientutil contains functions to help construct k8s client libraries.
package k8sclientutil

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"regexp"
	"strings"

	"intrinsic/kubernetes/config/crcconfig"

	log "github.com/golang/glog"
	registryv1alpha1 "github.com/googlecloudrobotics/core/src/go/pkg/apis/registry/v1alpha1"
	"github.com/googlecloudrobotics/core/src/go/pkg/client/versioned"
	"github.com/pkg/errors"
	"google.golang.org/api/container/v1"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/client-go/discovery"
	"k8s.io/client-go/dynamic"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/rest"
	"k8s.io/client-go/tools/clientcmd"
	clientcmdapi "k8s.io/client-go/tools/clientcmd/api"
)

var osStat = os.Stat // Stubbed out for testing.

const (
	crcRobotNameLabel = "cloudrobotics.com/robot-name"
)

var gkeRE = regexp.MustCompile("gke_(.*)_(.*)_(.*)")

func parseGKEContext(kubeCtx string) (string, string, string, error) {
	match := gkeRE.FindStringSubmatch(kubeCtx)
	if len(match) != 4 {
		return "", "", "", fmt.Errorf(`expected "gke_project_location_cluster", got %q`, kubeCtx)
	}
	return match[1], match[2], match[3], nil
}

// clusterURL returns the identifier needed to look up GKE cluster info through the container API.
func clusterURL(kubeCtx string) (string, error) {
	project, location, cluster, err := parseGKEContext(kubeCtx)
	if err != nil {
		return "", err
	}
	return fmt.Sprintf("projects/%s/locations/%s/clusters/%s", project, location, cluster), nil
}

// KubernetesConfig returns a rest.Config for creating new clientsets.
func KubernetesConfig(ctx context.Context, cluster string) (*rest.Config, error) {
	usr, err := user.Current()
	if err != nil {
		return nil, errors.Wrap(err, "user.Current()")
	}

	// Explicitly set GOOGLE_APPLICATION_CREDENTIALS for the k8s client to
	// be able to authenticate against GCP.
	// Normally, Kubernetes will look for credentials in
	// $HOME/.config/gcloud/application_default_credentials.json, but $HOME is set
	// differently when run through bazel. Basing the path on $USER still works
	// although it is not clear why as usr.Homedir looks like it should be the same
	// as os.UserHomeDir() (https://golang.org/src/os/user/lookup_stubs.go),
	// but it is not. Also see the TestGoogleApplicationCredsIsNotBazelPath test case.
	credentialsFile := filepath.Join(usr.HomeDir, ".config/gcloud/application_default_credentials.json")
	if _, err := osStat(credentialsFile); err == nil {
		os.Setenv("GOOGLE_APPLICATION_CREDENTIALS", credentialsFile)
	}

	config, err := clientcmd.NewNonInteractiveDeferredLoadingClientConfig(
		clientcmd.NewDefaultClientConfigLoadingRules(),
		&clientcmd.ConfigOverrides{
			CurrentContext: cluster,
		}).ClientConfig()
	if err != nil {
		// Failed to load kubeconfig, which normally means the local context is not configured.
		if project, location, cluster, parseErr := parseGKEContext(cluster); parseErr == nil { // parseErr IS nil
			// This happens when using "inctl chart start" to deploy to GKE but the context does not exist.
			return nil, fmt.Errorf(`%v

To configure a context to deploy to a GKE (cloud) cluster:
	gcloud container clusters get-credentials --project %s --region %s %s`, err, project, location, cluster)
		}
		return nil, fmt.Errorf(`%v

To configure an existing cluster, run:
  inctl cluster k8s configure --cluster %s --org intrinsic@PROJECT

To lease a new VM:
  inctl vm lease --context-alias %s --org intrinsic@PROJECT`, err, cluster, cluster)
	}
	return config, nil
}

// isCloudCluster return true if the given k8s context is for a cloud cluster (e.g. "cloud-robotics", "ml-compute-cluster").
func isCloudCluster(contextName string) bool {
	return strings.HasPrefix(contextName, "gke_")
}

// AppsClientset returns a clientset for the custom resources introduced
// by the Cloud Robotics apps layer (go/robco-apps-v2) using the Kubernetes
// context from cluster.
func AppsClientset(ctx context.Context, cluster string) (*versioned.Clientset, error) {
	config, err := KubernetesConfig(ctx, cluster)
	if err != nil {
		return nil, err
	}
	return versioned.NewForConfig(config)
}

// KubernetesClientset returns a Kubernetes clientset using the Kubernetes context from cluster.
func KubernetesClientset(ctx context.Context, cluster string) (*kubernetes.Clientset, error) {
	config, err := KubernetesConfig(ctx, cluster)
	if err != nil {
		return nil, err
	}
	return kubernetes.NewForConfig(config)
}

// DynamicClientset returns a Dynamic clientset using the Kubernetes context from cluster
func DynamicClientset(ctx context.Context, cluster string) (dynamic.Interface, error) {
	config, err := KubernetesConfig(ctx, cluster)
	if err != nil {
		return nil, err
	}
	return dynamic.NewForConfig(config)
}

// DiscoveryClient returns a Discovery client using the Kubernetes context from cluster
func DiscoveryClient(ctx context.Context, cluster string) (discovery.DiscoveryInterface, error) {
	config, err := KubernetesConfig(ctx, cluster)
	if err != nil {
		return nil, err
	}
	return discovery.NewDiscoveryClientForConfig(config)
}

func robot(ctx context.Context, appsClient versioned.Interface) (*registryv1alpha1.Robot, error) {
	robots, err := appsClient.RegistryV1alpha1().Robots("default").List(ctx, metav1.ListOptions{})
	if err != nil {
		return nil, errors.Wrap(err, "list robots")
	}
	if len(robots.Items) != 1 {
		return nil, fmt.Errorf("expected exactly one robot Custom Resource (got %d). Please run `inctl diagnose` and report to the Infra team", len(robots.Items))
	}
	return &robots.Items[0], nil
}

// GCPProject returns the GCP project name associated with the current context.
func GCPProject(ctx context.Context, appsClient versioned.Interface, cluster string) (string, error) {
	// The cloud cluster is a special case.
	if isCloudCluster(cluster) {
		project, _, _, err := parseGKEContext(cluster)
		if err != nil {
			return "", err
		}
		return project, nil
	}

	robot, err := robot(ctx, appsClient)
	if err != nil {
		return "", err
	}
	if robot.Spec.Project == "" {
		return "", errors.New("missing spec.project property on robot resource")
	}
	return robot.Spec.Project, nil
}

// UniqueClusterName returns the project-wide unique name of the current k8s cluster.
func UniqueClusterName(ctx context.Context, appsClient versioned.Interface, cluster string) (string, error) {
	log.InfoContext(ctx, "Searching for the project-wide unique cluster name of the cluster.")
	// The cloud clusters are a special case.
	if isCloudCluster(cluster) {
		if strings.HasSuffix(cluster, "_cloud-robotics") {
			return "cloud", nil
		} else if strings.HasSuffix(cluster, "-ar-cloud-robotics") {
			parts := strings.Split(cluster, "_")
			cleaned := strings.TrimSuffix(parts[len(parts)-1], "-ar-cloud-robotics")
			return fmt.Sprintf("cloud-%s", cleaned), nil
		} else if strings.HasSuffix(cluster, "_ml-compute-cluster") {
			return "ml-compute-cluster", nil
		} else if strings.HasSuffix(cluster, "_ml-compute-backup-cluster") {
			return "ml-compute-backup-cluster", nil
		} else if strings.HasSuffix(cluster, "_ml-workflows-cluster") {
			return "ml-workflows-cluster", nil
		} else {
			return "", fmt.Errorf("unknown cloud cluster with context %q", cluster)
		}
	}

	robot, err := robot(ctx, appsClient)
	if err != nil {
		return "", fmt.Errorf("while getting robot: %w", err)
	}
	robotName := robot.GetLabels()[crcRobotNameLabel]
	if robotName == "" {
		return "", fmt.Errorf("Missing %q label on robot %q", crcRobotNameLabel, robot.Name)
	}
	return robotName, nil
}

// ClusterLocation returns the location of the current k8s cluster in case of a cloud cluster.
func ClusterLocation(ctx context.Context, cluster string) (string, error) {
	// The cloud cluster is a special case.
	if isCloudCluster(cluster) {
		_, location, _, err := parseGKEContext(cluster)
		if err != nil {
			return "", err
		}
		return location, nil
	}
	return "", nil
}

// ErrMinikubeContext is returned when trying to resolve a minikube context to a project.
var ErrMinikubeContext = errors.New("project unknown for user \"minikube\"")

// ResolveContext resolves a context name to a project and cluster name by reading the local
// kubeconfig. It returns ErrMinikubeContext if the context refers to a minikube cluster,
// as for such clusters the project is not recorded in the kubeconfig, but you don't need
// to talk to them over the relay.
func ResolveContext(kubeCtx string) (string, string, error) {
	return resolveContext(kubeCtx, clientcmd.NewDefaultPathOptions())
}

// resolveContext exists so the test can inject the configAccess loader to point it at a temporary
// kubeconfig file.
func resolveContext(kubeCtxName string, configAccess clientcmd.ConfigAccess) (string, string, error) {
	config, err := configAccess.GetStartingConfig()
	if err != nil {
		return "", "", err
	}
	kubeCtx, ok := config.Contexts[kubeCtxName]
	if !ok {
		return "", "", fmt.Errorf("context %q not found in kubeconfig", kubeCtxName)
	}
	if kubeCtx.AuthInfo == "minikube" {
		return "", "", ErrMinikubeContext
	}
	project, _, _, err := parseGKEContext(kubeCtx.AuthInfo)
	if err != nil {
		return "", "", fmt.Errorf("context %q invalid: %v", kubeCtxName, err)
	}
	return project, kubeCtx.Cluster, nil
}

// ContextExists returns true if the context exists in the local kubeconfig, false if it's absent,
// and an error if the kubeconfig cannot be loaded.
func ContextExists(kubeCtx string) (bool, error) {
	return contextExists(kubeCtx, clientcmd.NewDefaultPathOptions())
}

// contextExists exists so the test can inject the configAccess loader to point it at a temporary
// kubeconfig file.
func contextExists(kubeCtxName string, configAccess clientcmd.ConfigAccess) (bool, error) {
	config, err := configAccess.GetStartingConfig()
	if err != nil {
		return false, err
	}
	_, ok := config.Contexts[kubeCtxName]
	return ok, nil
}

// CreateGKEContext creates a GKE context for the given cluster, needed by "bazel run :app" or
// "bazel run :chart".
func CreateGKEContext(ctx context.Context, project string) error {
	// Get cloud robotics information
	crc, err := crcconfig.New(ctx, project)
	if err != nil {
		return fmt.Errorf("get CRC config: %w", err)
	}
	cluster := crc[crcconfig.CloudRoboticsCtx]
	if cluster == "" {
		return fmt.Errorf("no key %s in CRC config", crcconfig.CloudRoboticsCtx)
	}
	log.InfoContextf(ctx, "Setting up cloud robotics context %s", cluster)

	// Use GKE service client (using GCP ADC) to get cluster information
	containerService, err := container.NewService(ctx)
	if err != nil {
		return err
	}
	url, err := clusterURL(cluster)
	if err != nil {
		return err
	}
	containerCluster, err := containerService.Projects.Locations.Clusters.Get(url).Context(ctx).Do()
	if err != nil {
		return err
	}
	server := fmt.Sprintf("https://%s", containerCluster.Endpoint)
	certificateAuthorityData, err := base64.StdEncoding.DecodeString(containerCluster.MasterAuth.ClusterCaCertificate)
	if err != nil {
		return err
	}

	// Write kube-config
	configAccess := clientcmd.NewDefaultPathOptions()
	config, err := configAccess.GetStartingConfig()
	if err != nil {
		return err
	}
	config.Clusters[cluster] = &clientcmdapi.Cluster{
		Server:                   server,
		CertificateAuthorityData: certificateAuthorityData,
	}
	config.AuthInfos[cluster] = &clientcmdapi.AuthInfo{
		Exec: DefaultGKEExecConfig(),
	}
	config.Contexts[cluster] = &clientcmdapi.Context{
		Cluster:   cluster,
		Namespace: "default",
		AuthInfo:  cluster,
	}
	if err := clientcmd.ModifyConfig(configAccess, *config, true); err != nil {
		return err
	}

	return nil
}

// SetContext creates local kubectl context for the given remote cluster. If alias is non-empty and
// not equal to cluster, contexts are created for both.
func SetContext(ctx context.Context, alias, cluster, project string) error {
	// first retrieve CRC config
	crc, err := crcconfig.New(ctx, project)
	if err != nil {
		return fmt.Errorf("failed to retrieve CRC context: %w", err)
	}
	user := crc[crcconfig.CloudRoboticsCtx]
	if user == "" {
		return fmt.Errorf("no key %s in CRC config", crcconfig.CloudRoboticsCtx)
	}

	configAccess := clientcmd.NewDefaultPathOptions()
	config, err := configAccess.GetStartingConfig()
	if err != nil {
		return err
	}
	server := fmt.Sprintf("https://www.endpoints.%s.cloud.goog/apis/core.kubernetes-relay/client/%s", project, cluster)
	config.Clusters[cluster] = &clientcmdapi.Cluster{Server: server}
	config.AuthInfos[user] = &clientcmdapi.AuthInfo{
		Exec: DefaultGKEExecConfig(),
	}
	config.Contexts[cluster] = &clientcmdapi.Context{
		Cluster:   cluster,
		Namespace: "default",
		AuthInfo:  user,
	}
	config.CurrentContext = cluster
	if alias != "" && alias != cluster {
		config.Contexts[alias] = &clientcmdapi.Context{
			Cluster:   cluster,
			Namespace: "default",
			AuthInfo:  user,
		}
		config.CurrentContext = alias
	}
	if err := clientcmd.ModifyConfig(configAccess, *config, true); err != nil {
		return err
	}

	return nil
}

func FindClusterInCloud(ctx context.Context, projectName string, clusterName string) (*container.Cluster, error) {
	srv, err := container.NewService(ctx)
	if err != nil {
		return nil, fmt.Errorf("create container service: %w", err)
	}

	csrv := container.NewProjectsLocationsClustersService(srv)
	clusters, err := csrv.List("projects/" + projectName + "/locations/-").Do()
	if err != nil {
		return nil, fmt.Errorf("list clusters: %w", err)
	}

	for _, c := range clusters.Clusters {
		if c.Name == clusterName {
			return c, nil
		}
	}
	return nil, fmt.Errorf("cluster %q not found in project %q", clusterName, projectName)
}

func getBaseConfig(clusterName string, project string, cluster *clientcmdapi.Cluster) *clientcmdapi.Config {
	return &clientcmdapi.Config{
		Kind:       "Config",
		APIVersion: "v1",
		Clusters:   map[string]*clientcmdapi.Cluster{clusterName: cluster},
		AuthInfos: map[string]*clientcmdapi.AuthInfo{project: {
			Exec: DefaultGKEExecConfig(),
		}},
		Contexts: map[string]*clientcmdapi.Context{clusterName: {
			Cluster:   clusterName,
			Namespace: "default",
			AuthInfo:  project,
		}},
		CurrentContext: clusterName,
	}
}

func CloudConfig(ctx context.Context, projectName string, clusterName string) (*clientcmdapi.Config, error) {
	c, err := FindClusterInCloud(ctx, projectName, clusterName)
	if err != nil {
		return nil, fmt.Errorf("find cluster in cloud: %w", err)
	}
	ca, err := base64.StdEncoding.DecodeString(c.MasterAuth.ClusterCaCertificate)
	if err != nil {
		return nil, fmt.Errorf("decode cluster CA certificate: %w", err)
	}

	return getBaseConfig(clusterName, projectName, &clientcmdapi.Cluster{
		Server:                   "https://" + c.Endpoint,
		CertificateAuthorityData: ca,
	}), nil
}

func OnPremConfig(projectName string, clusterName string) *clientcmdapi.Config {
	urlPath := fmt.Sprintf("https://www.endpoints.%s.cloud.goog/apis/core.kubernetes-relay/client/%s", projectName, clusterName)
	return getBaseConfig(clusterName, projectName, &clientcmdapi.Cluster{Server: urlPath})
}

// DefaultGKEExecConfig returns the default ExecConfig for gke-gcloud-auth-plugin.
func DefaultGKEExecConfig() *clientcmdapi.ExecConfig {
	var args []string
	if shouldUseADCs() {
		args = append(args, "--use_application_default_credentials")
	}

	return &clientcmdapi.ExecConfig{
		APIVersion:         "client.authentication.k8s.io/v1beta1",
		Command:            "gke-gcloud-auth-plugin",
		Args:               args,
		ProvideClusterInfo: true,
		InteractiveMode:    clientcmdapi.IfAvailableExecInteractiveMode,
	}
}

func shouldUseADCs() bool {
	return os.Getenv("CLOUDSDK_CONTAINER_USE_APPLICATION_DEFAULT_CREDENTIALS") == "true"
}
