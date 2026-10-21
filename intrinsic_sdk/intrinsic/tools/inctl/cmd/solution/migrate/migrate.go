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

// Package migrate provides a command for automated Solution migrations.
package migrate

import (
	"context"
	"fmt"
	"io"
	"strings"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"

	motionplannerservicemigrationpb "intrinsic/solution_versions/proto/v1alpha1/motion_planner_service_migration_go_proto"
	solutionmigrationpb "intrinsic/solution_versions/proto/v1alpha1/solution_migration_go_proto"
)

const (
	migrationMotionPlanner = "motion-planner"
)

// dial opens the gRPC connection to the SolutionMigration Service. Tests replace it with a
// connection to a local fake server, because the default needs cloud credentials.
var dial = dialCloud

func dialCloud(ctx context.Context, v *viper.Viper) (*grpc.ClientConn, error) {
	return auth.NewCloudConnection(ctx, auth.WithFlagValues(v))
}

// migrateFlags holds the command argument and flag values that define a MigrateRequest.
type migrateFlags struct {
	branchID   string
	snapshotID string
	dryRun     bool
	migration  string
	config     string
}

func printMigrateResult(cmd *cobra.Command, req *solutionmigrationpb.MigrateRequest, resp *solutionmigrationpb.MigrateResponse) error {
	w := cmd.OutOrStdout()
	if printer.GetFlagOutputType(cmd) == printer.OutputTypeJSON {
		data, err := protojson.Marshal(resp)
		if err != nil {
			return fmt.Errorf("cannot marshal response as JSON: %w", err)
		}
		fmt.Fprintln(w, string(data))
		return nil
	}

	renderMigrationBanner(w, req, resp)
	renderMigrationSummary(w, req.GetDryRun(), resp.GetMigrationSummary())

	// Point the user to Portal only when the migration saved a new snapshot, because a dry run or a
	// no-op leaves nothing to review or commit.
	saved := !req.GetDryRun() && resp.GetSnapshotSource().GetSnapshotId() != ""
	if saved {
		fmt.Fprintln(w, "\nTo review the changes and commit them, visit the Solution Version Viewer in Portal.")
	}
	return nil
}

func renderMigrationBanner(w io.Writer, req *solutionmigrationpb.MigrateRequest, resp *solutionmigrationpb.MigrateResponse) {
	if req.GetDryRun() {
		color.C.Yellow().Fprintf(
			w,
			"DRY-RUN COMPLETE: Diff computed in memory. No changes were saved to deployment branch %s.\n",
			req.GetBranchId(),
		)
		// A dry run never saves a snapshot, so an empty summary is the only sign that the migration
		// found nothing to change. Say so, because a bare banner looks like missing output.
		if strings.TrimSpace(resp.GetMigrationSummary()) == "" {
			color.C.Green().Fprintf(w, "Solution is already up to date. The migration would make no changes.\n")
		}
		return
	}

	snapshotID := resp.GetSnapshotSource().GetSnapshotId()
	if snapshotID == "" {
		color.C.Green().Fprintf(w, "SUCCESS: Solution is already up to date. No changes were saved.\n")
		return
	}

	color.C.Green().Fprintf(
		w,
		"SUCCESS: Migration saved to deployment branch %s. (Snapshot: %s)\n",
		resp.GetSnapshotSource().GetBranchId(),
		snapshotID,
	)
}

func renderMigrationSummary(w io.Writer, dryRun bool, summary string) {
	summary = strings.TrimSpace(summary)
	if summary == "" {
		return
	}

	header := "\nMigration Summary:"
	if dryRun {
		header = "\nMigration Summary (Dry-Run Preview):"
	}

	fmt.Fprintln(w, header)
	for _, line := range strings.Split(summary, "\n") {
		fmt.Fprintf(w, "  %s\n", line)
	}
}

// unmarshalConfig parses the --config JSON into m. An empty config leaves m unchanged, so that
// every migration can run with its defaults.
func unmarshalConfig(config string, m proto.Message) error {
	if config == "" {
		return nil
	}
	if err := protojson.Unmarshal([]byte(config), m); err != nil {
		return fmt.Errorf("cannot parse --config as JSON for %s: %w", m.ProtoReflect().Descriptor().FullName(), err)
	}
	return nil
}

