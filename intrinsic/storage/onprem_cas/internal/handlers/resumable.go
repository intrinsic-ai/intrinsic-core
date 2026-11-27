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
	"encoding"
	"encoding/hex"
	"errors"
	"hash"
	"hash/crc32"
	"io"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	"intrinsic/storage/onprem_cas/internal/sessions"

	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

// The resumable upload API (StartResumableWrite, WriteObject, QueryWriteStatus) follows the same
// contract as the cloud CAS, see cas_service.proto. Differences in the implementation:
//   - A session is a single partial file plus a metadata file in the sessions directory (see
//     sessions.Store). On finalize, the session is first marked as committed, then the partial file
//     is linked to its final destination, like in Create. A committed session accepts no more
//     data, because the partial file and the object may be the same file.
//   - A flush syncs the partial file and then atomically replaces the metadata file.
//   - Instead of a lease, an in-process lock makes sure that only one RPC at a time works on a
//     session. RPCs wait for the lock for a short time before they fail with Aborted.
//   - Sessions survive restarts of the CAS service (and IPC reboots when partial_dir is on
//     persistent storage).

// StartResumableWrite starts a resumable upload session.
func (h *LocalCASHandler) StartResumableWrite(ctx context.Context, req *caspb.StartResumableWriteRequest) (*caspb.StartResumableWriteResponse, error) {
	if req.ObjectSize != nil && req.GetObjectSize() < 0 {
		return nil, statusErrorf(codes.InvalidArgument, "object size must be non-negative, got %d", req.GetObjectSize())
	}
	if err := os.MkdirAll(h.sessions.Dir(), 0755); err != nil {
		return nil, toGRPCError(err, "failed to create upload sessions directory")
	}

	uploadID := h.uuidNew()
	id := sessions.ServerID(uploadID)
	if err := h.createSession(id, req.ObjectSize); err != nil {
		return nil, err
	}
	if req.ObjectSize != nil {
		slog.Info("Local CAS: StartResumableWrite", slog.String("session", id), slog.Int64("object_size", req.GetObjectSize()))
	} else {
		slog.Info("Local CAS: StartResumableWrite", slog.String("session", id))
	}
	return &caspb.StartResumableWriteResponse{UploadId: uploadID}, nil
}

func (h *LocalCASHandler) createSession(id string, objectSize *int64) (err error) {
	f, err := os.OpenFile(h.sessions.DataPath(id), os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0644)
	if err != nil {
		return toGRPCError(err, "failed to create upload session file")
	}
	defer func() {
		if err != nil {
			h.sessions.Remove(id)
		}
	}()
	if objectSize != nil && *objectSize > 0 {
		if err := sessions.Preallocate(f, *objectSize); err != nil {
			_ = f.Close()
			return toGRPCError(err, "failed to reserve %d bytes of disk space", *objectSize)
		}
	}
	if err := f.Close(); err != nil {
		return toGRPCError(err, "failed to close upload session file")
	}
	return writeSessionMeta(h.sessions, id, &sessions.Meta{ObjectSize: objectSize})
}

func (h *LocalCASHandler) acquireSession(ctx context.Context, id string) (func(), error) {
	release, err := h.sessions.Acquire(ctx, id)
	if err == nil {
		return release, nil
	}
	if errors.Is(err, sessions.ErrBusy) {
		return nil, statusErrorf(codes.Aborted, "%v", err)
	}
	if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
		return nil, status.FromContextError(err).Err()
	}
	return nil, toGRPCError(err, "failed to acquire upload session")
}

func readSessionMeta(store *sessions.Store, id string) (*sessions.Meta, error) {
	m, err := store.ReadMeta(id)
	if err == nil {
		return m, nil
	}
	if errors.Is(err, sessions.ErrNotFound) {
		return nil, status.Error(codes.NotFound, "upload session not found or expired, call StartResumableWrite first")
	}
	return nil, toGRPCError(err, "failed to read upload session")
}

