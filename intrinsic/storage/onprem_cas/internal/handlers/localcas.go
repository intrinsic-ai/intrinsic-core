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
	"encoding/hex"
	"errors"
	"fmt"
	"hash/crc32"
	"io"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"syscall"

	"github.com/google/uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	"intrinsic/storage/content_addressable_storage/pkg/idhandling"
	"intrinsic/storage/onprem_cas/internal"

	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

var (
	crc32Castagnoli = crc32.MakeTable(crc32.Castagnoli)
)

const defaultChunkSize = 1024 * 1024 // 1MB

// LocalCASHandler handles CAS requests targeting only the local filesystem.
type LocalCASHandler struct {
	caspb.UnimplementedContentAddressableStorageServiceServer
	objectsDir string
	partialDir string
	uuidNew    func() string // for unit-testing
	chunkSize  int           // for unit-testing
}

// NewLocalCASHandler creates a new LocalCASHandler.
func NewLocalCASHandler(objectsDir, partialDir string) *LocalCASHandler {
	return &LocalCASHandler{
		objectsDir: objectsDir,
		partialDir: partialDir,
		uuidNew:    uuid.NewString,
		chunkSize:  defaultChunkSize,
	}
}

// Get streams the CAS object to the client if it exists locally.
func (h *LocalCASHandler) Get(req *caspb.GetRequest, stream caspb.ContentAddressableStorageService_GetServer) error {
	digest, err := idhandling.StripSchema(req.GetObjectId())
	if err != nil {
		return statusErrorf(codes.InvalidArgument, "invalid object ID: %v", err)
	}

	path := filepath.Join(h.objectsDir, digest)
	f, err := os.Open(path)
	if errors.Is(err, fs.ErrNotExist) { // NOT FOUND
		return statusErrorf(codes.NotFound, "object %q not found", req.GetObjectId())
	}
	if err != nil { // other errors
		return statusErrorf(codes.Internal, "failed to open object: %v", err)
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return statusErrorf(codes.Internal, "failed to stat object: %v", err)
	}
	if err := idhandling.ValidateObjectSize(digest, fi.Size()); err != nil {
		slog.Error("Corrupt local CAS object size detected during Get, removing",
			slog.String("digest", digest),
			slog.Int64("size", fi.Size()),
			slog.Any("error", err))
		_ = f.Close()
		if removeErr := os.Remove(path); removeErr != nil && !errors.Is(removeErr, fs.ErrNotExist) {
			slog.Warn("Failed to remove corrupt object", slog.String("path", path), slog.Any("error", removeErr))
		}
		return statusErrorf(codes.NotFound, "object %q is corrupt and was removed: %v", req.GetObjectId(), err)
	}

	for {
		// allocate a buffer per chunk in case grpc or an interceptor
		// accesses the chunk after stream.Send returned
		buf := make([]byte, h.chunkSize)
		n, err := io.ReadFull(f, buf)
		if n == 0 { // EOF
			break
		}
		if err != nil && !errors.Is(err, io.ErrUnexpectedEOF) {
			return statusErrorf(codes.Internal, "failed to read object: %v", err)
		}
		chunk := buf[:n]
		res := &caspb.GetResponse{
			ChecksummedData: &caspb.ChecksummedData{
				Content: chunk,
				Crc32C:  proto.Uint32(crc32.Checksum(chunk, crc32Castagnoli)),
			},
		}
		if err := stream.Send(res); err != nil {
			return statusErrorf(status.Code(err), "failed to send response: %v", err)
		}
		if errors.Is(err, io.ErrUnexpectedEOF) {
			break
		}
	}
	slog.Debug("Local CAS: Get", slog.String("digest", digest), slog.String("path", path))

	return nil
}

// Create handles local CAS object uploads.
func (h *LocalCASHandler) Create(stream caspb.ContentAddressableStorageService_CreateServer) error {
	uploadID := h.uuidNew()
	partialName := uploadID + internal.PartialExt
	partialPath := filepath.Join(h.partialDir, partialName)

	f, err := os.Create(partialPath)
	if err != nil {
		return toGRPCError(err, "failed to create partial file")
	}
	defer func() {
		_ = f.Close()
		if err := os.Remove(partialPath); err != nil && !errors.Is(err, fs.ErrNotExist) {
			slog.Warn("Failed to remove partial file", slog.String("path", partialPath), slog.Any("error", err))
		}
	}()

	hasher := idhandling.NewHasher()
	var totalSize uint64

	for {
		req, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return statusErrorf(codes.Internal, "stream error: %v", err)
		}

		checksummedData := req.GetChecksummedData()
		if checksummedData == nil {
			return statusErrorf(codes.InvalidArgument, "missing checksummed data")
		}
		data := checksummedData.GetContent()
		if clientCRC := checksummedData.Crc32C; clientCRC != nil {
			if crc32.Checksum(data, crc32Castagnoli) != *clientCRC {
				return statusErrorf(codes.DataLoss, "checksum mismatch")
			}
		}

		n, err := f.Write(data)
		if err != nil {
			return toGRPCError(err, "failed to write to partial file")
		}
		totalSize += uint64(n)
		hasher.Write(data)
	}

	digest := hex.EncodeToString(hasher.Sum(nil))
	finalPath := filepath.Join(h.objectsDir, digest)

	// commit the changes to disk and close the file handler
	if err := f.Sync(); err != nil {
		return toGRPCError(err, "failed to sync partial file")
	}
	if err := f.Close(); err != nil {
		return toGRPCError(err, "failed to close partial file")
	}

	slog.Debug("Linking partial file to final destination", slog.String("digest", digest),
		slog.String("path_partial", partialPath), slog.String("path", finalPath))

	// Link to final destination. If it already exists, check if it is corrupt or size-mismatched.
	err = os.Link(partialPath, finalPath)
	if errors.Is(err, fs.ErrExist) {
		existingFi, statErr := os.Stat(finalPath)
		if statErr != nil || existingFi.Size() != int64(totalSize) || idhandling.ValidateObjectSize(digest, existingFi.Size()) != nil {
			slog.Warn("Existing object is corrupt or size mismatch; replacing with newly written file",
				slog.String("digest", digest),
				slog.Uint64("new_size", totalSize))
			if renameErr := os.Rename(partialPath, finalPath); renameErr != nil {
				return toGRPCError(renameErr, "failed to replace corrupt existing file")
			}
		}
	} else if err != nil {
		return toGRPCError(err, "failed to link partial file to final destination")
	}
	slog.Info("Local CAS: Create", slog.String("digest", digest), slog.Uint64("size", totalSize),
		slog.String("path", finalPath), slog.Bool("existed", errors.Is(err, fs.ErrExist)))

	objectID, err := idhandling.AddSchema(digest)
	if err != nil {
		return statusErrorf(codes.Internal, "invalid result digest: %v", err)
	}
	if err := stream.SendAndClose(&caspb.CreateResponse{
		ObjectId: objectID,
	}); err != nil {
		return statusErrorf(status.Code(err), "failed to send response: %v", err)
	}
	return nil
}

