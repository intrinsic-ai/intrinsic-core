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
	"io"
	"math"
	"net/http"
	"os"
	"os/signal"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util"
	"intrinsic/tools/inctl/util/color"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"go.opencensus.io/plugin/ocgrpc"
	"golang.org/x/sync/errgroup"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"

	bpb "intrinsic/logging/proto/blob_go_proto"
	dgrpcpb "intrinsic/logging/proto/log_dispatcher_service_go_proto"
	dpb "intrinsic/logging/proto/log_dispatcher_service_go_proto"
	lipb "intrinsic/logging/proto/log_item_go_proto"
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

	defaultBlobDownloadConcurrency = 16
	defaultBlobDownloadTimeout     = 5 * time.Minute
	defaultBlobMaxIdleConns        = 64
	defaultBlobMaxRetries          = 3
	defaultBlobRetryBackoff        = 200 * time.Millisecond
	defaultBlobMaxRetryBackoff     = 5 * time.Second
	defaultHTTPErrorBodySnippetLen = 512
	defaultWaitTimeForLogs         = 5 * time.Second
	defaultWaitAttemptsForLogs     = 20
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

// newLogDispatcherClient creates a gRPC LogDispatcherClient and returns it along with the underlying connection.
func newLogDispatcherClient(ctx context.Context) (dgrpcpb.LogDispatcherClient, *grpc.ClientConn, error) {
	conn, err := newConn(ctx)
	if err != nil {
		return nil, nil, err
	}
	return dgrpcpb.NewLogDispatcherClient(conn), conn, nil
}

// newBlobHTTPClient creates an HTTP client tuned for concurrent cloud blob downloads.
func newBlobHTTPClient() *http.Client {
	var transport http.RoundTripper
	if baseTransport, ok := http.DefaultTransport.(*http.Transport); ok {
		cloned := baseTransport.Clone()
		cloned.MaxIdleConnsPerHost = defaultBlobDownloadConcurrency
		cloned.MaxIdleConns = defaultBlobMaxIdleConns
		transport = cloned
	} else {
		transport = http.DefaultTransport
	}
	return &http.Client{
		Timeout:   defaultBlobDownloadTimeout,
		Transport: transport,
	}
}

// spinnerUpdate updates the spinner status message if the spinner is non-nil.
func spinnerUpdate(spinner *util.Spinner, msg string) {
	if spinner != nil {
		spinner.UpdateMessage(msg)
	}
}

// spinnerInterrupt prints a message line above the active spinner if the spinner is non-nil.
func spinnerInterrupt(spinner *util.Spinner, msg string) {
	if spinner != nil {
		spinner.Interrupt(msg)
	}
}