func writeSessionMeta(store *sessions.Store, id string, m *sessions.Meta) error {
	if err := store.WriteMeta(id, m); err != nil {
		return toGRPCError(err, "failed to save upload session")
	}
	return nil
}

// QueryWriteStatus returns the persisted size of an upload session and, if it was finalized, the
// object ID.
func (h *LocalCASHandler) QueryWriteStatus(ctx context.Context, req *caspb.QueryWriteStatusRequest) (*caspb.QueryWriteStatusResponse, error) {
	uploadID := req.GetUploadId()
	if uploadID == "" {
		return nil, statusErrorf(codes.InvalidArgument, "upload_id must be provided")
	}
	id := sessions.ServerID(uploadID)
	// Waiting for the lock makes sure that a stream that is still persisting the received bytes
	// has finished, so that the returned size is final.
	release, err := h.acquireSession(ctx, id)
	if err != nil {
		return nil, err
	}
	defer release()

	m, err := readSessionMeta(h.sessions, id)
	if err != nil {
		return nil, err
	}
	return &caspb.QueryWriteStatusResponse{
		PersistedSize: m.PersistedSize,
		ObjectId:      m.FinalObjectID,
	}, nil
}

// WriteObject streams data chunks for an upload session, persists them on flush (or stream end),
// and finalizes the object when a WriteObjectRequest sets finish_write = true.
// See WriteObject in cas_service.proto for the full protocol contract.
func (h *LocalCASHandler) WriteObject(stream caspb.ContentAddressableStorageService_WriteObjectServer) error {
	return h.writeObject(stream, nil)
}

// afterCommitFunc is a hook used by ConnectedCASHandler to upload the file to the upstream CAS
// once the upload to local CAS is finished (after the object is committed to objectsDir, and
// before FinalObjectID is written to session metadata). Running it before marking the session
// finalized ensures QueryWriteStatus does not report an object ID if the upstream upload fails:
// the session stays committed with all bytes persisted, and the client can retry with an empty
// finish_write, which invokes afterCommit again.
type afterCommitFunc func(ctx context.Context, digest string) error

func (h *LocalCASHandler) writeObject(stream caspb.ContentAddressableStorageService_WriteObjectServer, afterCommit afterCommitFunc) error {
	ctx := stream.Context()
	req, err := stream.Recv()
	if errors.Is(err, io.EOF) {
		return statusErrorf(codes.InvalidArgument, "stream was closed before sending any data")
	}
	if err != nil {
		return status.Errorf(status.Code(err), "stream was cancelled before the first message arrived: %v", err)
	}

	uploadID := req.GetUploadId()
	if uploadID == "" {
		return statusErrorf(codes.InvalidArgument, "upload_id must be provided")
	}
	if req.WriteOffset == nil {
		return statusErrorf(codes.InvalidArgument, "write_offset must be provided")
	}
	if offset := req.GetWriteOffset(); offset < 0 {
		return statusErrorf(codes.InvalidArgument, "write_offset must be non-negative, got %d", offset)
	}

	id := sessions.ServerID(uploadID)
	release, err := h.acquireSession(ctx, id)
	if err != nil {
		return err
	}
	defer release()
	// If the client cancelled the stream while it waited for the session, gRPC may still deliver
	// requests that it buffered before. Don't process them: a QueryWriteStatus may already have
	// returned the persisted size, and the client resumes from there.
	if err := ctx.Err(); err != nil {
		return status.FromContextError(err).Err()
	}

	u, err := h.openUpload(ctx, id, req.GetWriteOffset())
	if err != nil {
		return err
	}
	defer u.close()

	err = h.receive(stream, u, req, afterCommit)
	if !u.finalized {
		// Persist all bytes received on this stream, also if it failed, so that the client can
		// resume from there.
		if persistErr := u.persist(); persistErr != nil && err == nil {
			err = persistErr
		}
	}
	return err
}

