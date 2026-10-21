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
	"crypto/tls"
	"fmt"
	"math"
	"os"
	"path"
	"strings"
	"time"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util"
	"intrinsic/tools/inctl/util/color"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"

	bpb "intrinsic/logging/proto/blob_go_proto"
	dgrpcpb "intrinsic/logging/proto/log_dispatcher_service_go_proto"
	dpb "intrinsic/logging/proto/log_dispatcher_service_go_proto"
	lgrpcpb "intrinsic/logging/proto/logger_service_go_proto"
	lpb "intrinsic/logging/proto/logger_service_go_proto"

	timestamppb "google.golang.org/protobuf/types/known/timestamppb"
)

const (
	defaultLookback    = 10 * time.Minute
	defaultReceiveSize = 100 * 1024 * 1024

	// LINT.IfChange(defaultMaxPageSizeMB)
	defaultMaxPageSizeMB = 25
	// LINT.ThenChange(//intrinsic/logging/dispatcher/server_impl.h:max_total_byte_size_mb)
)

var (
	flagLookBack               time.Duration
	flagHistoric               bool
	flagHistoricStartTimestamp string
	flagHistoricEndTimestamp   string
	flagQuiet                  bool
	flagRecordingID            string
)

func newConn(ctx context.Context) (*grpc.ClientConn, error) {
	project := cmdFlags.GetFlagProject()
	addr := "www.endpoints." + project + ".cloud.goog:443"

	cfg, err := auth.NewStore().GetConfiguration(project)
	if err != nil {
		return nil, err
	}
	creds, err := cfg.GetDefaultCredentials()
	if err != nil {
		return nil, err
	}

	grpcOpts := []grpc.DialOption{
		grpc.WithPerRPCCredentials(creds),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithTransportCredentials(credentials.NewTLS(&tls.Config{})),
	}
	conn, err := grpc.NewClient(addr, grpcOpts...)
	if err != nil {
		return nil, errors.Wrapf(err, "grpc.Dial(%q)", addr)
	}
	return conn, nil
}

func newLogDispatcherClient(ctx context.Context) (dgrpcpb.LogDispatcherClient, error) {
	conn, err := newConn(ctx)
	if err != nil {
		return nil, err
	}
	return dgrpcpb.NewLogDispatcherClient(conn), nil
}

// writeBlob writes the blob data to disk and clears the data field in the protobuf.
func writeBlob(blob *bpb.Blob, localDir string, spinner *util.Spinner) error {
	if len(blob.GetData()) == 0 {
		return nil
	}
	dir := path.Join(localDir, path.Dir(blob.BlobId))
	if err := os.MkdirAll(dir, os.ModePerm); err != nil {
		return errors.Wrapf(err, "os.MkdirAll %s", dir)
	}
	p := path.Join(localDir, blob.BlobId)
	if err := os.WriteFile(p, blob.GetData(), 0o644); err != nil {
		return errors.Wrapf(err, "os.WriteFile of blob to %s", p)
	}
	// Clear the blob data so we can write the rest of the response as a textproto.
	blob.Data = []byte{}
	if spinner != nil {
		spinner.Interrupt(fmt.Sprintf("file://%s", p))
	}
	return nil
}

