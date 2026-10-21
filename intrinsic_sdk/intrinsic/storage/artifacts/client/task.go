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

package client

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"math/rand"
	"strings"

	"intrinsic/storage/artifacts/internal/utils"

	log "github.com/golang/glog"
	crv1 "github.com/google/go-containerregistry/pkg/v1"
	"github.com/google/go-containerregistry/pkg/v1/partial"
	crtypes "github.com/google/go-containerregistry/pkg/v1/types"
	"github.com/pkg/errors"
	"go.uber.org/atomic"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	artifactgrpcpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

const (
	chunkIDMaxIncrement int64 = 100
	size4MB             int   = 4 * 1024 * 1024
)

type imageNameCtxType struct{}

var imageNameCtxValue = imageNameCtxType{}

type uploadStrategy int

const (
	streamingUpload uploadStrategy = iota
	nonStreamingUpload
)

type uploadTask interface {
	runWithCtx(ctx context.Context) error
}

type taskData struct {
	reader     io.ReadCloser
	client     artifactgrpcpb.ArtifactServiceApiClient
	descriptor *crv1.Descriptor
	monitor    ProgressMonitor
	updateSize int32
	name       string
}

func newTask(ctx context.Context, strategy uploadStrategy, client artifactgrpcpb.ArtifactServiceApiClient, maxUpdateSize int32, item namedObject, getItemReader itemReader) (uploadTask, error) {
	baseData := taskData{
		updateSize: maxUpdateSize,
		monitor:    getProgressMonitor(ctx),
		client:     client,
	}

	var err error

	// we expect that the caller will tell a task what is maximum data chunk size
	// we can safely send over internet to gRPC server. In case they don't, we will
	// restrict size of the chunk to 4MB to fit it within default gRPC
	// message size. To ensure proper communication, the caller have to get this
	// size information from server first, e.g.; by calling CheckImage() method.
	// Even if we get new size from server, we will never update this as that
	// could be callers decision outside our understanding.
	if baseData.updateSize <= 0 {
		baseData.updateSize = int32(size4MB)
	}

	baseData.descriptor, err = partial.Descriptor(item)
	if err != nil {
		return nil, err
	}

	baseData.reader, err = getItemReader()
	if err != nil {
		return nil, err
	}

	baseData.name, err = item.Name()
	if err != nil {
		return nil, err
	}

	switch strategy {
	case streamingUpload:
		return newStreamingTask(baseData)
	default:
		return newNonStreamingTask(baseData)
	}
}

func newStreamingTask(base taskData) (_ uploadTask, err error) {
	log.Infof("creating streaming upload for %s", asShortName(base.name))
	return &streamingTask{
		taskData:     base,
		terminalChan: make(chan error),
	}, nil
}

type streamingTask struct {
	taskData
	terminalChan chan error
}

func runWithContext(ctx context.Context, task uploadTask) func() error {
	return func() error {
		return task.runWithCtx(ctx)
	}
}

func (t *streamingTask) run() error {
	return t.runWithCtx(context.Background())
}