// receive processes req and all following requests of the stream.
func (h *LocalCASHandler) receive(stream caspb.ContentAddressableStorageService_WriteObjectServer, u *upload, req *caspb.WriteObjectRequest, afterCommit afterCommitFunc) error {
	for {
		if err := u.write(req.GetChecksummedData()); err != nil {
			return err
		}
		if req.GetFinishWrite() {
			return h.finalize(stream, u, afterCommit)
		}
		if req.GetFlush() {
			if err := u.persist(); err != nil {
				return err
			}
			if err := stream.Send(&caspb.WriteObjectResponse{PersistedSize: u.meta.PersistedSize}); err != nil {
				return status.Errorf(status.Code(err), "failed to send flush acknowledgement: %v", err)
			}
		}

		var err error
		req, err = stream.Recv()
		if errors.Is(err, io.EOF) {
			// The client half-closed the stream without finishing the upload.
			if err := u.persist(); err != nil {
				return err
			}
			if err := stream.Send(&caspb.WriteObjectResponse{PersistedSize: u.meta.PersistedSize}); err != nil {
				return status.Errorf(status.Code(err), "failed to send response: %v", err)
			}
			return nil
		}
		if err != nil {
			slog.Info("Local CAS: WriteObject stream ended with error", slog.String("session", u.id), slog.Int64("received_size", u.written), slog.Any("error", err))
			return status.Errorf(status.Code(err), "error receiving chunk: %v", err)
		}
	}
}

// finalize commits the uploaded object and marks the session as finalized.
//
// Before the data file is linked to the object, the session is marked as committed. A committed
// session accepts no more data, because the data file may be the object itself. If finalizing
// fails after that, e.g. because afterCommit failed, finalizing again only repeats the steps after
// marking.
func (h *LocalCASHandler) finalize(stream caspb.ContentAddressableStorageService_WriteObjectServer, u *upload, afterCommit afterCommitFunc) error {
	if size := u.meta.ObjectSize; size != nil && u.written != *size {
		return statusErrorf(codes.InvalidArgument, "object size %d doesn't match expected size %d", u.written, *size)
	}
	if err := u.persist(); err != nil {
		return err
	}
	if u.f != nil {
		err := u.f.Close()
		u.f = nil
		if err != nil {
			return toGRPCError(err, "failed to close upload session file")
		}
	}
	if u.meta.CommittedDigest == "" {
		digest := hex.EncodeToString(u.hasher.Sum(nil))
		if err := u.updateMeta(func(m *sessions.Meta) { m.CommittedDigest = digest }); err != nil {
			return err
		}
	}

	digest := u.meta.CommittedDigest
	dataPath := h.sessions.DataPath(u.id)
	finalPath, existed, err := h.commitSession(dataPath, digest, u.written)
	if err != nil {
		return err
	}
	if afterCommit != nil {
		if err := afterCommit(stream.Context(), digest); err != nil {
			return err
		}
	}
	objectID, err := idhandling.AddSchema(digest)
	if err != nil {
		return statusErrorf(codes.Internal, "invalid result digest: %v", err)
	}

	if err := u.updateMeta(func(m *sessions.Meta) { m.FinalObjectID = objectID }); err != nil {
		return err
	}
	u.finalized = true
	// The object holds the data now. The session keeps only its metadata until it expires, so
	// that QueryWriteStatus can return the object ID.
	if err := os.Remove(dataPath); err != nil && !errors.Is(err, fs.ErrNotExist) {
		slog.Warn("Failed to remove upload session file", slog.String("path", dataPath), slog.Any("error", err))
	}
	slog.Info("Local CAS: WriteObject finalized", slog.String("session", u.id), slog.String("digest", digest),
		slog.Int64("size", u.written), slog.String("path", finalPath), slog.Bool("existed", existed))

	if err := stream.Send(&caspb.WriteObjectResponse{
		ObjectId:      objectID,
		PersistedSize: u.meta.PersistedSize,
	}); err != nil {
		return status.Errorf(status.Code(err), "failed to send final response: %v", err)
	}
	return nil
}

