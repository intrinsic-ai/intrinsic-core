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
	"context"
	"fmt"
	"io"

	backoff "github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"go.uber.org/atomic"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

var (
	// internal error to indicate that item already exists, and we should stop update
	errAlreadyExists = errors.New("already exists")
	// this can be set in tests to speed them up
	backOffStrategy = backoff.WithMaxRetries(backoff.NewExponentialBackOff(), 3)
)

type nonStreamingTask struct {
	taskData
}

func newNonStreamingTask(base taskData) (uploadTask, error) {
	return &nonStreamingTask{taskData: base}, nil
}

func (t *nonStreamingTask) runWithCtx(ctx context.Context) error {
	log.V(1).InfoContextf(ctx, "[%s] starting upload", asShortName(t.name))
	updateMonitor(t.monitor, asShortName(t.name), ProgressUpdate{
		Status:  StatusUndetermined,
		Current: 0,
		Total:   t.descriptor.Size,
		Message: "waiting for upload",
	})

	contentBuffer := make([]byte, 100) // initial exploratory chunk, see b/327799134
	idTracker := atomic.NewInt64(0)
	firstChunk := true
	var totalSize int64 = 0
	var ctxErr error = nil
	for ctxErr = ctx.Err(); ctxErr == nil; ctxErr = ctx.Err() {
		action := artifactpb.UpdateAction_UPDATE_ACTION_UPDATE
		length, err := io.ReadFull(t.reader, contentBuffer)
		if err != nil {
			// io.ErrUnexpectedEOF just means we read less than full buffer.
			if err != io.EOF && err != io.ErrUnexpectedEOF {
				abortRun(t.name, firstChunk, idTracker, t.sender(ctx))
				return fmt.Errorf("failed to read from source: %w", err)
			}
			action = artifactpb.UpdateAction_UPDATE_ACTION_COMMIT
		}

		updateRequest := &artifactpb.UpdateRequest{
			Ref:       t.name,
			MediaType: string(t.descriptor.MediaType),
			Action:    action,
			// randomizing sequence, no need for cryptographically safe random
			ChunkId: idTracker.Add(nextChunkID()),
			Length:  int32(length),
			Data:    make([]byte, length),
		}
		if length > 0 {
			// let's make defensive copy of slice to ensure we do not run into issues
			// if Message is delayed "on wire".
			copy(updateRequest.Data, contentBuffer[:length])
		}
		totalSize += int64(length)

		log.V(1).InfoContextf(ctx, "[%s]: sending chunk %5d (%s): (%d/%d) in %d increment", asShortName(t.name), idTracker.Load(), action, totalSize, t.descriptor.Size, t.updateSize)

		if firstChunk {
			digest := t.descriptor.Digest.String()
			updateRequest.ExpectedDigest = &digest
			updateRequest.Content = asArtifactDescriptor(ctx, t.descriptor)
			// reset contentBuffer to full size after first chunk
			contentBuffer = make([]byte, t.updateSize)
		}

		response, err := t.writeContent(ctx, updateRequest, firstChunk)
		if err != nil {
			if errors.Is(err, errAlreadyExists) {
				// this item already exists. This could be for various reasons,
				// but we consider this success.
				updateMonitor(t.monitor, asShortName(t.name), ProgressUpdate{
					Status:  StatusSuccess,
					Current: t.descriptor.Size,
					Total:   t.descriptor.Size,
					Message: "already exists",
				})
				return nil
			}
			return fmt.Errorf("[%s] write failed: %w", asShortName(t.name), err)
		}
		firstChunk = false
		doUpdateMonitor(t.monitor, response, t.descriptor)

		updateAction := valueOrDefault(response.Action, action)
		if updateAction == artifactpb.UpdateAction_UPDATE_ACTION_COMMIT ||
			updateAction == artifactpb.UpdateAction_UPDATE_ACTION_ABORT {
			// this is the only valid non-error exit condition, any other path
			// needs to return with error.
			return nil
		}
	}

	return fmt.Errorf("[%s]: premature end: %w", asShortName(t.name), ctxErr)
}

func (t *nonStreamingTask) writeContent(ctx context.Context, updateRequest *artifactpb.UpdateRequest, firstChunk bool) (response *artifactpb.UpdateResponse, err error) {
	// adding retry for unavailable (503) response. see:b/330747118
	err = backoff.Retry(func() error {
		var localErr error
		response, localErr = t.client.WriteContent(ctx, updateRequest)
		if localErr != nil {
			if errStatus, ok := status.FromError(localErr); ok {
				// this is valid only for first request
				if codes.AlreadyExists == errStatus.Code() && firstChunk {
					log.V(1).InfoContextf(ctx, "[%s] already exists", asShortName(t.name))
					return backoff.Permanent(errAlreadyExists) // our work is done.
				}
				if codes.Unavailable == errStatus.Code() {
					// This is transient error, let's retry
					log.WarningContextf(ctx, "[%s] transient error: %s", asShortName(t.name), errStatus)
					return localErr
				}
			}
		}
		// any other error is considered permanent
		return backoff.Permanent(localErr)
	}, backOffStrategy)

	return response, err
}

func (t *nonStreamingTask) sender(ctx context.Context) func(request *artifactpb.UpdateRequest) error {
	return func(request *artifactpb.UpdateRequest) error {
		_, err := t.client.WriteContent(ctx, request)
		return err
	}
}
