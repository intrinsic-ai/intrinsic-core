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

package device

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"regexp"
	"strings"
	"time"

	"intrinsic/tools/inctl/util/orgutil"

	"github.com/intrinsic-ai/insrc/incode/cloud/devicemanager/shared"

	log "github.com/golang/glog"
	"github.com/spf13/cobra"
)

const (
	// https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#dns-label-names
	hostnameRegexString = `^[a-z0-9]([a-z0-9-]{0,40}[a-z0-9])?`
	replaceKey          = "replace"
)

var (
	deviceRole    = ""
	deviceRegion  = ""
	privateDevice = false
	replaceDevice = false
	noWait        = false
	noUpdate      = false
	hostname      = ""
)

func validHostname(hostname string) (int, bool) {
	match := regexp.MustCompile(hostnameRegexString).FindStringIndex(hostname)
	if len(match) < 2 {
		return 0, false
	}

	// This should be guaranteed by ^, but better save than sorry
	if match[0] != 0 {
		return -1, false
	}

	// Make sure we matched the entire string
	if match[1] != len(hostname) {
		return match[1] + 1, false
	}

	return 0, true
}

func makeNameError(hostname string, index int) string {
	if len(hostname) == 0 {
		return "hostname cannot be empty"
	}

	if len(hostname) > 42 {
		return "hostname cannot exceed 42 characters"
	}

	if strings.HasSuffix(hostname, "-") {
		return "hostname cannot end with a dash"
	}

	if regexp.MustCompile("[A-Z]").MatchString(hostname) {
		return "hostname cannot contain upper case letters"
	}

	if index < 1 {
		index = 1
	}
	offender := hostname[index-1]
	return fmt.Sprintf("Cannot use %q in hostname", offender)
}

func waitForConfigDownload(ctx context.Context, client authedClient, clusterName, deviceID string) error {
	// This should usually only take 1-2 min.
	// If it takes longer than 5 minutes, there' something wrong.
	ctx, cancel := context.WithTimeout(ctx, time.Minute*5)
	defer cancel()

	fmt.Printf("Waiting for IPC to download config.")
	defer fmt.Printf("\n")
	for {
		status := map[string]any{}
		if err := client.getJSON(ctx, clusterName, deviceID, "configure:status", &status); err != nil {
			if errors.Is(err, context.DeadlineExceeded) {
				return fmt.Errorf("the IPC did not reach cloud infrastructure.\nPlease make sure the IPC has a stable internet connection and retry")
			}

			// This could be a transient network error or a 5xx error from nginx.
			log.WarningContextf(ctx, "Unexpected error while waiting for download, retrying: %v", err)
		}
		if downloaded, ok := status["downloaded"]; ok {
			if downloaded.(bool) {
				return nil
			}
		}
		fmt.Printf(".")
		time.Sleep(time.Second * 30)
	}
}

func waitForStatusAvailable(ctx context.Context, client authedClient, clusterName, deviceID string) error {
	fmt.Printf("Waiting for IPC to offer status")
	for {
		resp, err := client.getDevice(ctx, clusterName, deviceID, "relay/v1alpha1/status")
		if err != nil {
			if errors.Is(err, context.DeadlineExceeded) {
				return fmt.Errorf("the IPC failed to initialize.\nPlease make sure the IPC has as stable internet connection")
			}

			// This could be a transient network error.
			log.WarningContextf(ctx, "Unexpected error while getting status, retrying: %v", err)
		}
		resp.Body.Close()

		if resp.StatusCode == http.StatusOK {
			fmt.Printf("\n")
			return nil
		}

		// StatusBadGateway is expected when the control plane isn't up yet.
		// StatusNotFound is expected when a worker node isn't up yet.
		if resp.StatusCode != http.StatusBadGateway && resp.StatusCode != http.StatusNotFound {
			// This could be a transient nginx 5xx error.
			log.WarningContextf(ctx, "Unexpected error while getting status, retrying: %d", resp.StatusCode)
		}

		fmt.Printf(".")
		time.Sleep(time.Second * 30)
	}
}

func waitForCluster(ctx context.Context, client authedClient, clusterName, deviceID, hostname string) error {
	// 40 min timeout as this might require both an OS and an intrinsic-base update
	// See also devicemanager/create_cluster.go.
	ctx, cancel := context.WithTimeout(ctx, time.Minute*40)
	defer cancel()

	if err := waitForConfigDownload(ctx, client, clusterName, deviceID); err != nil {
		return fmt.Errorf("wait for config download: %w", err)
	}
	if err := waitForStatusAvailable(ctx, client, clusterName, hostname); err != nil {
		return fmt.Errorf("wait for status: %w", err)
	}

	fmt.Printf("Device finished initialization.\n")

	return nil
}