func getLogsOnprem(ctx context.Context, cmd *cobra.Command, eventSource string, dir string) error {
	clusterName, err := getClusterName(ctx, &cmdParams{
		projectName: cmdFlags.GetFlagProject(),
		org:         cmdFlags.GetFlagOrganization(),
		context:     flagContext,
	})
	if err != nil {
		return errors.Wrap(err, "could not resolve cluster name")
	}

	conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(localViper), auth.WithCluster(clusterName))
	if err != nil {
		return errors.Wrap(err, "failed to create cloud connection")
	}
	defer conn.Close()

	client := lgrpcpb.NewDataLoggerClient(conn)
	request := &lpb.GetLogItemsRequest{
		Query: &lpb.GetLogItemsRequest_GetQuery{
			GetQuery: &lpb.GetLogItemsRequest_Query{
				EventSource: eventSource,
				StartTime:   timestamppb.New(time.Now().Add(flagLookBack * -1)),
			},
		},
	}

	spinner := util.NewSpinner(ctx, cmd.OutOrStdout(), 100*time.Millisecond, util.PositionFront, util.StyleDotsUpload, util.ColorRGB, util.DirectionReverse)
	spinner.Start("Getting local logs from the IPC...")

	for true {
		response, err := client.GetLogItems(context.Background(), request, grpc.MaxCallRecvMsgSize(math.MaxInt64), grpc.WaitForReady(true))
		if err != nil {
			spinner.Stop("")
			return errors.Wrap(err, "client.GetLogItems")
		}
		for _, item := range response.LogItems {
			if err := writeBlob(item.GetBlobPayload(), dir, spinner); err != nil {
				spinner.Stop("")
				return errors.Wrap(err, "writeBlob")
			}
		}
		nextPageCursor := response.GetNextPageCursor()
		truncationCause := response.GetTruncationCause()
		responseFilename := fmt.Sprintf("response_%d.pbtxt", time.Now().UnixNano())
		p := path.Join(dir, responseFilename)
		response.NextPageCursor = nil
		response.TruncationCause = nil
		if err = os.WriteFile(p, []byte(prototext.Format(response)), 0o644); err != nil {
			spinner.Stop("")
			return errors.Wrapf(err, "os.WriteFile of response to %s", p)
		}
		if len(truncationCause) == 0 {
			spinner.Stop(color.C.Green().Sprintf("Done getting local logs from the IPC."))
			return nil
		}
		if len(nextPageCursor) == 0 {
			break
		}
		request.Query = &lpb.GetLogItemsRequest_Cursor{
			Cursor: nextPageCursor,
		}
	}
	spinner.Stop(color.C.Green().Sprintf("Done getting local logs from the IPC."))
	return nil
}

