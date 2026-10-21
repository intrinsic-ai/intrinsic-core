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

// Package clienthelpers provides useful constants and helper functions for working with the
// content-addressable storage service.
package clienthelpers

import (
	"bytes"
	"context"
	"fmt"
	"hash"
	"hash/crc32"
	"io"
	"os"
	"time"

	backoff "github.com/cenkalti/backoff/v4"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	casgrpcpb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

// DefaultUploadChunkSize is a recommended size of content to be included into one stream request.
//
// Why 1 MiB? https://cloud.google.com/blog/products/gcp/optimizing-your-cloud-storage-performance-google-cloud-performance-atlas
// suggests that 1 MB+ is a good chunk size.
const DefaultUploadChunkSize = 1 * 1024 * 1024

var crc32Castagnoli = crc32.MakeTable(crc32.Castagnoli)

// NewChecksummer returns a checksummer that should be used for checksumming the content sent to the
// content-addressable storage service. See https://cloud.google.com/storage/docs/hashes-etags#crc32c.
func NewChecksummer() hash.Hash32 {
	return crc32.New(crc32Castagnoli)
}

// Get is a convenience function that retrieves an object from CAS and writes the bytes to the
// provided writer. If an error is returned from the CAS service, it is passed down unwrapped so
// that clients can check for the canonical error code.
func Get(ctx context.Context, casClient casgrpcpb.ContentAddressableStorageServiceClient, objectID string, w io.Writer) error {
	ctx, span := trace.StartSpan(ctx, "clienthelpers.Get")
	defer span.End()

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	req := &caspb.GetRequest{ObjectId: objectID}
	stream, err := casClient.Get(ctx, req)
	if err != nil {
		return fmt.Errorf("creating stream for %q: %w", req, err)
	}

	checksummer := NewChecksummer()
	for {
		checksummer.Reset()
		res, err := stream.Recv()
		if err == io.EOF {
			break
		} else if err != nil {
			return err
		}
		content := res.GetChecksummedData().GetContent()
		if _, err := checksummer.Write(content); err != nil {
			return fmt.Errorf("could not checksum content: %w", err)
		}
		if clientCRC, serverCRC := checksummer.Sum32(), res.GetChecksummedData().GetCrc32C(); clientCRC != serverCRC {
			return fmt.Errorf("checksum mismatch: client computed 0x%08x, server computed 0x%08x", clientCRC, serverCRC)
		}
		n, err := w.Write(content)
		if err != nil {
			return fmt.Errorf("could not write data: %w", err)
		}
		if n != len(content) {
			return fmt.Errorf("content write mismatch, wrote %d bytes, got %d from server", n, len(content))
		}
	}
	return nil
}

type downloadOptions struct {
	casClient       casgrpcpb.ContentAddressableStorageServiceClient
	offset          int64
	maxRetries      uint64
	infiniteRetries bool
	initialBackoff  time.Duration
	maxBackoff      time.Duration
}

func defaultDownloadOptions() *downloadOptions {
	return &downloadOptions{
		maxRetries:     5,
		initialBackoff: 100 * time.Millisecond,
		maxBackoff:     2 * time.Second,
	}
}

// DownloadOption configures GetResumableReader.
type DownloadOption func(*downloadOptions)

// WithCASClient configures the CAS service client used for the download.
func WithCASClient(casClient casgrpcpb.ContentAddressableStorageServiceClient) DownloadOption {
	return func(o *downloadOptions) {
		o.casClient = casClient
	}
}

// WithOffset configures the starting byte offset for the download.
// Defaults to 0 if not specified.
func WithOffset(offset int64) DownloadOption {
	return func(o *downloadOptions) {
		o.offset = offset
	}
}

// WithMaxRetries configures the maximum number of resume retries upon transient errors.
// The retries are set per chunk of downloaded data.
func WithMaxRetries(retries uint64) DownloadOption {
	return func(o *downloadOptions) {
		o.maxRetries = retries
		o.infiniteRetries = false
	}
}

// WithInfiniteRetries configures GetResumableReader to retry upon transient errors
// indefinitely until the caller's context is done.
func WithInfiniteRetries() DownloadOption {
	return func(o *downloadOptions) {
		o.infiniteRetries = true
		o.maxRetries = 0
	}
}

// WithRetryBackoff configures the initial and maximum exponential backoff duration.
func WithRetryBackoff(initial, max time.Duration) DownloadOption {
	return func(o *downloadOptions) {
		o.initialBackoff = initial
		o.maxBackoff = max
	}
}

func (o *downloadOptions) backoff(ctx context.Context) backoff.BackOff {
	b := backoff.NewExponentialBackOff(
		backoff.WithInitialInterval(o.initialBackoff),
		backoff.WithMaxInterval(o.maxBackoff),
	)

	var bo backoff.BackOff = b
	if o.infiniteRetries {
		// Disable the default 15-minute MaxElapsedTime so the caller's context
		// is the sole authority for total timeout.
		b.MaxElapsedTime = 0
	} else {
		bo = backoff.WithMaxRetries(b, o.maxRetries)
	}
	return backoff.WithContext(bo, ctx)
}

func isRetriable(err error) bool {
	switch status.Code(err) {
	case codes.Unavailable, codes.DeadlineExceeded, codes.ResourceExhausted, codes.Aborted:
		return true
	}
	return false
}

var (
	_ io.ReadCloser = (*ResumableReader)(nil)
	_ io.WriterTo   = (*ResumableReader)(nil)
)

// ResumableReader streams an object from CAS starting at a given byte offset,
// verifying CRC32C checksums per chunk and automatically retrying transient
// gRPC errors with exponential backoff from the latest read offset.
// If the server does not implement GetRange and the starting offset is 0, it
// falls back to Get.
//
// Callers must call [ResumableReader.Close] when done to release the underlying
// gRPC stream resources. ResumableReader is not safe for concurrent use.
type ResumableReader struct {
	// [io.Reader]'s Read doesn't accept a context but we need to propagate it to
	// [ResumableReader.pull] to refill [ResumableReader.buf].
	ctx        context.Context
	cancelFunc context.CancelCauseFunc
	span       *trace.Span
	// contains remaining data unread from the most recently pulled chunk of the object.
	buf         []byte
	checksummer hash.Hash32
	objectID    string
	// tracks the current byte offset in the CAS object
	offset    int64
	casClient casgrpcpb.ContentAddressableStorageServiceClient
	// not all servers implement GetRange. useGet makes [ResumableReader] fall back to Get.
	// It is only supported for a download from 0 offset.
	useGet  bool
	backoff backoff.BackOff
	err     error

	streamGet      casgrpcpb.ContentAddressableStorageService_GetClient
	streamGetRange casgrpcpb.ContentAddressableStorageService_GetRangeClient
}

// WriteTo implements [io.WriterTo] to optimize io.Copy
// by getting rid of an intermediate buffer during the copy.
func (r *ResumableReader) WriteTo(w io.Writer) (int64, error) {
	var totalWritten int64

	for {
		if r.ctx.Err() != nil {
			return totalWritten, context.Cause(r.ctx)
		}

		for len(r.buf) == 0 {
			if err := r.pull(); err != nil {
				// io.EOF indicates the entire stream has been written; per the io.WriterTo
				// and io.Copy contracts, normal completion returns a nil error.
				if err == io.EOF {
					return totalWritten, nil
				}
				return totalWritten, err
			}
		}

		n, err := w.Write(r.buf)
		if n < 0 || n > len(r.buf) {
			if err != nil {
				return totalWritten, err
			}
			return totalWritten, fmt.Errorf(
				"invalid write count: writer reported %d bytes written for %d-byte buffer",
				n, len(r.buf),
			)
		}
		r.buf = r.buf[n:]
		r.offset += int64(n)
		totalWritten += int64(n)
		if err != nil {
			return totalWritten, err
		}

		if len(r.buf) > 0 {
			return totalWritten, fmt.Errorf(
				"%w: wrote %d of %d bytes",
				io.ErrShortWrite, n, n+len(r.buf),
			)
		}
	}
}

// Close implements [io.ReadCloser].
func (r *ResumableReader) Close() error {
	r.cancelFunc(os.ErrClosed)
	r.span.End()
	return nil
}

// Read implements [io.ReadCloser].
func (r *ResumableReader) Read(p []byte) (int, error) {
	if r.ctx.Err() != nil {
		return 0, context.Cause(r.ctx)
	}

	if len(p) == 0 {
		return 0, nil
	}

	// pull() is called in the loop to make sure r.buf is filled.
	for len(r.buf) == 0 {
		if err := r.pull(); err != nil {
			return 0, err
		}
	}

	n := copy(p, r.buf)
	r.buf = r.buf[n:]
	r.offset += int64(n)

	return n, nil
}

// pull pulls the next chunk from the Get/GetRange stream and puts it into
// r.buf.
func (r *ResumableReader) pull() error {
	if r.err != nil {
		return r.err
	}

	r.err = backoff.Retry(func() error {
		if err := r.open(); err != nil {
			return err
		}

		var data *caspb.ChecksummedData

		if !r.useGet {
			resp, err := r.streamGetRange.Recv()
			switch {
			case status.Code(err) == codes.Unimplemented && r.offset == 0:
				r.streamGetRange = nil
				r.useGet = true
				if err := r.open(); err != nil {
					return err
				}
			case err != nil:
				// it will be reinitialized in [ResumableReader.open] before retrying
				r.streamGetRange = nil
				if isRetriable(err) {
					return err
				}
				return backoff.Permanent(err)
			case resp.GetChunkOffset() != r.offset:
				return backoff.Permanent(
					fmt.Errorf("unexpected chunk offset: got %d, want %d", resp.GetChunkOffset(), r.offset),
				)
			default:
				data = resp.GetChecksummedData()
			}
		}

		if r.useGet {
			resp, err := r.streamGet.Recv()
			if err != nil {
				// it will be reinitialized in [ResumableReader.open] before retrying
				r.streamGet = nil
				// It is retriable only for 0 offset. Otherwise, the data will be overwritten.
				if isRetriable(err) && r.offset == 0 {
					return err
				}

				return backoff.Permanent(err)
			}

			data = resp.GetChecksummedData()
		}

		r.checksummer.Reset()
		if _, err := r.checksummer.Write(data.GetContent()); err != nil {
			return backoff.Permanent(fmt.Errorf("error calculating checksum: %w", err))
		}

		if clientCRC, serverCRC := r.checksummer.Sum32(), data.GetCrc32C(); clientCRC != serverCRC {
			return backoff.Permanent(
				fmt.Errorf("checksum mismatch: client computed 0x%08x, server computed 0x%08x", clientCRC, serverCRC),
			)
		}

		r.buf = data.GetContent()

		return nil
	}, r.backoff)

	if r.ctx.Err() != nil {
		r.err = context.Cause(r.ctx)
	}

	return r.err
}

// open opens the stream to read data from.
// If the stream is already opened, it's a no-op.
func (r *ResumableReader) open() error {
	if r.useGet {
		if r.streamGet != nil {
			return nil
		}
		if r.offset != 0 {
			return backoff.Permanent(fmt.Errorf("cannot open Get stream at non-zero offset %d", r.offset))
		}

		req := &caspb.GetRequest{
			ObjectId: r.objectID,
		}
		stream, err := r.casClient.Get(r.ctx, req)
		if err != nil {
			if isRetriable(err) {
				return err
			}
			return backoff.Permanent(err)
		}
		r.streamGet = stream
		return nil
	}

	if r.streamGetRange != nil {
		return nil
	}

	req := &caspb.GetRangeRequest{
		ObjectId:   r.objectID,
		ReadOffset: r.offset,
	}
	stream, err := r.casClient.GetRange(r.ctx, req)
	if err != nil {
		if status.Code(err) == codes.Unimplemented && r.offset == 0 {
			r.useGet = true
			return r.open()
		}
		if isRetriable(err) {
			return err
		}
		return backoff.Permanent(err)
	}

	r.streamGetRange = stream
	return nil
}

// GetResumableReader returns a [ResumableReader] configured to read objectID
// starting at byte 0 (or at the offset configured via [WithOffset]).
// Callers must provide a CAS client via [WithCASClient].
// The underlying gRPC stream is opened lazily on the first call to Read or
// WriteTo; any error returned by the CAS service is passed down unwrapped so
// callers can inspect status.Code(err).
func GetResumableReader(
	ctx context.Context,
	objectID string,
	opts ...DownloadOption,
) (*ResumableReader, error) {
	defaultOpts := defaultDownloadOptions()
	for _, o := range opts {
		o(defaultOpts)
	}

	if defaultOpts.casClient == nil {
		return nil, status.Error(codes.InvalidArgument, "CAS client must not be nil")
	}

	if defaultOpts.offset < 0 {
		return nil, status.Error(codes.InvalidArgument, "offset must be >= 0")
	}

	ctx, span := trace.StartSpan(ctx, "clienthelpers.GetResumableReader")
	childCtx, cancel := context.WithCancelCause(ctx)

	return &ResumableReader{
		ctx:         childCtx,
		cancelFunc:  cancel,
		span:        span,
		objectID:    objectID,
		offset:      defaultOpts.offset,
		backoff:     defaultOpts.backoff(childCtx),
		casClient:   defaultOpts.casClient,
		checksummer: NewChecksummer(),
	}, nil
}

// GetAsProto is a convenience function that retrieves an object from CAS and then unmarshals the
// bytes to the provided [proto.Message] type T. If an error is returned from the CAS service, it is
// passed down unwrapped so that clients can check for the canonical error code.
func GetAsProto[T proto.Message](ctx context.Context, casClient casgrpcpb.ContentAddressableStorageServiceClient, objectID string) (T, error) {
	ctx, span := trace.StartSpan(ctx, "blobstorage.GetAsProto")
	defer span.End()

	var nilT T
	buf := new(bytes.Buffer)
	if err := Get(ctx, casClient, objectID, buf); err != nil {
		return nilT, err
	}

	var res T
	res = res.ProtoReflect().New().Interface().(T)
	if err := proto.Unmarshal(buf.Bytes(), res); err != nil {
		return nilT, fmt.Errorf("could not unmarshal bytes to type %T: %w", res, err)
	}
	return res, nil
}

// Create is a convenience function that reads from the provided reader object and writes these
// bytes to CAS. This function returns the object ID and an error. In case an error is returned
// from the CAS service, it is passed down unwrapped so that clients can check for the canonical
// error code.
func Create(ctx context.Context, casClient casgrpcpb.ContentAddressableStorageServiceClient, r io.Reader, chunkSize int64) (string, error) {
	ctx, span := trace.StartSpan(ctx, "clienthelpers.Create")
	defer span.End()
	var err error
	ctx, cancel := context.WithCancelCause(ctx)
	defer cancel(err)

	stream, err := casClient.Create(ctx)
	if err != nil {
		return "", err
	}
	checksummer := NewChecksummer()
	buf := make([]byte, chunkSize)
	for {
		checksummer.Reset()

		n, readerErr := io.ReadFull(r, buf)
		if readerErr != io.EOF && readerErr != io.ErrUnexpectedEOF && readerErr != nil {
			err = fmt.Errorf("could not read from reader: %w", readerErr)
			return "", err
		}
		if n == 0 {
			break
		}
		content := buf[:n]
		if _, err = checksummer.Write(content); err != nil {
			err = fmt.Errorf("could not checksum buf: %w", err)
			return "", err
		}
		req := &caspb.CreateRequest{
			ChecksummedData: &caspb.ChecksummedData{
				Content: content,
				Crc32C:  proto.Uint32(checksummer.Sum32()),
			},
		}
		if err = stream.Send(req); err != nil {
			return "", stream.RecvMsg(nil)
		}
		if readerErr == io.EOF || readerErr == io.ErrUnexpectedEOF {
			break
		}
	}
	res, err := stream.CloseAndRecv()
	if err != nil {
		return "", fmt.Errorf("closing stream: %w", err)
	}
	return res.GetObjectId(), nil
}

// CreateFromProto is a convenience function that takes a [proto.Message], marshals it to the wire
// format, and writes these bytes to CAS. This function returns the object ID and an error. In case
// an error is returned from the CAS service, it is passed down unwrapped so that clients can check
// for the canonical error code.
func CreateFromProto(ctx context.Context, casClient casgrpcpb.ContentAddressableStorageServiceClient, msg proto.Message, chunkSize int64) (string, error) {
	ctx, span := trace.StartSpan(ctx, "clienthelpers.CreateFromProto")
	defer span.End()

	if msg == nil {
		return "", fmt.Errorf("nil proto message provided")
	}

	d, err := proto.Marshal(msg)
	if err != nil {
		return "", fmt.Errorf("could not marshal proto: %w", err)
	}

	r := bytes.NewReader(d)
	return Create(ctx, casClient, r, chunkSize)
}
