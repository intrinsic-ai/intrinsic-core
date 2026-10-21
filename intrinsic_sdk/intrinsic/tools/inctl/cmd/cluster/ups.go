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

package cluster

import (
	"encoding/json"
	"fmt"
	"os"
	"text/tabwriter"

	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	clustermanageralphapb "intrinsic/frontend/cloud/api/v1alpha1/clustermanager_api_go_proto"
)

var (
	upsFlags struct {
		driver string
		port   string
	}
	experimentalFlag bool
)

var upsCmd = &cobra.Command{
	Use:   "ups",
	Short: "[EXPERIMENTAL] Configure a UPS.",
	Long: `[EXPERIMENTAL] Configure a UPS (uninterruptible power supply) attached to the cluster.

Use the subcommands to show, enable or disable UPS monitoring, for example:

  inctl cluster ups show --enable-experimental --org <org>[@<project>] --cluster <cluster>
  inctl cluster ups enable --enable-experimental --org <org>[@<project>] --cluster <cluster> --driver <driver> [--port <port>]`,
}

type upsInfo struct {
	Driver string `json:"driver,omitempty"`
	Port   string `json:"port,omitempty"`
}

var upsShowCmd = &cobra.Command{
	Use:   "show",
	Short: "[EXPERIMENTAL] Show the current UPS configuration.",
	Long:  `[EXPERIMENTAL] Show the current UPS configuration of the cluster.`,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()

		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)

		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("device manager client:\n%w", err)
		}
		defer c.close()

		req := clustermanageralphapb.GetConfigRequest{
			Project:   projectName,
			Org:       orgName,
			ClusterId: clusterName,
		}
		resp, err := c.alphaClient.GetConfig(ctx, &req)
		if err != nil {
			return err
		}
		ups := resp.GetUps()
		if ups == nil {
			return fmt.Errorf("this IPC doesn't support UPS monitoring: please update the OS")
		}

		ot := printer.GetFlagOutputType(cmd)
		if ot == printer.OutputTypeJSON {
			info := upsInfo{}
			if ups.GetDriver() != "" {
				info.Driver = ups.GetDriver()
				info.Port = ups.GetPort()
			}
			b, err := json.Marshal(info)
			if err != nil {
				return err
			}
			fmt.Fprintln(cmd.OutOrStdout(), string(b))
			return nil
		}

		if ups.GetDriver() == "" {
			fmt.Println("No UPS configured.")
			return nil
		}
		w := tabwriter.NewWriter(os.Stdout, 0, 0, 3, ' ', 0)
		fmt.Fprintf(w, "driver\tport\n")
		fmt.Fprintf(w, "%s\t%s\n", ups.GetDriver(), ups.GetPort())
		w.Flush()
		// TODO: b/443259813 - check for alarms due to missing UPS
		return nil
	},
}

var upsEnableCmd = &cobra.Command{
	Use:   "enable --enable-experimental --org <org>[@<project>] --cluster <cluster> --driver <driver> [--port <port>]",
	Short: "[EXPERIMENTAL] Enable a UPS.",
	Long: `[EXPERIMENTAL] Configure a UPS (uninterruptible power supply) attached to the cluster.

It will monitor the UPS and shut the IPC down when running from battery. This only works for
single-node clusters (a single IPC, as opposed to an older Tier 3 system).

To select the driver, see https://networkupstools.org/stable-hcl.html. To select the port, see the
driver-specific documentation.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()
		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client:\n%w", err)
		}
		defer c.close()

		// TODO: b/443257000 - watch LRO
		_, err = c.alphaClient.UpdateConfig(ctx, &clustermanageralphapb.UpdateConfigRequest{
			Project:   projectName,
			Org:       orgName,
			ClusterId: clusterName,
			Config: &clustermanageralphapb.Config{
				Ups: &clustermanageralphapb.UPS{
					Driver: upsFlags.driver,
					Port:   upsFlags.port,
				},
			},
		})
		return err
	},
}

var upsDisableCmd = &cobra.Command{
	Use:   "disable --enable-experimental --org <org>[@<project>] --cluster <cluster>",
	Short: "[EXPERIMENTAL] Disable a UPS.",
	Long: `[EXPERIMENTAL] Configure the cluster to not connect to a UPS (uninterruptible power supply).

You can still plug it into a UPS, but it won't shut down when running from battery.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()
		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client:\n%w", err)
		}
		defer c.close()

		// TODO: b/443257000 - watch LRO
		_, err = c.alphaClient.UpdateConfig(ctx, &clustermanageralphapb.UpdateConfigRequest{
			Project:   projectName,
			Org:       orgName,
			ClusterId: clusterName,
			Config: &clustermanageralphapb.Config{
				Ups: &clustermanageralphapb.UPS{
					// Empty UPS config disables the UPS.
				},
			},
		})
		return err
	},
}

func init() {
	ClusterCmd.AddCommand(upsCmd)
	upsCmd.PersistentFlags().StringVar(&clusterName, "cluster", "", "Name of cluster to configure.")
	upsCmd.MarkPersistentFlagRequired("cluster")
	// TODO: b/443257818 - remove flag once the API is stable
	upsCmd.PersistentFlags().BoolVar(&experimentalFlag, "enable-experimental", false, "Enable experimental features.")
	upsCmd.MarkPersistentFlagRequired("enable-experimental")
	upsCmd.AddCommand(upsShowCmd)
	upsCmd.AddCommand(upsDisableCmd)
	upsCmd.AddCommand(upsEnableCmd)
	upsEnableCmd.Flags().StringVar(&upsFlags.driver, "driver", "", "Driver to use for the UPS, eg \"usbhid-ups\" or \"snmp-ups\".")
	upsEnableCmd.MarkFlagRequired("driver")
	upsEnableCmd.Flags().StringVar(&upsFlags.port, "port", "auto", "Driver-dependent identifier for the UPS, eg \"auto\" for USB or \"192.168.1.123:161\" for SNMP")
}