func getLogsFromCloud(ctx context.Context, cmd *cobra.Command, eventSource string, dir string) error {
	orgID := cmdFlags.GetFlagOrganization()
	if orgID == "" {
		return errors.New("org should be specified")
	}
	if flagHistoricStartTimestamp == "" || flagHistoricEndTimestamp == "" {
		return errors.New("historic start timestamp and historic end timestamp should be specified")
	}

	client, err := newLogDispatcherClient(ctx)
	if err != nil {
		return errors.Wrap(err, "newLogDispatcherClient")
	}

	spinner := util.NewSpinner(ctx, cmd.OutOrStdout(), 100*time.Millisecond, util.PositionFront, util.StyleDotsUpload, util.ColorRGB, util.DirectionReverse)
	spinner.Start("Creating cloud cache and loading logs...This may take a while.")

	startTime, err := time.Parse(time.RFC3339, flagHistoricStartTimestamp)
	if err != nil {
		spinner.Stop("")
		return errors.Wrapf(err, "invalid start timestamp: %s", flagHistoricStartTimestamp)
	}
	endTime, err := time.Parse(time.RFC3339, flagHistoricEndTimestamp)
	if err != nil {
		spinner.Stop("")
		return errors.Wrapf(err, "invalid end timestamp: %s", flagHistoricEndTimestamp)
	}
	loadRequest := &dpb.LoadCloudLogItemsRequest{
		LoadQuery: &dpb.LoadCloudLogItemsRequest_Query{
			LogSource: &dpb.LogSource{
				EventSource:  eventSource,
				WorkcellName: flagContext,
			},
		},
		StartTime:      timestamppb.New(startTime),
		EndTime:        timestamppb.New(endTime),
		OrganizationId: orgID,
	}
	if flagRecordingID != "" {
		loadRequest.BagId = proto.String(flagRecordingID)
	}
	loadResp, err := client.LoadCloudLogItems(ctx, loadRequest)
	if err != nil {
		spinner.Stop("")
		return errors.Wrap(err, "client.LoadCloudLogItems")
	}
	if loadResp.Metadata.NumItems == 0 {
		spinner.Stop("")
		fmt.Fprintln(cmd.OutOrStdout(), "No logs found matched the query")
		return errors.Wrapf(err, "no logs found matched the query")
	}

	spinner.UpdateMessage("Finished creating cloud cache. Getting logs from cloud cache and writing to disk...This may take a while.")

	getReq := &dpb.GetCloudLogItemsRequest{
		Query: &dpb.GetCloudLogItemsRequest_GetQuery{
			GetQuery: &dpb.GetCloudLogItemsRequest_Query{
				LogSource: &dpb.LogSource{
					EventSource:  eventSource,
					WorkcellName: flagContext,
				},
				StartTime: timestamppb.New(startTime),
				EndTime:   timestamppb.New(endTime),
			},
		},
		SessionToken:       loadResp.GetSessionToken(),
		MaxTotalByteSizeMb: proto.Float64(defaultMaxPageSizeMB),
		OrganizationId:     orgID,
	}
	waitTimeForLogs := 5 * time.Second
	waitAttemptsForLogs := 20
	totalLogItemSize := uint64(0)
	numDownloadedLogItems := uint64(0)
	for {
		getStartTime := time.Now()
		getResp, err := client.GetCloudLogItems(ctx, getReq, grpc.MaxCallRecvMsgSize(defaultReceiveSize))
		if err != nil {
			if waitAttemptsForLogs > 0 && strings.Contains(err.Error(), "NotFound") {
				waitAttemptsForLogs--
				spinner.UpdateMessage("No logs found in cache yet, waiting again for logs to be loaded...")
				time.Sleep(waitTimeForLogs)
				continue
			}
			spinner.Stop("")
			return errors.Wrap(err, "client.GetCloudLogItems")
		}
		numDownloadedLogItems += uint64(len(getResp.GetItems()))

		spinner.Interrupt(fmt.Sprintf(
			"It took %s to load a page of %v items. Total number of items downloaded so far: %d",
			time.Since(getStartTime), len(getResp.GetItems()), numDownloadedLogItems))

		for _, item := range getResp.GetItems() {
			totalLogItemSize += uint64(proto.Size(item))
			if err := writeBlob(item.GetBlobPayload(), dir, spinner); err != nil {
				spinner.Stop("")
				return errors.Wrap(err, "writeBlob")
			}
			// Clear the blob payload from the item before it's written as textproto below,
			// to avoid duplicating large binary payloads in the metadata files.
			item.BlobPayload = nil
		}

		nextPageCursor := getResp.GetNextPageCursor()
		responseFilename := fmt.Sprintf("response_%d.pbtxt", time.Now().UnixNano())
		p := path.Join(dir, responseFilename)
		getResp.NextPageCursor = nil
		getResp.NextPageCursorExpiry = nil
		if err = os.WriteFile(p, []byte(prototext.Format(getResp)), 0o644); err != nil {
			spinner.Stop("")
			return errors.Wrapf(err, "os.WriteFile of response to %s", p)
		}
		if len(nextPageCursor) == 0 {
			break
		}
		spinner.UpdateMessage("Downloading next page...")
		getReq = &dpb.GetCloudLogItemsRequest{
			Query: &dpb.GetCloudLogItemsRequest_Cursor{
				Cursor: nextPageCursor,
			},
			SessionToken:       loadResp.GetSessionToken(),
			MaxTotalByteSizeMb: proto.Float64(defaultMaxPageSizeMB),
			OrganizationId:     orgID,
		}
	}

	spinner.Stop(color.C.Green().Sprintf("Download complete. Total number of items: %d. Total size: %d bytes", numDownloadedLogItems, totalLogItemSize))
	return nil
}

