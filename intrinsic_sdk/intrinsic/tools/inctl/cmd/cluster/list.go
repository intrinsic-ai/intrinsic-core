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
	"bytes"
	"cmp"
	"context"
	"encoding/json"
	"fmt"
	"slices"
	"strings"
	"text/tabwriter"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	clusterdiscoverygrpcpb "intrinsic/frontend/cloud/api/v1/clusterdiscovery_api_go_proto"
	clusterdiscoverypb "intrinsic/frontend/cloud/api/v1/clusterdiscovery_api_go_proto"
)

// ListClusterDescriptionsResponse embeds clusterdiscoverypb.ListClusterDescriptionsResponse.
type ListClusterDescriptionsResponse struct {
	m *clusterdiscoverypb.ListClusterDescriptionsResponse
}

// MarshalJSON converts a ListClusterDescriptionsResponse to a byte slice.
func (res *ListClusterDescriptionsResponse) MarshalJSON() ([]byte, error) {
	type cluster struct {
		ClusterName string `json:"clusterName,omitempty"`
		DisplayName string `json:"displayName,omitempty"`
		K8sContext  string `json:"k8sContext,omitempty"`
		Region      string `json:"region,omitempty"`
		CanDoSim    bool   `json:"canDoSim,omitempty"`
		CanDoReal   bool   `json:"canDoReal,omitempty"`
		HasGpu      bool   `json:"hasGpu,omitempty"`
	}
	clusters := make([]cluster, len(res.m.Clusters))
	for i, c := range res.m.Clusters {
		clusters[i] = cluster{
			ClusterName: c.GetClusterName(),
			DisplayName: c.GetDisplayName(),
			K8sContext:  c.GetK8SContext(),
			Region:      c.GetRegion(),
			CanDoSim:    c.GetCanDoSim(),
			CanDoReal:   c.GetCanDoReal(),
			HasGpu:      c.GetHasGpu(),
		}
	}
	return json.Marshal(struct {
		Clusters []cluster `json:"clusters"`
	}{Clusters: clusters})
}

// String converts a ListClusterDescriptionsResponse to a string
func (res *ListClusterDescriptionsResponse) String() string {
	// Sort by display name to match IPC managers's default sort.
	clusters := make([]*clusterdiscoverypb.ClusterDescription, len(res.m.Clusters))
	copy(clusters, res.m.Clusters)
	slices.SortFunc(clusters, func(a, b *clusterdiscoverypb.ClusterDescription) int {
		return cmp.Compare(a.GetDisplayName(), b.GetDisplayName())
	})

	b := new(bytes.Buffer)
	w := tabwriter.NewWriter(b,
		/*minwidth=*/ 1 /*tabwidth=*/, 1 /*padding=*/, 1 /*padchar=*/, ' ' /*flags=*/, 0)
	fmt.Fprintf(w, "%s\t%s\t%s\n", "Display Name", "ID", "Region")
	for _, c := range res.m.Clusters {
		fmt.Fprintf(w, "%s\t%s\t%s\n", c.GetDisplayName(), c.GetClusterName(), c.GetRegion())
	}
	w.Flush()
	// Remove the trailing newline as the pretty-printer wrapper will add one.
	return strings.TrimSuffix(b.String(), "\n")
}

func fetchAndPrintClusters(ctx context.Context, conn *grpc.ClientConn, prtr printer.Printer) error {
	client := clusterdiscoverygrpcpb.NewClusterDiscoveryServiceClient(conn)
	resp, err := client.ListClusterDescriptions(
		ctx, &clusterdiscoverypb.ListClusterDescriptionsRequest{})
	if err != nil {
		return fmt.Errorf("request to list clusters failed: %w", err)
	}

	prtr.Print(&ListClusterDescriptionsResponse{m: resp})

	return nil
}

var clusterListCmd = &cobra.Command{
	Use:   "list",
	Short: "List clusters in a project",
	Long:  "List compute cluster on the given project.",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		prtr, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}

		ctx := cmd.Context()
		conn, err := NewCloudConn(ctx)
		if err != nil {
			return err
		}
		defer conn.Close()

		return fetchAndPrintClusters(ctx, conn, prtr)
	},
}

func init() {
	ClusterCmd.AddCommand(clusterListCmd)
}