// commitSession makes the data file of a committed session the object with the given digest and
// syncs objectsDir, so that the object is durable before the session is marked as finalized.
func (h *LocalCASHandler) commitSession(dataPath, digest string, size int64) (string, bool, error) {
	var finalPath string
	var existed bool
	if _, err := os.Stat(dataPath); err == nil {
		finalPath, existed, err = h.commitObject(dataPath, digest, uint64(size))
		if err != nil {
			return "", false, err
		}
	} else if errors.Is(err, fs.ErrNotExist) {
		// An earlier attempt replaced a corrupt object with the data file (see commitObject). The
		// object is intact if it has the expected size.
		finalPath, existed = filepath.Join(h.objectsDir, digest), true
		fi, err := os.Stat(finalPath)
		if errors.Is(err, fs.ErrNotExist) || (err == nil && fi.Size() != size) {
			return "", false, statusErrorf(codes.NotFound, "data of upload session for object %s is missing", digest)
		}
		if err != nil {
			return "", false, toGRPCError(err, "failed to stat committed object file")
		}
	} else {
		return "", false, toGRPCError(err, "failed to stat upload session file")
	}
	if err := sessions.SyncDir(h.objectsDir); err != nil {
		return "", false, toGRPCError(err, "failed to sync objects directory")
	}
	return finalPath, existed, nil
}

// upload is the state of an upload session while a WriteObject stream works on it.
type upload struct {
	store  *sessions.Store
	id     string
	meta   *sessions.Meta
	f      *os.File
	hasher hash.Hash
	// written is the number of bytes in the data file that were received successfully.
	written   int64
	finalized bool
}

// openUpload opens the session id for a stream that starts writing at offset.
func (h *LocalCASHandler) openUpload(ctx context.Context, id string, offset int64) (*upload, error) {
	m, err := readSessionMeta(h.sessions, id)
	if err != nil {
		return nil, err
	}
	if m.FinalObjectID != "" {
		return nil, statusErrorf(codes.AlreadyExists, "upload session is already finalized as %q", m.FinalObjectID)
	}
	if offset != m.PersistedSize {
		return nil, statusErrorf(codes.InvalidArgument, "write_offset %d must be equal to the persisted size %d", offset, m.PersistedSize)
	}
	if m.CommittedDigest != "" {
		// The data file may be the object itself; it must not be opened for writing. The
		// session can only be finalized, which needs only the digest.
		return &upload{store: h.sessions, id: id, meta: m, written: m.PersistedSize}, nil
	}

	f, err := os.OpenFile(h.sessions.DataPath(id), os.O_RDWR, 0)
	if errors.Is(err, fs.ErrNotExist) {
		return nil, statusErrorf(codes.NotFound, "data of upload session %s is missing", id)
	}
	if err != nil {
		return nil, toGRPCError(err, "failed to open upload session file")
	}
	u := &upload{store: h.sessions, id: id, meta: m, f: f, written: m.PersistedSize}
	if err := u.prepare(ctx); err != nil {
		_ = f.Close()
		return nil, err
	}
	return u, nil
}