func (t *streamingTask) runWithCtx(ctx context.Context) error {
	log.InfoContextf(ctx, "starting upload: %s", asShortName(t.name))
	updateMonitor(t.monitor, asShortName(t.name), ProgressUpdate{
		Status:  StatusUndetermined,
		Current: 0,
		Total:   t.descriptor.Size,
		Message: "waiting for upload",
	})

	stream, err := t.client.UploadContent(ctx)
	if err != nil {
		return fmt.Errorf("cannot contact upstream: %s", err)
	}
	contentBuffer := make([]byte, t.updateSize)
	idTracker := atomic.NewInt64(0)
	firstChunk := true

	var totalSize int64 = 0
	var ctxErr error = nil
	for ctxErr = ctx.Err(); ctxErr == nil; ctxErr = ctx.Err() {
		action := artifactpb.UpdateAction_UPDATE_ACTION_UPDATE
		length, err := t.reader.Read(contentBuffer)
		if err != nil {
			if err != io.EOF {
				abortRun(t.name, firstChunk, idTracker, stream.Send)
				return fmt.Errorf("failed to read from source: %w", err)
			}
			action = artifactpb.UpdateAction_UPDATE_ACTION_COMMIT
		}

		// let's make defensive copy of slice to ensure we do not run into issues
		// if Message is delayed "on wire".
		dataSlice := make([]byte, length)
		copy(dataSlice, contentBuffer[:length])

		updateRequest := &artifactpb.UpdateRequest{
			Ref:       t.name,
			MediaType: string(t.descriptor.MediaType),
			Action:    action,
			// randomizing sequence, no need for cryptographically safe random
			ChunkId: idTracker.Add(nextChunkID()),
			Length:  int32(length),
			Data:    dataSlice,
		}
		log.InfoContextf(ctx, "[%s:%5d]: sending chunk (%d/%d) %s", asShortName(t.name), idTracker.Load(), totalSize+int64(length), t.descriptor.Size, action)

		if firstChunk {
			digest := t.descriptor.Digest.String()
			updateRequest.ExpectedDigest = &digest
			updateRequest.Content = asArtifactDescriptor(ctx, t.descriptor)
		}

		err = stream.Send(updateRequest)
		if err != nil {
			if errors.Is(err, io.EOF) {
				// fetch error details and break the loop.
				log.WarningContextf(ctx, "[%s:%5d]: stream.CloseAndRecv() on stream.Send() err: %s", asShortName(t.name), idTracker.Load(), err)
				var resp *artifactpb.UpdateResponse
				resp, ctxErr = stream.CloseAndRecv()
				doUpdateMonitor(t.monitor, resp, t.descriptor)
				break
			}
			if firstChunk {
				// on first chunk we need to check if resource we are writing already
				// exists. see b/327799134
				return t.checkForAlreadyExists(ctx, err)
			}
			return fmt.Errorf("[%s] send failed: %w", asShortName(t.name), err)
		}
		firstChunk = false

		totalSize += int64(length)
		if action == artifactpb.UpdateAction_UPDATE_ACTION_COMMIT {
			response, err := stream.CloseAndRecv()
			if t.checkForAlreadyExists(ctx, err) != nil {
				return fmt.Errorf("error waiting for response: %w", err)
			}
			doUpdateMonitor(t.monitor, response, t.descriptor)
			return nil // we are done with upload.
		}
		resp := makeProgressResponse(updateRequest, totalSize)
		doUpdateMonitor(t.monitor, resp, t.descriptor)

	}

	if ctxErr != nil {
		// we terminated loop with context. This means we may not have finished
		// full update. If we already did commit, we will get and error on abort
		// but if we didn't finish, we will put Abort on wire to make sure we
		// abort requests in flight
		abortRun(t.name, firstChunk, idTracker, stream.Send)
		return fmt.Errorf("[%s] premature termination: %w", asShortName(t.name), ctxErr)
	}

	return ctxErr
}

func (t *streamingTask) checkForAlreadyExists(ctx context.Context, err error) error {
	if err == nil {
		return nil
	}
	if errStatus, ok := status.FromError(err); ok {
		if errStatus.Code() == codes.AlreadyExists {
			// this item already exists. This could be for various reasons,
			// but we consider this success.
			updateMonitor(t.monitor, asShortName(t.name), ProgressUpdate{
				Status:  StatusSuccess,
				Current: t.descriptor.Size,
				Total:   t.descriptor.Size,
				Message: "already exists",
			})
			log.InfoContextf(ctx, "[%s] already exists", asShortName(t.name))
			return nil // our work is done.
		}
	}
	return err
}

func nextChunkID() int64 {
	return rand.Int63n(chunkIDMaxIncrement) + 1
}

func abortRun(ref string, firstChunk bool, idTracker *atomic.Int64, delivery func(request *artifactpb.UpdateRequest) error) {
	if !firstChunk {
		// we already sent some data to server, let's abort write
		abortRequest := &artifactpb.UpdateRequest{
			Ref:     ref,
			Action:  artifactpb.UpdateAction_UPDATE_ACTION_ABORT,
			ChunkId: idTracker.Add(chunkIDMaxIncrement),
		}
		if errSend := delivery(abortRequest); errSend != nil {
			log.Errorf("failed to send Abort to server: %s", errSend)
		}
	}
}

func asShortName(name string) string {
	if strings.HasPrefix(name, "sha") {
		// shaXYZ:IDENTIFIER
		if len(name) > 19 {
			return name[7:19]
		}
	}
	return name
}

func asArtifactDescriptor(ctx context.Context, descriptor *crv1.Descriptor) *artifactpb.ArtifactDescriptor {
	digest := descriptor.Digest.String()
	ad := &artifactpb.ArtifactDescriptor{
		MediaType:   string(descriptor.MediaType),
		Digest:      &digest,
		Size:        &descriptor.Size,
		Annotations: descriptor.Annotations,
	}
	setImageName(ctx, ad)
	return ad
}