var registerCmd = &cobra.Command{
	Use:   "register",
	Short: "Tool to register hardware in setup flow",
	RunE: func(cmd *cobra.Command, args []string) error {
		projectName := viperLocal.GetString(orgutil.KeyProject)
		orgName := viperLocal.GetString(orgutil.KeyOrganization)
		if hostname == "" {
			hostname = deviceID
		}
		if deviceRole != "control-plane" && clusterName == "" {
			fmt.Printf("--cluster_name needs to be provided for role %q\n", deviceRole)
			return fmt.Errorf("invalid arguments")
		}

		if offender, ok := validHostname(hostname); !ok {
			fmt.Printf("%q is not a valid as hostname. Provide a valid hostname.\nSee https://kubernetes.io/docs/concepts/overview/working-with-objects/names/#dns-label-names for more information.\n", hostname)
			return fmt.Errorf("%s", makeNameError(hostname, offender))
		}

		ctx := cmd.Context()
		client, err := newClient(ctx, projectName, orgName, clusterName)
		if err != nil {
			return fmt.Errorf("get client for project: %w", err)
		}
		defer client.close()

		// This map represents a json mapping of a config struct.

		// https://github.com/intrinsic-ai/xfa-tools/blob/main/pkg/devicemanager/config/config.go#L291

		config := map[string]any{
			"hostname": hostname,
			"cloudConnection": map[string]any{
				"project": projectName,
				"token":   "not-a-valid-token",
				"name":    clusterName,
			},
			"cluster": map[string]any{
				"role": deviceRole,
				// Only relevant for worker, but this doesn't hurt the control-plane nodes.
				"controlPlaneURI": fmt.Sprintf("%s:6443", clusterName),
				"token":           shared.TokenPlaceholder,
			},
			"proxyConfig": map[string]any{
				"aqualine": 2,
			},
			"version": "v1alphav1",
		}
		marshalled, err := json.Marshal(config)
		if err != nil {
			return fmt.Errorf("failed to marshal config: %w", err)
		}
		data := shared.ConfigureData{
			Hostname:   hostname,
			Config:     marshalled,
			Role:       deviceRole,
			Cluster:    clusterName,
			Private:    privateDevice,
			Region:     deviceRegion,
			Replace:    replaceDevice,
			AutoUpdate: !noUpdate,
		}
		if testID := os.Getenv("INCTL_CREATED_BY_TEST"); testID != "" {
			// This is an automated test.
			data.CreatedByTest = testID
		}
		body, err := json.Marshal(data)
		if err != nil {
			return fmt.Errorf("failed to marshal config: %w", err)
		}

		resp, err := client.postDevice(ctx, clusterName, deviceID, "configure", bytes.NewBuffer(body))
		if err != nil {
			return err
		}
		defer resp.Body.Close()

		switch resp.StatusCode {
		case http.StatusOK:
			fmt.Printf("Sent configuration to server. The device will reboot and apply the configuration within a minute.\n")
		case http.StatusConflict:
			return fmt.Errorf("cluster %q already exists. Please use a unique value for --hostname if this is a new cluster.\nTo replace the old cluster, call with --%s", hostname, replaceKey)
		case http.StatusPreconditionFailed:
			return fmt.Errorf("cluster %q does not exist. Please make sure that --cluster_name matches the --hostname from a previously registered cluster.\nIf you want to create a new cluster, do not use --device_role", clusterName)
		case http.StatusNotFound:
			return fmt.Errorf("device %q does not exist. Please make sure you have the exact id from the device you are trying to register", deviceID)
		case http.StatusUnauthorized:
			return fmt.Errorf("your login key has expired or been replaced.\nRun 'inctl auth login' to update it")
		case http.StatusForbidden:
			return fmt.Errorf("you do not have the necessary permissions to add a cluster on organization %q.\nOpen a support request to get the 'clusterProvisioner' role", orgutil.QualifiedOrg(projectName, orgName))
		default:
			io.Copy(os.Stderr, resp.Body)

			return fmt.Errorf("request failed. http code: %v", resp.StatusCode)
		}
		if !noWait {
			if err := waitForCluster(ctx, client, clusterName, deviceID, hostname); err != nil {
				return fmt.Errorf("wait for device: %w", err)
			}
		}

		return nil
	},
}

func init() {
	deviceCmd.AddCommand(registerCmd)

	registerCmd.Flags().StringVarP(&deviceRole, "device_role", "", "control-plane", "The role the device has in the cluster. Either 'control-plane' or 'worker'")
	registerCmd.Flags().BoolVarP(&privateDevice, "private", "", false, "If set to 'true', the device will not be visible to other organization members")
	registerCmd.Flags().StringVarP(&deviceRegion, "region", "", "unspecified", "This can be used for inventory tracking")
	registerCmd.Flags().BoolVarP(&replaceDevice, replaceKey, "", false, "If set to 'true', an existing cluster with the same name will be replaced.\nThis is equivalent to calling 'inctl cluster delete' first")
	registerCmd.Flags().BoolVarP(&noWait, "no-wait", "", false, "Set to true to avoid waiting for the cluster initialization.")
	registerCmd.Flags().BoolVarP(&noUpdate, "no-update", "", false, "Do not enroll the cluster into automatic updates.")
	registerCmd.Flags().StringVarP(&hostname, "hostname", "", "",
		`The hostname for the device. If it's a control plane this will be the cluster name.`)
}
