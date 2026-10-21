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

// Package device groups the commands for managing on-prem devices via inctl.
package device

import (
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/viperutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

const (
	keyClusterName = "cluster_name"
)

var (
	deviceID    = ""
	clusterName = ""
	viperLocal  = viper.New()
)

var deviceCmd = orgutil.WrapCmd(
	&cobra.Command{
		Use:   "device",
		Short: "Manage device specific settings and registration.",
		Long:  "This subcommand provides utilities that interact with singular devices.\nThese are generally part of a cluster, or not yet registered to any user.",
		// Catching common typos and potential alternatives
		SuggestFor: []string{"devcie", "dve", "deviec"},
	}, viperLocal)

func init() {
	root.RootCmd.AddCommand(deviceCmd)

	deviceCmd.PersistentFlags().StringVarP(&deviceID, "device_id", "", "", "The device ID of the device to claim")
	deviceCmd.MarkPersistentFlagRequired("device_id")

	deviceCmd.PersistentFlags().StringVarP(&clusterName, keyClusterName, "", "",
		`The cluster to join. Required for workers, ignored on control-plane.
		You can set the environment variable INTRINSIC_CLUSTER_NAME=cluster_name to set a default cluster_name.`)

	viperutil.BindFlags(viperLocal, deviceCmd.PersistentFlags(), viperutil.BindToListEnv(keyClusterName))
}