// prepare positions the data file at the persisted size and restores the hasher.
func (u *upload) prepare(ctx context.Context) error {
	fi, err := u.f.Stat()
	if err != nil {
		return toGRPCError(err, "failed to stat upload session file")
	}
	switch p := u.meta.PersistedSize; {
	case fi.Size() < p:
		return statusErrorf(codes.NotFound, "data of upload session %s has %d bytes, less than the persisted size %d", u.id, fi.Size(), p)
	case fi.Size() > p:
		// The bytes after the last flush, e.g. from before a crash, are not persisted. The client
		// sends them again.
		if err := u.f.Truncate(p); err != nil {
			return toGRPCError(err, "failed to truncate upload session file")
		}
		if size := u.meta.ObjectSize; size != nil && *size > 0 {
			if err := sessions.Preallocate(u.f, *size); err != nil {
				return toGRPCError(err, "failed to reserve %d bytes of disk space", *size)
			}
		}
	}
	if _, err := u.f.Seek(u.meta.PersistedSize, io.SeekStart); err != nil {
		return toGRPCError(err, "failed to seek upload session file")
	}

	u.hasher = idhandling.NewHasher()
	if len(u.meta.SHA512State) > 0 {
		err := u.hasher.(encoding.BinaryUnmarshaler).UnmarshalBinary(u.meta.SHA512State)
		if err == nil {
			return nil
		}
		slog.Warn("Failed to restore hash state of upload session; hashing the persisted data again", slog.String("session", u.id), slog.Any("error", err))
		u.hasher = idhandling.NewHasher()
	}
	if u.meta.PersistedSize > 0 {
		r := io.NewSectionReader(u.f, 0, u.meta.PersistedSize)
		buf := make([]byte, defaultChunkSize)
		var hashed int64
		for {
			if err := ctx.Err(); err != nil {
				return status.FromContextError(err).Err()
			}
			n, err := r.Read(buf)
			if n > 0 {
				_, _ = u.hasher.Write(buf[:n])
				hashed += int64(n)
			}
			if errors.Is(err, io.EOF) {
				break
			}
			if err != nil {
				return toGRPCError(err, "failed to hash upload session file")
			}
		}
		if hashed != u.meta.PersistedSize {
			return statusErrorf(codes.NotFound, "data of upload session %s has %d bytes, less than the persisted size %d", u.id, hashed, u.meta.PersistedSize)
		}
	}
	return nil
}

// write validates data and appends it to the data file.
func (u *upload) write(data *caspb.ChecksummedData) error {
	content := data.GetContent()
	total := u.written + int64(len(content))
	if size := u.meta.ObjectSize; size != nil && total > *size {
		return statusErrorf(codes.InvalidArgument, "object size %d exceeds expected size %d", total, *size)
	}
	if data != nil && data.Crc32C != nil {
		if crc := crc32.Checksum(content, crc32Castagnoli); crc != data.GetCrc32C() {
			return statusErrorf(codes.DataLoss, "checksum mismatch, computed 0x%08x, client provided 0x%08x", crc, data.GetCrc32C())
		}
	}
	if len(content) == 0 {
		return nil
	}
	if u.meta.CommittedDigest != "" {
		return statusErrorf(codes.InvalidArgument, "upload session was committed with %d bytes and accepts no more data, only finish_write", u.written)
	}
	if _, err := u.f.Write(content); err != nil {
		return toGRPCError(err, "failed to write to upload session file")
	}
	_, _ = u.hasher.Write(content)
	u.written = total
	return nil
}

// persist syncs all received bytes to disk and records them in the metadata.
func (u *upload) persist() error {
	if u.written == u.meta.PersistedSize {
		return nil
	}
	if err := u.f.Sync(); err != nil {
		return toGRPCError(err, "failed to sync upload session file")
	}
	state, err := u.hasher.(encoding.BinaryMarshaler).MarshalBinary()
	if err != nil {
		return statusErrorf(codes.Internal, "failed to save hash state: %v", err)
	}
	return u.updateMeta(func(m *sessions.Meta) {
		m.PersistedSize = u.written
		m.SHA512State = state
	})
}

// updateMeta applies fn to a copy of u.meta and writes it to disk, updating u.meta only on
// success so failed writes leave the in-memory state unchanged.
func (u *upload) updateMeta(fn func(*sessions.Meta)) error {
	m := *u.meta
	fn(&m)
	if err := writeSessionMeta(u.store, u.id, &m); err != nil {
		return err
	}
	u.meta = &m
	return nil
}

func (u *upload) close() {
	if u.f == nil {
		return
	}
	if err := u.f.Close(); err != nil {
		slog.Warn("Failed to close upload session file", slog.String("session", u.id), slog.Any("error", err))
	}
}
