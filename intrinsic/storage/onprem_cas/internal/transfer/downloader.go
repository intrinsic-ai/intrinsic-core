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

package transfer

import (
	"context"
	"fmt"
	"hash"
	"io"
	"time"

	"intrinsic/storage/content_addressable_storage/pkg/clienthelpers"
	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"

	"log/slog"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
)

// DownloadResult represents the outcome of a successful download operation.
type DownloadResult struct {
	// Digest is the CAS digest (SHA-512) of the downloaded object.
	Digest string
	// Err is set if the async operation failed.
	Err error
}

// Downloader manages the concurrent fetching of objects from an upstream CAS service
// to a local CAS service.
type Downloader struct {
	upstreamClient caspb.ContentAddressableStorageServiceClient
	localClient    caspb.ContentAddressableStorageServiceClient
	dualStatter    *dualStatter
	timeout        Timeout
}

// NewDownloader creates a new downloader using the provided CAS clients.
func NewDownloader(
	upstreamClient caspb.ContentAddressableStorageServiceClient,
	localClient caspb.ContentAddressableStorageServiceClient,
	dualStatter *dualStatter,
	cfg Timeout,
) *Downloader {
	if cfg.MinSpeedBps == 0 {
		cfg.MinSpeedBps = 1
	}
	return &Downloader{
		upstreamClient: upstreamClient,
		localClient:    localClient,
		dualStatter:    dualStatter,
		timeout:        cfg,
	}
}

// Download retrieves the object associated with the given digest from the upstream CAS service.
// If the object already exists locally, it returns immediately.
func (d *Downloader) Download(ctx context.Context, digest string) (*DownloadResult, error) {
	objectID, err := idhandling.AddSchema(digest) // performs also validation
	if err != nil {
		return nil, fmt.Errorf("invalid digest: %w", err)
	}

	// Determine size and upstream/local status of the object.
	statRes, err := d.dualStatter.stat(ctx, digest)
	if err != nil {
		return nil, fmt.Errorf("stat failed: %w", err)
	}

	if statRes.Locally {
		return &DownloadResult{Digest: digest}, nil
	}

	if !statRes.Upstream {
		return nil, status.Errorf(codes.NotFound, "object %q not found upstream", digest)
	}

	recordAttempt(ctx, OpDownload)

	trackActiveOperations(ctx, OpDownload, 1)
	defer trackActiveOperations(ctx, OpDownload, -1)

	start := time.Now()

	written, err := d.downloadImpl(ctx, objectID, statRes.Size)
	duration := time.Since(start)
	if err != nil {
		recordFailed(ctx, OpDownload)
		slog.Error("Download failed", slog.String("digest", digest), slog.Int64("duration_millis", duration.Milliseconds()), slog.Uint64("bytes_transferred", written), slog.Any("error", err))
		return nil, err
	}

	recordTransferredBytes(ctx, OpDownload, int64(written))
	recordCompleted(ctx, OpDownload)
	slog.Info("Download succeeded", slog.String("digest", digest), slog.Int64("duration_millis", duration.Milliseconds()), slog.Uint64("bytes_transferred", written))
	return &DownloadResult{Digest: digest}, nil
}

func (d *Downloader) downloadImpl(ctx context.Context, objectID string, size uint64) (uint64, error) {
	timeout := d.timeout.Effective(size)

	runCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	createStream, err := d.localClient.Create(runCtx)
	if err != nil {
		return 0, fmt.Errorf("local create failed: %w", err)
	}

	r, err := clienthelpers.GetResumableReader(
		runCtx,
		objectID,
		clienthelpers.WithCASClient(d.upstreamClient),
		clienthelpers.WithInfiniteRetries(),
		clienthelpers.WithRetryBackoff(100*time.Millisecond, 5*time.Second),
	)
	if err != nil {
		return 0, fmt.Errorf("creating resumable reader failed: %w", err)
	}
	defer r.Close()

	writer := newCASStreamWriter(createStream)

	if _, err = io.Copy(writer, r); err != nil {
		recordWastedBytes(ctx, OpDownload, int64(writer.written))
		return writer.written, fmt.Errorf("upstream resumable download failed: %w", err)
	}

	res, err := createStream.CloseAndRecv()
	if err != nil {
		return writer.written, fmt.Errorf("local close and recv failed: %w", err)
	}
	if res.GetObjectId() != objectID {
		return writer.written, fmt.Errorf("hash mismatch: got %q, want %q", res.GetObjectId(), objectID)
	}

	return writer.written, nil
}

type casStreamWriter struct {
	stream      caspb.ContentAddressableStorageService_CreateClient
	checksummer hash.Hash32
	written     uint64
}

func newCASStreamWriter(stream caspb.ContentAddressableStorageService_CreateClient) *casStreamWriter {
	return &casStreamWriter{
		stream:      stream,
		checksummer: clienthelpers.NewChecksummer(),
	}
}

func (w *casStreamWriter) Write(p []byte) (int, error) {
	w.checksummer.Reset()
	if _, err := w.checksummer.Write(p); err != nil {
		return 0, fmt.Errorf("checksumming chunk: %w", err)
	}

	req := &caspb.CreateRequest{
		ChecksummedData: &caspb.ChecksummedData{
			Content: p,
			Crc32C:  proto.Uint32(w.checksummer.Sum32()),
		},
	}
	if err := w.stream.Send(req); err != nil {
		return 0, fmt.Errorf("sending to local CAS: %w", w.stream.RecvMsg(nil))
	}

	w.written += uint64(len(p))
	return len(p), nil
}
