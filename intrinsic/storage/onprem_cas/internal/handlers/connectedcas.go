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

package handlers

import (
	"context"

	"log/slog"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

// Upstream defines the interface for interacting with an upstream CAS service.
// All methods accept the object digest directly (without schema prefix).
// The Implements* methods provide quick access to whether the given methods
// are available on Upstream.
type Upstream interface {
	Download(ctx context.Context, digest string) error
	ImplementsDownload() bool
	Stat(ctx context.Context, digest string) (*caspb.StatResponse, error)
	ImplementsStat() bool
	Upload(ctx context.Context, digest string) error
	ImplementsUpload() bool
}

// ConnectedCASHandler handles CAS requests in a connected deployment,
// supporting block-download-on-demand, stat, and upload from upstream.
type ConnectedCASHandler struct {
	caspb.UnimplementedContentAddressableStorageServiceServer
	LocalCAS         *LocalCASHandler
	Upstream         Upstream
	AllowLocalDelete bool
}

// Get tries to serve from local CAS first. If not found and Upstream supports download,
// it requests an upstream download, waits for it, and retries local retrieval.
func (h *ConnectedCASHandler) Get(req *caspb.GetRequest, stream caspb.ContentAddressableStorageService_GetServer) error {
	ctx := stream.Context()
	err := h.LocalCAS.Get(req, stream)
	if err == nil {
		return nil
	}
	if status.Code(err) != codes.NotFound || h.Upstream == nil || !h.Upstream.ImplementsDownload() {
		return err
	}

	oid := req.GetObjectId()
	digest, err := idhandling.StripSchema(oid)
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "invalid object ID: %v", err)
	}

	slog.Info("ConnectedCAS: Object not found locally, requesting download from upstream", slog.String("digest", digest))
	if dlErr := h.Upstream.Download(ctx, digest); dlErr != nil {
		slog.Warn("ConnectedCAS: Upstream download failed", slog.String("digest", digest), slog.Any("error", dlErr))
		if ctx.Err() != nil {
			return status.FromContextError(ctx.Err()).Err()
		}
		return dlErr
	}

	// Download succeeded! Retry serving from local CAS again
	slog.Info("ConnectedCAS: Download completed, retrying local CAS read", slog.String("digest", digest))
	return h.LocalCAS.Get(req, stream)
}

// createRecorder wraps the Create server stream so ConnectedCASHandler can capture
// the CreateResponse returned via SendAndClose by LocalCASHandler.
// Because CAS object IDs are computed from content checksums during local streaming
// upload, ConnectedCASHandler intercepts SendAndClose to capture the response,
// performs the upstream upload, and forwards SendAndClose(resp) to the client
// only after upstream upload succeeds.
type createRecorder struct {
	caspb.ContentAddressableStorageService_CreateServer
	resp *caspb.CreateResponse
}

func (r *createRecorder) SendAndClose(resp *caspb.CreateResponse) error {
	r.resp = resp
	return nil
}

// Create delegates local creation straight to the LocalCAS handler, then uploads to upstream if configured.
func (h *ConnectedCASHandler) Create(stream caspb.ContentAddressableStorageService_CreateServer) error {
	if h.Upstream == nil || !h.Upstream.ImplementsUpload() {
		return h.LocalCAS.Create(stream)
	}

	rec := &createRecorder{ContentAddressableStorageService_CreateServer: stream}
	if err := h.LocalCAS.Create(rec); err != nil {
		return err
	}
	if rec.resp == nil {
		return status.Error(codes.Internal, "local Create completed without response")
	}

	oid := rec.resp.GetObjectId()
	digest, err := idhandling.StripSchema(oid)
	if err != nil {
		return status.Errorf(codes.Internal, "invalid created object ID %q: %v", oid, err)
	}

	slog.Info("ConnectedCAS: Object created locally, uploading to upstream", slog.String("digest", digest))
	if err := h.Upstream.Upload(stream.Context(), digest); err != nil {
		slog.Warn("ConnectedCAS: Upstream upload failed", slog.String("digest", digest), slog.Any("error", err))
		if stream.Context().Err() != nil {
			return status.FromContextError(stream.Context().Err()).Err()
		}
		return err
	}

	slog.Info("ConnectedCAS: Upstream upload completed", slog.String("digest", digest))
	return stream.SendAndClose(rec.resp)
}

