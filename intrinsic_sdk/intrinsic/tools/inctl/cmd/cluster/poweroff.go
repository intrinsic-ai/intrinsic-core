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
	"context"
	"fmt"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	clustermanagergrpcpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
)

var poweroffDesc = `
Power off a cluster

Example:
	inctl cluster poweroff --cluster <my-cluster> --org <my-org>

If the IPC is online, it will power off.
`

func poweroffCluster(ctx context.Context, conn *grpc.ClientConn, cluster string) error {
	client := clustermanagergrpcpb.NewClustersServiceClient(conn)
	if _, err := client.PoweroffCluster(
		ctx, &clustermanagerpb.PoweroffClusterRequest{ClusterName: cluster}); err != nil {
		return fmt.Errorf("request to poweroff cluster: %w", err)
	}

	return nil
}

var clusterPoweroffCmd = &cobra.Command{
	Use:   "poweroff --cluster <my-cluster> --org <my-org>",
	Short: "Power off an IPC",
	Long:  poweroffDesc,
	RunE: func(cmd *cobra.Command, argv []string) error {
		ctx := cmd.Context()
		conn, err := NewCloudConn(ctx)
		if err != nil {
			return err
		}
		defer conn.Close()

		return poweroffCluster(ctx, conn, clusterName)
	},
}

func init() {
	ClusterCmd.AddCommand(clusterPoweroffCmd)
	clusterPoweroffCmd.PersistentFlags().StringVar(&clusterName, "cluster", "", "Name of the cluster to use")
}