// spinnerStop stops the spinner with a final message if the spinner is non-nil.
func spinnerStop(spinner *util.Spinner, finalMsg string) {
	if spinner != nil {
		spinner.Stop(finalMsg)
	}
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

// cloudLogsCopyConfig holds parameters for copying historic logs and blobs from the cloud.
type cloudLogsCopyConfig struct {
	orgID               string
	workcell            string
	eventSource         string
	recordingID         string
	startTime           time.Time
	endTime             time.Time
	localDir            string
	waitTimeForLogs     time.Duration
	waitAttemptsForLogs int
	blobConcurrency     int
	blobMaxRetries      int
	blobRetryBackoff    time.Duration
	quiet               bool
	out                 io.Writer
	errOut              io.Writer
	spinner             *util.Spinner
}

// isFatalBlobError reports whether err is an authentication or permission failure that should abort copying immediately.
func isFatalBlobError(err error) bool {
	switch status.Code(err) {
	case codes.Unauthenticated, codes.PermissionDenied:
		return true
	default:
		return false
	}
}

// isRetryableHTTPStatus reports whether statusCode represents a transient HTTP error worth retrying.
func isRetryableHTTPStatus(statusCode int) bool {
	switch statusCode {
	case http.StatusTooManyRequests,
		http.StatusInternalServerError,
		http.StatusBadGateway,
		http.StatusServiceUnavailable,
		http.StatusGatewayTimeout:
		return true
	default:
		return false
	}
}

// isTerminalWriter reports whether w is backed by a character device (terminal).
func isTerminalWriter(w io.Writer) bool {
	f, ok := w.(*os.File)
	if !ok {
		return false
	}
	fi, err := f.Stat()
	if err != nil {
		return false
	}
	return (fi.Mode() & os.ModeCharDevice) != 0
}

// parseHistoricTimeRange parses and validates RFC3339Nano start and end timestamps.
func parseHistoricTimeRange(startStr, endStr string) (time.Time, time.Time, error) {
	if startStr == "" || endStr == "" {
		return time.Time{}, time.Time{}, errors.New("historic start timestamp and historic end timestamp should be specified")
	}
	startTime, err := time.Parse(time.RFC3339Nano, startStr)
	if err != nil {
		return time.Time{}, time.Time{}, errors.Wrapf(err, "invalid start timestamp: %s", startStr)
	}
	endTime, err := time.Parse(time.RFC3339Nano, endStr)
	if err != nil {
		return time.Time{}, time.Time{}, errors.Wrapf(err, "invalid end timestamp: %s", endStr)
	}
	if endTime.Before(startTime) {
		return time.Time{}, time.Time{}, errors.Errorf("historic start timestamp (%s) must not be after historic end timestamp (%s)", startStr, endStr)
	}
	return startTime, endTime, nil
}

// cloudLogCopier coordinates paginated fetching of cloud log items and concurrent downloading of referenced blobs.
type cloudLogCopier struct {
	client          dgrpcpb.LogDispatcherClient
	httpClient      *http.Client
	cfg             cloudLogsCopyConfig
	errOutMu        sync.Mutex
	blobsMu         sync.Mutex
	downloadedBlobs map[string]struct{}
	failedBlobs     atomic.Uint64
}

// newCloudLogCopier constructs a cloudLogCopier, applying default settings for any unset configuration fields.
func newCloudLogCopier(client dgrpcpb.LogDispatcherClient, httpClient *http.Client, cfg cloudLogsCopyConfig) *cloudLogCopier {
	if httpClient == nil {
		httpClient = newBlobHTTPClient()
	}
	if cfg.out == nil {
		cfg.out = io.Discard
	}
	if cfg.errOut == nil {
		cfg.errOut = io.Discard
	}
	if cfg.blobConcurrency <= 0 {
		cfg.blobConcurrency = defaultBlobDownloadConcurrency
	}
	if cfg.blobMaxRetries <= 0 {
		cfg.blobMaxRetries = defaultBlobMaxRetries
	}
	if cfg.blobRetryBackoff <= 0 {
		cfg.blobRetryBackoff = defaultBlobRetryBackoff
	}
	return &cloudLogCopier{
		client:          client,
		httpClient:      httpClient,
		cfg:             cfg,
		downloadedBlobs: make(map[string]struct{}),
	}
}

// emitWarning writes a yellow warning message through the spinner if active, or directly to errOut.
func (c *cloudLogCopier) emitWarning(msg string) {
	if c.cfg.spinner != nil {
		spinnerInterrupt(c.cfg.spinner, color.C.Yellow().Sprintf("%s", msg))
		return
	}
	c.errOutMu.Lock()
	defer c.errOutMu.Unlock()
	color.C.Yellow().Fprintf(c.cfg.errOut, "%s\n", msg)
}

// fetchAndWriteBlobOnce performs a single attempt to obtain a signed URL, download a blob, verify its size, and atomically write it to disk.
// The returned retryable boolean reports whether a failed attempt hit a transient error that should be retried.
func (c *cloudLogCopier) fetchAndWriteBlobOnce(ctx context.Context, blobID string, req *dpb.GetCloudBlobRequest) (uint64, string, bool, error) {
	relPath := filepath.FromSlash(blobID)
	if !filepath.IsLocal(relPath) {
		return 0, "", false, errors.Errorf("blob ID %q is not a valid local path", blobID)
	}

	resp, err := c.client.GetCloudBlob(ctx, req)
	if err != nil {
		switch status.Code(err) {
		case codes.Unavailable, codes.DeadlineExceeded, codes.ResourceExhausted:
			return 0, "", true, err
		default:
			return 0, "", false, err
		}
	}
	signedURL := resp.GetSignedUrl()
	if signedURL == "" {
		return 0, "", false, errors.Errorf("empty signed URL returned for blob %q", blobID)
	}

	httpReq, err := http.NewRequestWithContext(ctx, http.MethodGet, signedURL, nil)
	if err != nil {
		return 0, "", false, errors.Wrapf(err, "create HTTP request for blob %q", blobID)
	}
	httpResp, err := c.httpClient.Do(httpReq)
	if err != nil {
		return 0, "", true, errors.Wrapf(err, "HTTP GET for blob %q", blobID)
	}
	defer httpResp.Body.Close()

	if httpResp.StatusCode != http.StatusOK {
		snippetBytes, _ := io.ReadAll(io.LimitReader(httpResp.Body, defaultHTTPErrorBodySnippetLen))
		_, _ = io.Copy(io.Discard, io.LimitReader(httpResp.Body, 64*1024))
		retryable := isRetryableHTTPStatus(httpResp.StatusCode)
		if body := strings.TrimSpace(string(snippetBytes)); body != "" {
			return 0, "", retryable, errors.Errorf("HTTP GET for blob %q returned status %d (%s): %s", blobID, httpResp.StatusCode, httpResp.Status, body)
		}
		return 0, "", retryable, errors.Errorf("HTTP GET for blob %q returned status %d (%s)", blobID, httpResp.StatusCode, httpResp.Status)
	}

	destPath := filepath.Join(c.cfg.localDir, relPath)
	destDir := filepath.Dir(destPath)
	if err := os.MkdirAll(destDir, 0o755); err != nil {
		return 0, "", false, errors.Wrapf(err, "os.MkdirAll %s", destDir)
	}

	tmpFile, err := os.CreateTemp(destDir, ".blob-*")
	if err != nil {
		return 0, "", false, errors.Wrapf(err, "os.CreateTemp in %s", destDir)
	}
	tmpPath := tmpFile.Name()
	renamed := false
	defer func() {
		if !renamed {
			_ = tmpFile.Close()
			_ = os.Remove(tmpPath)
		}
	}()

	writtenBytes, err := io.Copy(tmpFile, httpResp.Body)
	if err != nil {
		return 0, "", true, errors.Wrapf(err, "io.Copy blob %q", blobID)
	}
	if meta := resp.GetMetadata(); meta != nil && writtenBytes != meta.GetSizeBytes() {
		return 0, "", true, errors.Errorf("size mismatch for blob %q: downloaded %d bytes, expected %d bytes", blobID, writtenBytes, meta.GetSizeBytes())
	}
	if err := tmpFile.Close(); err != nil {
		return 0, "", false, errors.Wrapf(err, "close temp file for blob %q", blobID)
	}
	if err := os.Chmod(tmpPath, 0o644); err != nil {
		return 0, "", false, errors.Wrapf(err, "chmod temp file for blob %q", blobID)
	}
	if err := os.Rename(tmpPath, destPath); err != nil {
		return 0, "", false, errors.Wrapf(err, "rename temp file to %s", destPath)
	}
	renamed = true

	return uint64(writtenBytes), destPath, false, nil
}

// fetchAndWriteBlob downloads and writes the blob for item, retrying transient failures with exponential backoff.
func (c *cloudLogCopier) fetchAndWriteBlob(ctx context.Context, item *lipb.LogItem) (uint64, string, error) {
	blobID := item.GetBlobPayload().GetBlobId()
	workcell := item.GetMetadata().GetWorkcellName()
	if workcell == "" {
		workcell = c.cfg.workcell
	}
	eventSource := item.GetMetadata().GetEventSource()
	if eventSource == "" {
		eventSource = c.cfg.eventSource
	}

	blobRef := &bpb.BlobReference{
		BlobId:         blobID,
		WorkcellName:   workcell,
		OrganizationId: c.cfg.orgID,
		EventSource:    eventSource,
	}
	if c.cfg.recordingID != "" {
		blobRef.BagId = proto.String(c.cfg.recordingID)
	}
	req := &dpb.GetCloudBlobRequest{
		BlobReference: blobRef,
		WithSignedUrl: proto.Bool(true),
	}

	backoff := c.cfg.blobRetryBackoff
	var lastErr error
	for attempt := 0; attempt <= c.cfg.blobMaxRetries; attempt++ {
		if attempt > 0 {
			timer := time.NewTimer(backoff)
			select {
			case <-ctx.Done():
				timer.Stop()
				return 0, "", ctx.Err()
			case <-timer.C:
			}
			backoff = min(backoff*2, defaultBlobMaxRetryBackoff)
		}

		writtenBytes, destPath, retryable, err := c.fetchAndWriteBlobOnce(ctx, blobID, req)
		if err == nil {
			return writtenBytes, destPath, nil
		}
		lastErr = err
		if !retryable || ctx.Err() != nil {
			return 0, "", err
		}
	}
	return 0, "", lastErr
}

// downloadItemBlob downloads a single deduplicated blob for a log item, recording non-fatal failures as warnings.
func (c *cloudLogCopier) downloadItemBlob(parentCtx, gctx context.Context, item *lipb.LogItem, downloadedBlobBytes *atomic.Uint64) error {
	if gctx.Err() != nil {
		return parentCtx.Err()
	}
	blobID := item.GetBlobPayload().GetBlobId()
	writtenBytes, destPath, err := c.fetchAndWriteBlob(gctx, item)
	if err != nil {
		if isFatalBlobError(err) {
			return fmt.Errorf("GetCloudBlob for blob %q: %w", blobID, err)
		}
		if gctx.Err() != nil {
			return parentCtx.Err()
		}
		c.failedBlobs.Add(1)
		if status.Code(err) == codes.NotFound {
			c.emitWarning(fmt.Sprintf("Warning: blob %q not found in cloud storage; skipping (%v)", blobID, err))
		} else {
			c.emitWarning(fmt.Sprintf("Warning: failed to download blob %q; skipping (%v)", blobID, err))
		}
		return nil
	}

	c.blobsMu.Lock()
	c.downloadedBlobs[blobID] = struct{}{}
	c.blobsMu.Unlock()

	downloadedBlobBytes.Add(writtenBytes)
	spinnerInterrupt(c.cfg.spinner, fmt.Sprintf("file://%s", destPath))
	return nil
}

// downloadPageBlobs strips inline blob payloads from items, deduplicates blob IDs, and concurrently downloads new blobs for a page.
func (c *cloudLogCopier) downloadPageBlobs(ctx context.Context, items []*lipb.LogItem) (uint64, error) {
	var (
		pageItemBytes   uint64
		itemsToDownload []*lipb.LogItem
		queuedInPage    = make(map[string]struct{})
	)
	for _, item := range items {
		if bp := item.GetBlobPayload(); bp != nil {
			bp.Data = nil
		}
		pageItemBytes += uint64(proto.Size(item))

		blobID := item.GetBlobPayload().GetBlobId()
		if blobID == "" {
			continue
		}
		if _, queued := queuedInPage[blobID]; queued {
			continue
		}
		c.blobsMu.Lock()
		_, alreadyDownloaded := c.downloadedBlobs[blobID]
		c.blobsMu.Unlock()
		if alreadyDownloaded {
			continue
		}
		queuedInPage[blobID] = struct{}{}
		itemsToDownload = append(itemsToDownload, item)
	}

	var downloadedBlobBytes atomic.Uint64
	g, gctx := errgroup.WithContext(ctx)
	g.SetLimit(c.cfg.blobConcurrency)

	for _, item := range itemsToDownload {
		if gctx.Err() != nil {
			break
		}
		g.Go(func() error {
			return c.downloadItemBlob(ctx, gctx, item, &downloadedBlobBytes)
		})
	}

	if err := g.Wait(); err != nil {
		return pageItemBytes + downloadedBlobBytes.Load(), err
	}
	if ctx.Err() != nil {
		return pageItemBytes + downloadedBlobBytes.Load(), ctx.Err()
	}
	return pageItemBytes + downloadedBlobBytes.Load(), nil
}

// printCompletionSummary writes the final download summary line to c.cfg.out.
func (c *cloudLogCopier) printCompletionSummary(numItems, totalSize uint64) {
	failedBlobs := c.failedBlobs.Load()
	summary := fmt.Sprintf(
		"Download complete. Total number of items: %d. Total size: %d bytes. Downloaded blobs: %d",
		numItems, totalSize, len(c.downloadedBlobs),
	)
	summaryColor := color.C.Green()
	if failedBlobs > 0 {
		summary = fmt.Sprintf("%s, Failed blobs: %d.", summary, failedBlobs)
		summaryColor = color.C.Yellow()
	} else {
		summary += "."
	}
	if c.cfg.quiet {
		fmt.Fprintln(c.cfg.out, summary)
		return
	}
	summaryColor.Fprintf(c.cfg.out, "%s\n", summary)
}

// copy loads matching cloud logs into cache, saves all pages and associated blobs on the local file system, and prints a completion summary.
func (c *cloudLogCopier) copy(ctx context.Context) error {
	defer spinnerStop(c.cfg.spinner, "")

	loadRequest := &dpb.LoadCloudLogItemsRequest{
		LoadQuery: &dpb.LoadCloudLogItemsRequest_Query{
			LogSource: &dpb.LogSource{
				EventSource:  c.cfg.eventSource,
				WorkcellName: c.cfg.workcell,
			},
		},
		StartTime:      timestamppb.New(c.cfg.startTime),
		EndTime:        timestamppb.New(c.cfg.endTime),
		OrganizationId: c.cfg.orgID,
	}
	if c.cfg.recordingID != "" {
		loadRequest.BagId = proto.String(c.cfg.recordingID)
	}
	loadResp, err := c.client.LoadCloudLogItems(ctx, loadRequest)
	if err != nil {
		return errors.Wrap(err, "client.LoadCloudLogItems")
	}
	if loadResp.GetMetadata().GetNumItems() == 0 {
		spinnerStop(c.cfg.spinner, "")
		fmt.Fprintln(c.cfg.out, "No logs found matched the query")
		return nil
	}

	if err := os.MkdirAll(c.cfg.localDir, 0o755); err != nil {
		return errors.Wrapf(err, "os.MkdirAll %s", c.cfg.localDir)
	}

	spinnerUpdate(c.cfg.spinner, "Finished creating cloud cache. Getting logs from cloud cache and writing to disk...This may take a while.")

	getReq := &dpb.GetCloudLogItemsRequest{
		Query: &dpb.GetCloudLogItemsRequest_GetQuery{
			GetQuery: &dpb.GetCloudLogItemsRequest_Query{
				LogSource: &dpb.LogSource{
					EventSource:  c.cfg.eventSource,
					WorkcellName: c.cfg.workcell,
				},
				StartTime: timestamppb.New(c.cfg.startTime),
				EndTime:   timestamppb.New(c.cfg.endTime),
			},
		},
		SessionToken:       loadResp.GetSessionToken(),
		MaxTotalByteSizeMb: proto.Float64(defaultMaxPageSizeMB),
		OrganizationId:     c.cfg.orgID,
		OmitBlobPayloads:   true,
	}

	waitAttemptsForLogs := c.cfg.waitAttemptsForLogs
	var (
		totalLogItemSize      uint64
		numDownloadedLogItems uint64
		lastResponseNano      int64
	)

	for {
		getStartTime := time.Now()
		getResp, err := c.client.GetCloudLogItems(ctx, getReq, grpc.MaxCallRecvMsgSize(defaultReceiveSize))
		if err != nil {
			if waitAttemptsForLogs > 0 && status.Code(err) == codes.NotFound && ctx.Err() == nil {
				waitAttemptsForLogs--
				spinnerUpdate(c.cfg.spinner, "No logs found in cache yet, waiting again for logs to be loaded...")
				timer := time.NewTimer(c.cfg.waitTimeForLogs)
				select {
				case <-ctx.Done():
					timer.Stop()
					return ctx.Err()
				case <-timer.C:
				}
				continue
			}
			return errors.Wrap(err, "client.GetCloudLogItems")
		}
		numDownloadedLogItems += uint64(len(getResp.GetItems()))

		spinnerInterrupt(c.cfg.spinner, fmt.Sprintf(
			"It took %s to load a page of %v items. Total number of items downloaded so far: %d",
			time.Since(getStartTime), len(getResp.GetItems()), numDownloadedLogItems))

		pageBytes, err := c.downloadPageBlobs(ctx, getResp.GetItems())
		totalLogItemSize += pageBytes
		if err != nil {
			return err
		}

		nextPageCursor := getResp.GetNextPageCursor()
		ts := time.Now().UnixNano()
		if ts <= lastResponseNano {
			ts = lastResponseNano + 1
		}
		lastResponseNano = ts
		responseFilename := fmt.Sprintf("response_%d.pbtxt", ts)
		p := filepath.Join(c.cfg.localDir, responseFilename)
		getResp.NextPageCursor = nil
		getResp.NextPageCursorExpiry = nil
		if err = os.WriteFile(p, []byte(prototext.Format(getResp)), 0o644); err != nil {
			return errors.Wrapf(err, "os.WriteFile of response to %s", p)
		}
		if len(nextPageCursor) == 0 {
			break
		}
		spinnerUpdate(c.cfg.spinner, "Downloading next page...")
		getReq = &dpb.GetCloudLogItemsRequest{
			Query: &dpb.GetCloudLogItemsRequest_Cursor{
				Cursor: nextPageCursor,
			},
			SessionToken:       loadResp.GetSessionToken(),
			MaxTotalByteSizeMb: proto.Float64(defaultMaxPageSizeMB),
			OrganizationId:     c.cfg.orgID,
			OmitBlobPayloads:   true,
		}
	}

	spinnerStop(c.cfg.spinner, "")
	c.printCompletionSummary(numDownloadedLogItems, totalLogItemSize)
	return nil
}

// copyLogsFromCloud copies historic cloud logs and blobs to disk using the provided clients and configuration.
func copyLogsFromCloud(ctx context.Context, client dgrpcpb.LogDispatcherClient, blobHTTPClient *http.Client, cfg cloudLogsCopyConfig) error {
	return newCloudLogCopier(client, blobHTTPClient, cfg).copy(ctx)
}

// getLogsFromCloud validates CLI flags, initializes clients and progress output, and copies historic logs from the cloud.
func getLogsFromCloud(ctx context.Context, cmd *cobra.Command, eventSource string, dir string) error {
	orgID := cmdFlags.GetFlagOrganization()
	if orgID == "" {
		return errors.New("org should be specified")
	}
	startTime, endTime, err := parseHistoricTimeRange(flagHistoricStartTimestamp, flagHistoricEndTimestamp)
	if err != nil {
		return err
	}

	client, conn, err := newLogDispatcherClient(ctx)
	if err != nil {
		return errors.Wrap(err, "newLogDispatcherClient")
	}
	defer conn.Close()

	errOut := cmd.ErrOrStderr()
	var spinner *util.Spinner
	if !flagQuiet && isTerminalWriter(errOut) {
		spinner = util.NewSpinner(context.Background(), errOut, 100*time.Millisecond, util.PositionFront, util.StyleDotsUpload, util.ColorRGB, util.DirectionReverse)
		spinner.Start("Creating cloud cache and loading logs...This may take a while.")
		defer spinnerStop(spinner, "")
	}

	cfg := cloudLogsCopyConfig{
		orgID:               orgID,
		workcell:            flagContext,
		eventSource:         eventSource,
		recordingID:         flagRecordingID,
		startTime:           startTime,
		endTime:             endTime,
		localDir:            dir,
		waitTimeForLogs:     defaultWaitTimeForLogs,
		waitAttemptsForLogs: defaultWaitAttemptsForLogs,
		blobConcurrency:     defaultBlobDownloadConcurrency,
		blobMaxRetries:      defaultBlobMaxRetries,
		blobRetryBackoff:    defaultBlobRetryBackoff,
		quiet:               flagQuiet,
		out:                 cmd.OutOrStdout(),
		errOut:              errOut,
		spinner:             spinner,
	}
	return copyLogsFromCloud(ctx, client, newBlobHTTPClient(), cfg)
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

		ctx, stop := signal.NotifyContext(cmd.Context(), os.Interrupt)
		defer stop()
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
	logsCpCmd.Flags().BoolVarP(&flagQuiet, "quiet", "q", false, "Suppress progress output and the best-effort pipeline reminder message")
	logsCpCmd.MarkFlagRequired("context")

	logsCpCmd.MarkFlagsRequiredTogether("historic", "historic_start_timestamp", "historic_end_timestamp")
	logsCpCmd.MarkFlagsMutuallyExclusive("historic", "lookback")
	logsCpCmd.MarkFlagsMutuallyExclusive("lookback", "recording_id")
}