// StartResumableWrite delegates to the LocalCAS handler.
func (h *ConnectedCASHandler) StartResumableWrite(ctx context.Context, req *caspb.StartResumableWriteRequest) (*caspb.StartResumableWriteResponse, error) {
	return h.LocalCAS.StartResumableWrite(ctx, req)
}

// QueryWriteStatus delegates to the LocalCAS handler. An upload session is finalized only after
// the upstream upload succeeded, so it doesn't report an object ID before that.
func (h *ConnectedCASHandler) QueryWriteStatus(ctx context.Context, req *caspb.QueryWriteStatusRequest) (*caspb.QueryWriteStatusResponse, error) {
	return h.LocalCAS.QueryWriteStatus(ctx, req)
}

// WriteObject delegates to the LocalCAS handler. If Upstream supports upload, finalizing an upload
// also uploads the object to upstream, like Create, and the upload session is marked as
// finalized only after the upstream upload succeeded. If the upstream upload fails, the session
// keeps all bytes persisted, and the client can finalize it again, which retries the upstream
// upload.
func (h *ConnectedCASHandler) WriteObject(stream caspb.ContentAddressableStorageService_WriteObjectServer) error {
	if h.Upstream == nil || !h.Upstream.ImplementsUpload() {
		return h.LocalCAS.WriteObject(stream)
	}
	return h.LocalCAS.writeObject(stream, func(ctx context.Context, digest string) error {
		slog.Info("ConnectedCAS: Object finalized locally, uploading to upstream", slog.String("digest", digest))
		if err := h.Upstream.Upload(ctx, digest); err != nil {
			slog.Warn("ConnectedCAS: Upstream upload failed", slog.String("digest", digest), slog.Any("error", err))
			if ctx.Err() != nil {
				return status.FromContextError(ctx.Err()).Err()
			}
			return err
		}
		slog.Info("ConnectedCAS: Upstream upload completed", slog.String("digest", digest))
		return nil
	})
}

// Delete deletes an object from local CAS if AllowLocalDelete is true.
// Otherwise, it returns unimplemented as ConnectedCAS does not support deletion by default.
func (h *ConnectedCASHandler) Delete(ctx context.Context, req *caspb.DeleteRequest) (*caspb.DeleteResponse, error) {
	if h.AllowLocalDelete {
		return h.LocalCAS.Delete(ctx, req)
	}
	return nil, status.Error(codes.Unimplemented, "Delete is not supported in ConnectedCAS")
}

// Stat tries to serve from local CAS first. If not found and Upstream supports stat,
// it requests an upstream stat.
func (h *ConnectedCASHandler) Stat(ctx context.Context, req *caspb.StatRequest) (*caspb.StatResponse, error) {
	resp, err := h.LocalCAS.Stat(ctx, req)
	if err == nil {
		return resp, nil
	}
	if status.Code(err) != codes.NotFound || h.Upstream == nil || !h.Upstream.ImplementsStat() {
		return resp, err
	}

	oid := req.GetObjectId()
	digest, err := idhandling.StripSchema(oid)
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid object ID: %v", err)
	}

	slog.Info("ConnectedCAS: Object not found locally, requesting stat from upstream", slog.String("digest", digest))
	resp, statErr := h.Upstream.Stat(ctx, digest)
	if statErr != nil {
		slog.Warn("ConnectedCAS: Upstream stat failed", slog.String("digest", digest), slog.Any("error", statErr))
		if ctx.Err() != nil {
			return nil, status.FromContextError(ctx.Err()).Err()
		}
		return nil, statErr
	}

	slog.Info("ConnectedCAS: Upstream stat completed", slog.String("digest", digest))
	return resp, nil
}