// newMigrateRequest builds the complete MigrateRequest from the command argument and flags.
func newMigrateRequest(flags migrateFlags) (*solutionmigrationpb.MigrateRequest, error) {
	req := &solutionmigrationpb.MigrateRequest{
		BranchId:   flags.branchID,
		SnapshotId: flags.snapshotID,
		DryRun:     flags.dryRun,
	}
	switch flags.migration {
	case migrationMotionPlanner:
		mps := &motionplannerservicemigrationpb.MotionPlannerServiceMigration{}
		if err := unmarshalConfig(flags.config, mps); err != nil {
			return nil, err
		}
		req.MigrationType = &solutionmigrationpb.MigrateRequest_MotionPlannerService{
			MotionPlannerService: mps,
		}
	case "":
		return nil, fmt.Errorf("--migration is required (supported migrations: %s)", migrationMotionPlanner)
	default:
		return nil, fmt.Errorf("unknown migration %q (supported migrations: %s)", flags.migration, migrationMotionPlanner)
	}
	return req, nil
}

// NewCommand returns the migrate command.
func NewCommand() *cobra.Command {
	viperLocal := viper.New()
	var flags migrateFlags

	migrateCmd := orgutil.WrapCmd(&cobra.Command{
		Use:   "migrate <deployment_branch_id>",
		Short: "Perform a migration on a saved Solution.",
		Long: `Perform a migration on a saved Solution.

The argument must be a deployment branch ID (ending in _BRANCH), not a Solution ID or a version
branch ID. The migration saves a new snapshot to the deployment branch. It does not commit to the
upstream version branch, so you can review the changes and commit them in Portal. Use --dry-run
to preview the changes without saving them.`,
		Example: `  # Preview migration without saving
  inctl solution migrate <deployment_branch_id> --migration motion-planner --dry-run --org <org>

  # Apply migration and save it to the deployment branch
  inctl solution migrate <deployment_branch_id> --migration motion-planner --org <org>

  # Pin the Motion Planner Service version to install
  inctl solution migrate <deployment_branch_id> --migration motion-planner --config '{"targetVersion": "1.0.4"}' --org <org>

  # Migrate a specific historical snapshot. The saved snapshot is based on that snapshot, so it
  # does not include changes made after it.
  inctl solution migrate <deployment_branch_id> --migration motion-planner --snapshot-id <snapshot_id> --org <org>`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			flags.branchID = args[0]
			req, err := newMigrateRequest(flags)
			if err != nil {
				return fmt.Errorf("invalid migration target: %w", err)
			}

			ctx := cmd.Context()
			conn, err := dial(ctx, viperLocal)
			if err != nil {
				return fmt.Errorf("cannot create cloud connection: %w", err)
			}
			defer conn.Close()

			resp, err := solutionmigrationpb.NewSolutionMigrationClient(conn).Migrate(ctx, req)
			if err != nil {
				return fmt.Errorf("migration %q failed: %w", flags.migration, err)
			}

			if err := printMigrateResult(cmd, req, resp); err != nil {
				return fmt.Errorf("cannot print migration result: %w", err)
			}
			return nil
		},
	}, viperLocal)

	migrateCmd.Flags().StringVar(
		&flags.migration,
		"migration",
		"",
		fmt.Sprintf("Required. Name of the migration to run (e.g. %s).", migrationMotionPlanner),
	)
	migrateCmd.Flags().StringVar(
		&flags.config,
		"config",
		"",
		`Optional JSON-formatted migration configuration (e.g. '{"targetVersion": "1.0.4"}').`,
	)
	migrateCmd.Flags().StringVar(
		&flags.snapshotID,
		"snapshot-id",
		"",
		"Optional snapshot ID to migrate (defaults to the branch tip snapshot). The saved snapshot "+
			"does not include changes made after this snapshot.",
	)
	migrateCmd.Flags().BoolVar(
		&flags.dryRun,
		"dry-run",
		false,
		"Preview migration without saving changes.",
	)

	return migrateCmd
}