var logsCpCmd = &cobra.Command{
	Use:   "cp <event_source> <destination> [--lookback=600] | --historic [--historic_start_timestamp=2024-08-20T12:00:00Z --historic_end_timestamp=2024-08-20T12:00:00Z]",
	Short: "Copies recently logged blobs & logs to a local folder",
	Long:  "Copies recently logged blobs & logs to a local folder",
	Args:  cobra.ExactArgs(2),
	RunE: func(cmd *cobra.Command, args []string) (err error) {
		startTime := time.Now()
		defer func() {
			if err == nil {
				if !flagQuiet {
					color.C.Green().Fprintf(cmd.OutOrStdout(), "Download took %s\n", time.Since(startTime))
				} else {
					fmt.Fprintf(cmd.OutOrStdout(), "Download took %s\n", time.Since(startTime))
				}
			}
		}()

		ctx := cmd.Context()
		if flagRecordingID != "" && !flagHistoric {
			return errors.New("--recording_id can only be used with --historic")
		}
		if err = os.MkdirAll(args[1], os.ModePerm); err != nil {
			return errors.Wrapf(err, "os.MkdirAll %s", args[1])
		}

		if flagHistoric {
			if !flagQuiet {
				color.C.Cyan().Fprintf(cmd.OutOrStdout(), "Pulling logs from the Cloud...\n")
				color.C.Yellow().Fprintf(cmd.ErrOrStderr(), "Reminder: this command retrieves logs that were recorded using a best effort pipeline.\n")
				color.C.Yellow().Fprintf(cmd.ErrOrStderr(), "For more information about log retention, please review https://flowstate.intrinsic.ai/docs/operate/store_transmit_and_access_data/structured_logging/overview/#data-flow .\n")
				color.C.Yellow().Fprintf(cmd.ErrOrStderr(), "If your use case has stricter requirements about reliability, we recommend using the Recordings feature, documented at https://flowstate.intrinsic.ai/docs/operate/store_transmit_and_access_data/structured_logging/solution_recordings/\n")
				color.C.Yellow().Fprintf(cmd.ErrOrStderr(), "Pass --quiet to suppress this notice.\n")
			}
			err = getLogsFromCloud(ctx, cmd, args[0], args[1])
			return err
		}

		if !flagQuiet {
			color.C.Green().Fprintf(cmd.OutOrStdout(), "Pulling logs from the local IPC...\n")
		}
		err = getLogsOnprem(ctx, cmd, args[0], args[1])
		if err != nil {
			if !flagQuiet {
				color.C.Yellow().Fprintf(cmd.ErrOrStderr(), "Warning: Failed to connect to IPC. The IPC may be offline or connectivity is bad. Try pulling historic logs instead using --historic.\n")
			}
			return err
		}
		return nil
	},
}

func init() {
	showLogs.AddCommand(logsCpCmd)
	logsCpCmd.Flags().DurationVar(&flagLookBack, "lookback", defaultLookback, "The time window to copy logs from")
	logsCpCmd.Flags().StringVarP(&flagContext, "context", "c", "", "The Kubernetes cluster to use.")
	logsCpCmd.Flags().BoolVar(&flagHistoric, "historic", false, "Uses the cloud to fetch historical logs.")
	logsCpCmd.Flags().StringVar(&flagHistoricStartTimestamp, "historic_start_timestamp", "", "Start timestamp in RFC3339 format for fetching historical logs. eg. 2024-08-20T12:00:00Z")
	logsCpCmd.Flags().StringVar(&flagHistoricEndTimestamp, "historic_end_timestamp", "", "End timestamp in RFC3339 format for fetching historical logs. eg. 2024-08-20T12:00:00Z")
	logsCpCmd.Flags().StringVar(&flagRecordingID, "recording_id", "", "The ID of a specific recording to pull logs from.")
	logsCpCmd.Flags().BoolVarP(&flagQuiet, "quiet", "q", false, "Suppress the best-effort pipeline reminder message")
	logsCpCmd.MarkFlagRequired("context")

	logsCpCmd.MarkFlagsRequiredTogether("historic", "historic_start_timestamp", "historic_end_timestamp")
	logsCpCmd.MarkFlagsMutuallyExclusive("historic", "lookback")
	logsCpCmd.MarkFlagsMutuallyExclusive("lookback", "recording_id")
}
