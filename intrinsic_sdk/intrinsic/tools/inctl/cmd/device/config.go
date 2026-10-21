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
	"context"
	"encoding/json"
	"fmt"
	"net"
	"os"
	"sort"
	"strings"
	"time"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/intrinsic-ai/insrc/incode/cloud/devicemanager/shared"

	"github.com/spf13/cobra"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

const (
	gatewayError      = "Cluster is currently not connected to the cloud relay. Make sure it is turned on and connected to the internet.\nIf the device restarted in the last 10 minutes, wait a couple of minutes, then try again.\n"
	unauthorizedError = "Request authorization failed. This happens when you generated a new API-Key on a different machine or the API-Key expired.\n"
)

func prettyPrintStatusInterfaces(interfaces map[string]shared.StatusInterface) string {
	ret := ""
	names := make([]string, len(interfaces))
	i := 0
	for k := range interfaces {
		names[i] = k
		i++
	}
	sort.Strings(names)
	for _, name := range names {
		ips := make([]string, len(interfaces[name].IPAddress))
		copy(ips, interfaces[name].IPAddress)
		sort.Strings(ips)
		ret = ret + fmt.Sprintf("\t%s: %v\n", name, ips)
	}

	return ret
}

var configCmd = &cobra.Command{
	Use:   "config",
	Short: "Tool to interact with device config",
}

type networkConfigInfo struct {
	Current map[string]shared.StatusInterface `json:"current"`
	Config  string                            `json:"config"`
}

func (info *networkConfigInfo) String() string {
	return "Current state:\n" + prettyPrintStatusInterfaces(info.Current) + "Current config: " + info.Config
}

var configGetCmd = &cobra.Command{
	Use:   "get",
	Short: "Retrieve current configuration",
	RunE: func(cmd *cobra.Command, args []string) error {
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}

		projectName := viperLocal.GetString(orgutil.KeyProject)
		orgName := viperLocal.GetString(orgutil.KeyOrganization)

		if clusterName == "" {
			// deviceID is required but clusterName is optional. They're normally the same for single-node
			// clusters.
			clusterName = deviceID
		}
		ctx := cmd.Context()
		client, err := newClient(ctx, projectName, orgName, clusterName)
		if err != nil {
			return fmt.Errorf("get project client: %w", err)
		}
		defer client.close()

		statusNetwork, err := client.getStatusNetwork(ctx, clusterName, deviceID)
		if err != nil {
			switch status.Code(err) {
			case codes.NotFound:
				fmt.Fprintf(os.Stderr, "Cluster does not exist. Either it does not exist, or you don't have access to it.\n")
			case codes.PermissionDenied:
				fmt.Fprint(os.Stderr, unauthorizedError)
			}
			return err
		}

		network, err := client.getNetworkConfig(ctx, clusterName, deviceID)
		if err != nil {
			return err
		}
		jsonNetwork, err := json.Marshal(network)
		if err != nil {
			return err
		}
		prtr.Print(&networkConfigInfo{Current: statusNetwork, Config: string(jsonNetwork)})

		return nil
	},
}

var configSetCmd = &cobra.Command{
	Use:   "set",
	Short: "Set the network config",
	Args:  cobra.MatchAll(cobra.ExactArgs(1), cobra.OnlyValidArgs),
	ValidArgsFunction: func(cmd *cobra.Command, args []string, toComplete string) ([]string, cobra.ShellCompDirective) {
		var comps []string
		if len(args) == 0 {
			comps = cobra.AppendActiveHelp(comps, "You must provide a valid network configuration in json format.")
		}

		return comps, cobra.ShellCompDirectiveNoFileComp
	},

	RunE: func(cmd *cobra.Command, args []string) error {
		configString := args[0]
		projectName := viperLocal.GetString(orgutil.KeyProject)
		orgName := viperLocal.GetString(orgutil.KeyOrganization)
		if clusterName == "" {
			// deviceID is required but clusterName is optional. They're normally the same for single-node
			// clusters.
			clusterName = deviceID
		}
		ctx := cmd.Context()
		client, err := newClient(ctx, projectName, orgName, clusterName)
		if err != nil {
			return fmt.Errorf("get project client: %w", err)
		}
		defer client.close()

		var config map[string]shared.Interface
		if err := json.Unmarshal([]byte(configString), &config); err != nil {
			fmt.Fprintf(os.Stderr, "Provided configuration is not a valid configuration string.\n")
			return err
		}

		for name := range config {
			// This is a soft error to allow for later changes
			// The list should cover
			// * en*: All wired interface names set by udev
			// * wl*: All wireless interface names set by udev (usually wlp... or wlan#)
			// * realtime_nic0: For our own naming scheme
			if !strings.HasPrefix(name, "en") && !strings.HasPrefix(name, "wl") && !strings.HasPrefix(name, "realtime_nic") {
				fmt.Fprintf(os.Stderr, "WARNING: Interface %q does not look like a valid interface.\n", name)
			}

			// This is an easy to make mistake in the config building.
			if net.ParseIP(name) != nil {
				return fmt.Errorf("%q was used as interface name but is an IP address, please use \"en...\" for example", name)
			}
		}

		req := &clustermanagerpb.UpdateNetworkConfigRequest{
			Cluster: clusterName,
			Device:  deviceID,
			Config:  translateToNetworkConfig(config),
		}
		lro, err := client.grpcClient.UpdateNetworkConfig(ctx, req)
		if err != nil {
			switch status.Code(err) {
			case codes.NotFound:
				fmt.Fprintf(os.Stderr, "Cluster does not exist. Either it does not exist, or you don't have access to it.\n")
			case codes.PermissionDenied:
				fmt.Fprint(os.Stderr, unauthorizedError)
			}
			return err
		}

		lroctx, stop := context.WithTimeout(ctx, time.Minute*3)
		defer stop()

		ticker := time.NewTicker(time.Second * 5)
		defer ticker.Stop()

		for {
			select {
			case <-ticker.C:
				lro, err := client.grpcClient.GetOperation(ctx, &lropb.GetOperationRequest{Name: lro.GetName()})
				if err != nil {
					return err
				}
				if !lro.GetDone() {
					continue
				}
				if lro.GetError() != nil {
					return fmt.Errorf("operation %q failed: %v", lro.GetName(), lro.GetError())
				}
				fmt.Println("Successfully applied new network configuration to the device.")
				return nil
			case <-lroctx.Done():
				return fmt.Errorf("operation %q timed out", lro.GetName())
			}
		}
	},
}

func init() {
	deviceCmd.AddCommand(configCmd)
	configCmd.AddCommand(configGetCmd)
	configCmd.AddCommand(configSetCmd)
}
