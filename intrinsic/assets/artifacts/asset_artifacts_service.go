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

// Package assetartifactsservice provides an implementation of AssetArtifacts.
package assetartifactsservice

import (
	"context"
	"fmt"
	"strings"

	"intrinsic/assets/artifacts/processor"
	"intrinsic/assets/artifacts/uploader"
	"intrinsic/assets/referenceddata"

	log "github.com/golang/glog"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// service implements the AssetArtifacts service.
type service struct {
	proc     *processor.Processor
	uploader *uploader.Uploader
}

// New creates a new AssetArtifactsServer.
func New(proc *processor.Processor, uploader *uploader.Uploader) (assetartifactspb.AssetArtifactsServer, error) {
	if proc == nil {
		return nil, fmt.Errorf("proc must be non-nil")
	}
	if uploader == nil {
		return nil, fmt.Errorf("uploader must be non-nil")
	}

	return &service{
		proc:     proc,
		uploader: uploader,
	}, nil
}

func (s *service) Process(ctx context.Context, req *assetartifactspb.ProcessRequest) (opProto *lropb.Operation, err error) {
	ctx, span := trace.StartSpan(ctx, "AssetArtifacts.Process")
	defer func() { finalizeSpan(span, err) }()

	ref, makeResponse, err := parseProcessRequest(req)
	if err != nil {
		return nil, err
	}

	op, err := s.proc.Process(ctx, ref, makeResponse)
	if err != nil {
		return nil, err
	}

	return op.Proto(), nil
}

func parseProcessRequest(req *assetartifactspb.ProcessRequest) (*referenceddata.ReferencedData, processor.MakeResponse, error) {
	if req == nil {
		return nil, nil, status.Error(codes.InvalidArgument, "request is required")
	}

	type artifactType int

	const (
		artifactTypeURI artifactType = iota
		artifactTypeReferencedData
	)

	var refProto *rdpb.ReferencedData
	var artType artifactType

	switch a := req.GetArtifact().(type) {
	case *assetartifactspb.ProcessRequest_ReferencedData:
		refProto = a.ReferencedData
		artType = artifactTypeReferencedData
	case *assetartifactspb.ProcessRequest_Uri:
		artType = artifactTypeURI
		if strings.HasPrefix(a.Uri, "data:,") {
			refProto = &rdpb.ReferencedData{
				Data: &rdpb.ReferencedData_Inlined{
					Inlined: []byte(a.Uri[6:]),
				},
			}
		} else {
			refProto = &rdpb.ReferencedData{
				Data: &rdpb.ReferencedData_Reference{
					Reference: a.Uri,
				},
			}
		}
	default:
		return nil, nil, status.Error(codes.InvalidArgument, "first message must contain uri or referenced_data")
	}
	ref := referenceddata.FromProto(refProto)

	makeResponse := func(ref *referenceddata.ReferencedData) proto.Message {
		resp := &assetartifactspb.ProcessResponse{
			Digest: ref.Digest(),
		}

		switch artType {
		case artifactTypeURI:
			uri := ref.Reference()
			if ref.Type() == referenceddata.InlinedReferenceType {
				uri = "data:," + string(ref.Inlined())
			}

			resp.Artifact = &assetartifactspb.ProcessResponse_Uri{Uri: uri}
		case artifactTypeReferencedData:
			resp.Artifact = &assetartifactspb.ProcessResponse_ReferencedData{
				ReferencedData: ref.ToProto(),
			}
		}

		return resp
	}

	return ref, makeResponse, nil
}

// StartUpload starts a chunked upload session.
func (s *service) StartUpload(ctx context.Context, req *assetartifactspb.StartUploadRequest) (*assetartifactspb.StartUploadResponse, error) {
	id, artifactExists, err := s.uploader.Add(ctx, req.GetDigest())
	if err != nil {
		log.ErrorContextf(ctx, "StartUpload failed: %v", err)
		return nil, err
	}

	return &assetartifactspb.StartUploadResponse{
		ArtifactExists: artifactExists,
		UploadId:       id,
	}, nil
}

// UploadChunk uploads a chunk of data for an active upload session.
func (s *service) UploadChunk(ctx context.Context, req *assetartifactspb.UploadChunkRequest) (*assetartifactspb.UploadChunkResponse, error) {
	upload, err := s.uploader.Get(req.GetUploadId())
	if err != nil {
		log.WarningContextf(ctx, "UploadChunk failed: upload %q not found: %v", req.GetUploadId(), err)
		return nil, err
	}

	if err := upload.Send(ctx, req.GetOffset(), req.GetData()); err != nil {
		log.ErrorContextf(ctx, "UploadChunk for upload %q failed at offset %d (size %d): %v", req.GetUploadId(), req.GetOffset(), len(req.GetData()), err)
		return nil, err
	}

	return &assetartifactspb.UploadChunkResponse{
		AckOffset: req.GetOffset() + int64(len(req.GetData())),
	}, nil
}

// FinalizeUpload finalizes a chunked upload session and returns the ReferencedData.
func (s *service) FinalizeUpload(ctx context.Context, req *assetartifactspb.FinalizeUploadRequest) (*assetartifactspb.FinalizeUploadResponse, error) {
	upload, err := s.uploader.Get(req.GetUploadId())
	if err != nil {
		log.WarningContextf(ctx, "FinalizeUpload failed: upload %q not found: %v", req.GetUploadId(), err)
		return nil, err
	}

	ref, err := upload.Finalize(ctx, req.GetExpectedDigest())
	if err != nil {
		log.ErrorContextf(ctx, "FinalizeUpload for upload %q failed: %v", req.GetUploadId(), err)
		return nil, err
	}

	return &assetartifactspb.FinalizeUploadResponse{
		ReferencedData: ref,
	}, nil
}

func finalizeSpan(span *trace.Span, err error) {
	if err != nil {
		if st, ok := status.FromError(err); ok {
			span.SetStatus(trace.Status{
				Code:    int32(st.Code()),
				Message: st.Message(),
			})
		} else {
			span.SetStatus(trace.Status{
				Code:    trace.StatusCodeUnknown,
				Message: err.Error(),
			})
		}
	}

	span.End()
}