// Delete removes a local CAS object.
func (h *LocalCASHandler) Delete(ctx context.Context, req *caspb.DeleteRequest) (*caspb.DeleteResponse, error) {
	digest, err := idhandling.StripSchema(req.GetObjectId())
	if err != nil {
		return nil, statusErrorf(codes.InvalidArgument, "invalid object ID: %v", err)
	}

	path := filepath.Join(h.objectsDir, digest)
	if err = os.Remove(path); err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return nil, statusErrorf(codes.NotFound, "object %q not found", req.GetObjectId())
		}
		return nil, statusErrorf(codes.Internal, "failed to delete object: %v", err)
	}

	slog.Info("Local CAS: Delete", slog.String("digest", digest), slog.String("path", path))
	return &caspb.DeleteResponse{}, nil
}

const maxPeekBytes = 128

// Stat returns the size and optional prefix of a local CAS object.
func (h *LocalCASHandler) Stat(ctx context.Context, req *caspb.StatRequest) (*caspb.StatResponse, error) {
	if p := req.GetPeekBytes(); !(0 <= p && p < maxPeekBytes) {
		return nil, statusErrorf(codes.InvalidArgument, "%d peek bytes outside of the valid range, want 0 <= p < %d", p, maxPeekBytes)
	}

	digest, err := idhandling.StripSchema(req.GetObjectId())
	if err != nil {
		return nil, statusErrorf(codes.InvalidArgument, "invalid object ID: %v", err)
	}

	path := filepath.Join(h.objectsDir, digest)
	fi, err := os.Stat(path)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return nil, statusErrorf(codes.NotFound, "object %q not found", req.GetObjectId())
		}
		return nil, statusErrorf(codes.Internal, "failed to stat object: %v", err)
	}

	if err := idhandling.ValidateObjectSize(digest, fi.Size()); err != nil {
		slog.Error("Corrupt local CAS object detected during Stat, removing",
			slog.String("digest", digest),
			slog.Int64("size", fi.Size()),
			slog.Any("error", err))
		if removeErr := os.Remove(path); removeErr != nil && !errors.Is(removeErr, fs.ErrNotExist) {
			slog.Warn("Failed to remove corrupt object", slog.String("path", path), slog.Any("error", removeErr))
		}
		return nil, statusErrorf(codes.NotFound, "object %q is corrupt and was removed: %v", req.GetObjectId(), err)
	}

	res := &caspb.StatResponse{
		Size: fi.Size(),
	}

	if peek := req.GetPeekBytes(); peek > 0 {
		data, err := mustRead(path, peek)
		if err != nil {
			return nil, statusErrorf(codes.Internal, "failed to peek %d bytes: %w", peek, err)
		}
		res.FirstBytes = data
	}

	slog.Debug("Local CAS: Stat", slog.String("digest", digest), slog.String("path", path),
		slog.Int("peek_bytes", len(res.FirstBytes)))

	return res, nil
}

// mustRead reads n bytes from file.
// Can return less only if the file is shorter.
func mustRead(path string, n int32) ([]byte, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, statusErrorf(codes.Internal, "failed to open object for peek: %v", err)
	}
	defer f.Close()

	buf := make([]byte, n)
	nRead, err := io.ReadFull(f, buf)
	if err != nil && !errors.Is(err, io.EOF) && !errors.Is(err, io.ErrUnexpectedEOF) {
		return nil, statusErrorf(codes.Internal, "failed to read peek bytes: %v", err)
	}
	return buf[:nRead], nil
}

func statusErrorf(code codes.Code, format string, args ...any) error {
	err := fmt.Errorf(format, args...)
	slog.Error(err.Error(), slog.String("code", code.String()))
	return status.Error(code, err.Error())
}

func toGRPCError(err error, format string, args ...any) error {
	if err == nil {
		return nil
	}
	code := codes.Internal
	if errors.Is(err, syscall.ENOSPC) || errors.Is(err, syscall.EDQUOT) {
		code = codes.ResourceExhausted
	}
	return statusErrorf(code, format+": %v", append(args, err)...)
}
