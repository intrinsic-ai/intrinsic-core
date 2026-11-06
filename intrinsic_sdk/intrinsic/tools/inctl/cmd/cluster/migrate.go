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

// Copyright 2026 Intrinsic Innovation LLC

package cluster

import (
	"context"
	"fmt"
	"time"

	env "intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/agents"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	longrunningpb "cloud.google.com/go/longrunning/autogen/longrunningpb"

	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermigrationpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermigration_api_go_proto"
)

var (
	migrateDesc = `
Safely migrate a single-node IPC cluster from its current organization/project to a target organization/project.

Example:
	inctl cluster migrate <cluster-name> --org <source-org> --target_org <target-org@target-project>
`
	flagMigrateTargetOrg        string
	flagMigratePrivateDevice    bool
	flagMigratePreserveVersions bool
	flagMigrateDetach           bool
	flagMigratePollInterval     = 2 * time.Second
)

func newPortalMigrationConn(ctx context.Context, sourceProject string) (*grpc.ClientConn, error) {
	envName := ClusterCmdViper.GetString(orgutil.KeyEnvironment)
	if envName == "" {
		envName = env.FromAnyProject(sourceProject)
	}
	portalProject := env.PortalProject(envName)
	if portalProject == "" {
		portalProject = env.PortalProject(env.Prod)
	}
	return auth.NewCloudConnection(
		ctx,
		auth.WithFlagValues(ClusterCmdViper),
		auth.WithTargetProject(portalProject),
		auth.WithEnv(envName),
	)
}

func runMigrateCluster(ctx context.Context, out printer.Printer, client clustermigrationpb.ClusterMigrationServiceClient, req *clustermigrationpb.MigrateClusterRequest, detach bool, pollInterval time.Duration) error {
	op, err := client.MigrateCluster(ctx, req)
	if err != nil {
		return fmt.Errorf("failed to start cluster migration: %w", err)
	}
	out.PrintSf("Started migration operation %s for cluster %q (%s -> %s)", op.GetName(), req.GetClusterName(), req.GetSourceProject(), req.GetTargetProject())
	if detach {
		return nil
	}

	var lastStage clustermigrationpb.MigrationStage
	for !op.GetDone() {
		op, err = client.GetOperation(ctx, &longrunningpb.GetOperationRequest{Name: op.GetName()})
		if err != nil {
			return fmt.Errorf("failed to poll migration operation %s: %w", op.GetName(), err)
		}

		var meta clustermigrationpb.MigrateClusterMetadata
		if op.GetMetadata() != nil {
			if err := op.GetMetadata().UnmarshalTo(&meta); err == nil {
				if meta.GetStage() != lastStage && meta.GetStage() != clustermigrationpb.MigrationStage_MIGRATION_STAGE_UNSPECIFIED {
					lastStage = meta.GetStage()
					out.PrintSf("Migration stage: %s", lastStage.String())
				}
			}
		}

		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-time.After(pollInterval):
			continue
		}
	}

	if opErr := op.GetError(); opErr != nil {
		var meta clustermigrationpb.MigrateClusterMetadata
		if op.GetMetadata() != nil {
			_ = op.GetMetadata().UnmarshalTo(&meta)
		}
		if meta.GetStage() != clustermigrationpb.MigrationStage_MIGRATION_STAGE_UNSPECIFIED {
			return fmt.Errorf("cluster migration failed (stage %s): %s (code %d)", meta.GetStage().String(), opErr.GetMessage(), opErr.GetCode())
		}
		return fmt.Errorf("cluster migration failed: %s (code %d)", opErr.GetMessage(), opErr.GetCode())
	}

	var migratedCluster clustermanagerpb.Cluster
	if op.GetResponse() != nil {
		if err := op.GetResponse().UnmarshalTo(&migratedCluster); err != nil {
			out.PrintSf("Failed to read cluster from response: %v", err)
			migratedCluster.ClusterName = "ERROR"
		}
	}
	out.PrintSf("Successfully migrated cluster %q to organization %q in project %q", migratedCluster.GetClusterName(), req.GetTargetOrganizationId(), req.GetTargetProject())
	return nil
}

var clusterMigrateCmd = &cobra.Command{
	Use:   "migrate <cluster-name> --org <source-org> --target_org <target-org>",
	Short: "Safely migrate a single-node IPC cluster to another project/organization",
	Long:  migrateDesc,
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, argv []string) error {
		if err := agents.Check(cmd); err != nil {
			return err
		}
		out, err := printer.NewPrinter(root.FlagOutput)
		if err != nil {
			return err
		}
		clusterName := argv[0]
		sourceProject := ClusterCmdViper.GetString(orgutil.KeyProject)
		sourceOrg := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		targetOrg, targetProject, err := orgutil.ResolveOrgAndProject(flagMigrateTargetOrg)
		if err != nil {
			return fmt.Errorf("failed to resolve --target_org %q: %w", flagMigrateTargetOrg, err)
		}

		ctx := cmd.Context()
		conn, err := newPortalMigrationConn(ctx, sourceProject)
		if err != nil {
			return err
		}
		defer conn.Close()

		preserve := flagMigratePreserveVersions
		req := &clustermigrationpb.MigrateClusterRequest{
			ClusterName:          clusterName,
			SourceProject:        sourceProject,
			SourceOrganizationId: sourceOrg,
			TargetProject:        targetProject,
			TargetOrganizationId: targetOrg,
			PrivateDevice:        flagMigratePrivateDevice,
			PreserveVersions:     &preserve,
		}
		client := clustermigrationpb.NewClusterMigrationServiceClient(conn)
		return runMigrateCluster(ctx, out, client, req, flagMigrateDetach, flagMigratePollInterval)
	},
}

func init() {
	ClusterCmd.AddCommand(clusterMigrateCmd)

	clusterMigrateCmd.Flags().StringVar(&flagMigrateTargetOrg, "target_org", "", "Target Intrinsic organization (e.g. <org> or <org@project>)")
	clusterMigrateCmd.Flags().BoolVar(&flagMigratePrivateDevice, "private_device", false, "Register the migrated cluster as a private device owned only by the migrating user in the target organization")
	clusterMigrateCmd.Flags().BoolVar(&flagMigratePreserveVersions, "preserve_versions", true, "Preserve the cluster's OS and Intrinsic platform versions from the source project")
	clusterMigrateCmd.Flags().BoolVar(&flagMigrateDetach, "detach", false, "Return immediately after starting the migration operation without waiting for completion")
	_ = clusterMigrateCmd.MarkFlagRequired("target_org")
}