func makeProgressResponse(update *artifactpb.UpdateRequest, total int64) *artifactpb.UpdateResponse {
	return &artifactpb.UpdateResponse{
		Ref:     update.Ref,
		ChunkId: update.ChunkId,
		Total:   &total,
		Action:  &update.Action,
	}
}

func doUpdateMonitor(monitor ProgressMonitor, msg *artifactpb.UpdateResponse, descriptor *crv1.Descriptor) {
	if msg == nil {
		return
	}
	action := valueOrDefault(msg.Action, artifactpb.UpdateAction_UPDATE_ACTION_UNDEFINED)
	switch action {
	case artifactpb.UpdateAction_UPDATE_ACTION_COMMIT:
		updateMonitor(monitor, asShortName(msg.Ref), ProgressUpdate{
			Status:  StatusSuccess,
			Current: *msg.Total,
			Total:   *msg.Total,
			Message: fmt.Sprintf("[%5d]: done", msg.ChunkId),
		})
		return
	case artifactpb.UpdateAction_UPDATE_ACTION_ABORT:
		updateMonitor(monitor, asShortName(msg.Ref), ProgressUpdate{
			Status:  StatusFailure,
			Current: *msg.Total,
			Total:   descriptor.Size,
			Message: fmt.Sprintf("[%5d]: aborted", msg.ChunkId),
		})
		return
	case artifactpb.UpdateAction_UPDATE_ACTION_UPDATE:
		updateMonitor(monitor, asShortName(msg.Ref), ProgressUpdate{
			Status:  StatusContinue,
			Current: *msg.Total,
			Total:   descriptor.Size,
			Message: fmt.Sprintf("[%5d]: uploading", msg.ChunkId),
		})
	default:
	}
}

func updateMonitor(monitor ProgressMonitor, name string, update ProgressUpdate) {
	if monitor != nil {
		monitor.UpdateProgress(asShortName(name), update)
	}
}

type itemReader func() (io.ReadCloser, error)

func bytesReader(extract func() ([]byte, error)) itemReader {
	return func() (io.ReadCloser, error) {
		content, err := extract()
		if err != nil {
			return nil, err
		}
		return io.NopCloser(bytes.NewReader(content)), nil
	}
}

func asDigestNamed(desc partial.Describable) namedObject {
	return asNamedObject(func() (string, error) {
		digest, err := desc.Digest()
		if err != nil {
			return "", err
		}
		return digest.String(), nil
	}, desc)
}

func asSimplyNamed(name string, desc partial.Describable) namedObject {
	return asNamedObject(func() (string, error) {
		return name, nil
	}, desc)
}

func asNamedObject(name func() (string, error), desc partial.Describable) namedObject {
	return &describableObject{
		describable: desc,
		nameFx:      name,
	}
}

type namedObject interface {
	partial.Describable
	Name() (string, error)
}

type describableObject struct {
	describable partial.Describable
	nameFx      func() (string, error)
}

func (d *describableObject) Digest() (crv1.Hash, error) {
	return d.describable.Digest()
}

func (d *describableObject) MediaType() (crtypes.MediaType, error) {
	return d.describable.MediaType()
}

func (d *describableObject) Size() (int64, error) {
	return d.describable.Size()
}

func (d *describableObject) Name() (string, error) {
	return d.nameFx()
}

type descWrap struct {
	value crv1.Descriptor
}

func (c descWrap) Digest() (crv1.Hash, error) {
	return c.value.Digest, nil
}

func (c descWrap) MediaType() (crtypes.MediaType, error) {
	return c.value.MediaType, nil
}

func (c descWrap) Size() (int64, error) {
	return c.value.Size, nil
}

func valueOrDefault[T any](ptr *T, def T) T {
	if ptr == nil {
		return def
	}
	return *ptr
}

func attachImageName(ctx context.Context, imageName string) context.Context {
	return context.WithValue(ctx, imageNameCtxValue, imageName)
}

func setImageName(ctx context.Context, ad *artifactpb.ArtifactDescriptor) {
	if ad == nil {
		return
	}
	value := ctx.Value(imageNameCtxValue)
	if value == nil || value.(string) == "" {
		return
	}

	if ad.Annotations == nil {
		ad.Annotations = make(map[string]string)
	}
	ad.Annotations[utils.AnnotationImageName] = value.(string)
	return
}
