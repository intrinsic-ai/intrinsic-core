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

package logs

import (
	"context"
	"fmt"
	"time"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util"
	"intrinsic/tools/inctl/util/color"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	pb "intrinsic/logging/proto/logger_service_go_proto"
)

var logsSyncCmd = &cobra.Command{
	Use:   "sync",
	Short: "Manually triggers a flush of buffered logs to the cloud",
	Long:  "Manually triggers a flush of buffered logs to the cloud. You can specify event sources to sync or sync all.",
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()

		eventSources, err := cmd.Flags().GetStringSlice("event_source")
		if err != nil {
			return errors.Wrap(err, "failed to read event_source flag")
		}

		syncAll, err := cmd.Flags().GetBool("all")
		if err != nil {
			return errors.Wrap(err, "failed to read all flag")
		}

		wait, err := cmd.Flags().GetBool("wait")
		if err != nil {
			return errors.Wrap(err, "failed to read wait flag")
		}

		req := &pb.SyncRequest{
			EventSources: eventSources,
			SyncAll:      syncAll,
			WaitForFlush: wait,
		}

		// We use a 60-second timeout for the connection and the potentially expensive sync operation
		// because the api-relay ingress has a default proxy-read-timeout of 60 seconds.
		// If we set this higher, the client will hang indefinitely until the ingress drops the connection.
		connectCtx, cf := context.WithDeadline(ctx, time.Now().Add(60*time.Second))
		defer cf()

		conn, err := auth.NewCloudConnection(connectCtx, auth.WithFlagValues(localViper), auth.WithCluster(flagContext))
		if err != nil {
			return errors.Wrap(err, "failed to create cloud connection")
		}
		defer conn.Close()

		client := pb.NewDataLoggerClient(conn)

		msg := "Triggering log sync. This may take a while..."
		if wait {
			msg = "Triggering log sync and waiting for flush. This may take a while..."
		}

		spinner := util.NewSpinner(connectCtx, cmd.OutOrStdout(), 100*time.Millisecond, util.PositionFront, util.StyleDotsUpload, util.ColorRGB, util.DirectionForward)
		spinner.Start(msg)

		resp, err := client.SyncAndRotateLogs(connectCtx, req)
		if err != nil {
			spinner.Stop("")
			if status.Code(err) == codes.DeadlineExceeded || errors.Is(err, context.DeadlineExceeded) {
				color.C.Yellow().Fprintf(cmd.OutOrStderr(), "\nWARNING: The operation timed out. This can happen when the IPC's internet connectivity is slow or down.\n")
			}
			return errors.Wrap(err, "failed to sync and rotate logs")
		}

		spinner.Stop("Done triggering log sync!")

		if len(resp.EventSources) > 0 {
			fmt.Fprintln(cmd.OutOrStdout(), "Successfully synced event sources:")
			for _, es := range resp.EventSources {
				fmt.Fprintf(cmd.OutOrStdout(), "  - %s\n", es)
			}
		} else {
			fmt.Fprintln(cmd.OutOrStdout(), "No event sources were successfully synced.")
		}

		if len(resp.ThrottledEventSources) > 0 {
			color.C.Yellow().Fprintf(cmd.OutOrStderr(), "\nWARNING: The following event sources were throttled by the backend and were NOT synced:\n")
			for _, es := range resp.ThrottledEventSources {
				color.C.Yellow().Fprintf(cmd.OutOrStderr(), "  - %s\n", es)
			}
			color.C.Yellow().Fprintf(cmd.OutOrStderr(), "Please wait before attempting to manually sync these sources again.\n")
		}

		return nil
	},
}

func init() {
	showLogs.AddCommand(logsSyncCmd)

	logsSyncCmd.Flags().StringVarP(&flagContext, "context", "c", "", "The Kubernetes cluster to use.")
	logsSyncCmd.MarkFlagRequired("context")

	logsSyncCmd.Flags().StringSlice("event_source", []string{}, "Event sources to sync (regex). Can be specified multiple times.")
	logsSyncCmd.Flags().Bool("all", false, "If true, sync all event sources.")
	logsSyncCmd.Flags().Bool("wait", false, "If true, wait for the backend to finish flushing logs to the cloud.")

	logsSyncCmd.MarkFlagsMutuallyExclusive("event_source", "all")
	logsSyncCmd.MarkFlagsOneRequired("event_source", "all")
}
