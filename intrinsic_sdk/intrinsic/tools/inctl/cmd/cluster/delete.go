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

	"intrinsic/tools/inctl/util/agents"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	clustermanagergrpcpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
)

var (
	deleteDesc = `
Delete an IPC.

Example:
	inctl cluster delete <cluster-name> --org <my-org>

If the IPC is online, it will be reset back to the unregistered state. If it
is offline, it will be removed from your organization, but will need to be
reset or reinstalled before it is registered to another project.
`
	require_reachable = false
)

func deleteCluster(ctx context.Context, conn *grpc.ClientConn, cluster string) error {
	client := clustermanagergrpcpb.NewClustersServiceClient(conn)
	if _, err := client.DeleteCluster(
		ctx, &clustermanagerpb.DeleteClusterRequest{ClusterName: cluster, RequireReachable: require_reachable}); err != nil {
		return fmt.Errorf("request to delete cluster: %w", err)
	}

	return nil
}

var clusterDeleteCmd = &cobra.Command{
	Use:   "delete my-cluster --org <my-org>",
	Short: "Delete an IPC",
	Long:  deleteDesc,
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, argv []string) error {
		if err := agents.Check(cmd); err != nil {
			return err
		}
		ctx := cmd.Context()
		conn, err := NewCloudConn(ctx)
		if err != nil {
			return err
		}
		defer conn.Close()

		return deleteCluster(ctx, conn, argv[0])
	},
}

func init() {
	ClusterCmd.AddCommand(clusterDeleteCmd)

	clusterDeleteCmd.Flags().BoolVar(&require_reachable, "require_reachable", false, "Fail the deletion command when the cluster was not reachable")
}
