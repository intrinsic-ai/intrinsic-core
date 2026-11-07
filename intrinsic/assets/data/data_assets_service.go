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

// Package dataassetsservice provides an implementation of the DataAssets service.
package dataassetsservice

import (
	"context"
	"fmt"
	"slices"
	"strings"

	"intrinsic/assets/data/utils"
	"intrinsic/assets/idutils"
	"intrinsic/assets/pageutils"
	"intrinsic/assets/referenceddata"
	"intrinsic/resources/service/resourcetyperuntime"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/reflect/protoreflect"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dasgrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	daspb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	mpb "intrinsic/assets/proto/metadata_go_proto"
)

// Options configures a [DataAssetsService].
type Options struct {
	RTRClient resourcetyperuntime.Client
}

// dataAssetsService implements the DataAssets service.
type dataAssetsService struct {
	rtrClient resourcetyperuntime.Client
}

// New constructs a [DataAssetsService].
func New(opts Options) dasgrpcpb.DataAssetsServer {
	return &dataAssetsService{
		rtrClient: opts.RTRClient,
	}
}

func (s *dataAssetsService) ListDataAssets(ctx context.Context, req *daspb.ListDataAssetsRequest) (*daspb.ListDataAssetsResponse, error) {
	pageSize, err := pageutils.ResolvePageSize(int(req.GetPageSize()))
	if err != nil {
		return nil, err
	}
	prevStartAfters, err := pageutils.DecodePageToken(req.GetPageToken(), []string{req.GetStrictFilter().GetProtoName()})
	if err != nil {
		return nil, err
	}

	// List all assets, then filter for Data assets.
	rtrs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient)
	if err != nil {
		return nil, fmt.Errorf("could not list runtime info for data assets: %v", err)
	}
	das := make([]*dapb.DataAsset, 0, len(rtrs))
	for _, rtr := range rtrs {
		if rtr.GetData() != nil {
			das = append(das, rtr.GetData())
		}
	}

	// Sort by ID version.
	slices.SortFunc(das, func(lhs *dapb.DataAsset, rhs *dapb.DataAsset) int {
		return slices.Compare(idVersionAsKey(lhs.GetMetadata().GetIdVersion()), idVersionAsKey(rhs.GetMetadata().GetIdVersion()))
	})
	// Filter for the specified proto name, if any.
	if filter := req.GetStrictFilter().GetProtoName(); filter != "" {
		das = slices.DeleteFunc(das, func(o *dapb.DataAsset) bool {
			protoName, err := protoNameFromTypeURL(o.GetData().GetTypeUrl())
			if err != nil {
				return false
			}
			return protoName != filter
		})
	}

	// Keep only the requested page of results.
	das = slices.DeleteFunc(das, func(da *dapb.DataAsset) bool {
		return slices.Compare(idVersionAsKey(da.GetMetadata().GetIdVersion()), prevStartAfters) <= 0
	})
	if len(das) <= int(pageSize) {
		return &daspb.ListDataAssetsResponse{
			DataAssets: das,
		}, nil
	}
	das = das[:pageSize]

	// Get the next page token.
	nextStartAfters := idVersionAsKey(das[pageSize-1].GetMetadata().GetIdVersion())
	token, err := pageutils.EncodePageToken(nextStartAfters, false, []string{req.GetStrictFilter().GetProtoName()})
	if err != nil {
		return nil, err
	}

	return &daspb.ListDataAssetsResponse{
		DataAssets:    das,
		NextPageToken: token,
	}, nil
}

func (s *dataAssetsService) ListDataAssetMetadata(ctx context.Context, req *daspb.ListDataAssetMetadataRequest) (*daspb.ListDataAssetMetadataResponse, error) {
	listDataAssetsResponse, err := s.ListDataAssets(ctx, &daspb.ListDataAssetsRequest{
		StrictFilter: req.GetStrictFilter(),
		PageSize:     req.GetPageSize(),
		PageToken:    req.GetPageToken(),
	})
	if err != nil {
		return nil, err
	}

	metadata := make([]*mpb.Metadata, len(listDataAssetsResponse.GetDataAssets()))
	for i, da := range listDataAssetsResponse.GetDataAssets() {
		metadata[i] = da.GetMetadata()
	}
	return &daspb.ListDataAssetMetadataResponse{
		Metadata:      metadata,
		NextPageToken: listDataAssetsResponse.GetNextPageToken(),
	}, nil
}

func (s *dataAssetsService) GetDataAsset(ctx context.Context, req *daspb.GetDataAssetRequest) (*dapb.DataAsset, error) {
	reqID, err := idutils.IDFromProto(req.GetId())
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "%v", err)
	}

	// List all assets, then filter for the requested asset.
	idvs, _, err := s.rtrClient.List(ctx)
	if err != nil {
		return nil, fmt.Errorf("could not list runtime info for assets: %v", err)
	}
	idvsByID := make(map[string][]string)
	for _, idv := range idvs {
		parts, err := idutils.NewIDVersionParts(idv)
		if err != nil {
			return nil, fmt.Errorf("invalid state in cache: %v", err)
		}
		idvsByID[parts.ID()] = append(idvsByID[parts.ID()], idv)
	}

	reqIDVs, ok := idvsByID[reqID]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "requested id %q not found", reqID)
	}
	if len(reqIDVs) != 1 {
		return nil, status.Errorf(codes.Internal, "found more than one asset that could match %q: %v", reqID, strings.Join(reqIDVs, ","))
	}

	// Get the runtime for the asset.
	rtr, err := s.rtrClient.Get(ctx, reqIDVs[0])
	if err != nil {
		return nil, fmt.Errorf("could not find runtime info for asset %q: %v", reqIDVs[0], err)
	}
	if rtr.GetData() == nil {
		return nil, status.Errorf(codes.NotFound, "asset %q is not a data asset", reqID)
	}

	return rtr.GetData(), nil
}

func (s *dataAssetsService) StreamReferencedData(req *daspb.StreamReferencedDataRequest, stream dasgrpcpb.DataAssets_StreamReferencedDataServer) error {
	bufferSize := 1024 * 1024
	if bs := req.BufferSize; bs != nil {
		bufferSize = int(*bs)
	}
	if bufferSize <= 0 {
		return status.Errorf(codes.InvalidArgument, "buffer size must be positive")
	}

	// Find the specified ReferencedData.
	var ref *rdpb.ReferencedData
	switch r := req.GetReference().(type) {
	case *daspb.StreamReferencedDataRequest_Data:
		ref = r.Data
	case *daspb.StreamReferencedDataRequest_Path:
		da, err := s.GetDataAsset(stream.Context(), &daspb.GetDataAssetRequest{
			Id: r.Path.GetId(),
		})
		if err != nil {
			return err
		}

		// Extract the ReferencedData from the payload.
		payload, err := utils.ExtractPayload(da)
		if err != nil {
			return err
		}
		ref, err = extractReferencedDataField(payload.ProtoReflect(), r.Path.GetFieldPath())
		if err != nil {
			return err
		}
	case nil:
		return status.Errorf(codes.InvalidArgument, "no reference specified")
	default:
		return status.Errorf(codes.InvalidArgument, "unknown reference type: %T", req.GetReference())
	}

	refExt := referenceddata.FromProto(ref)
	switch refExt.Type() {
	case referenceddata.FileReferenceType:
		return status.Errorf(codes.InvalidArgument, "file references are not supported")
	case referenceddata.CASReferenceType:
		return status.Errorf(codes.Unimplemented, "CAS references are not supported")
	case referenceddata.InlinedReferenceType:
		offset := 0
		totalSize := len(refExt.Inlined())
		for chunk := range slices.Chunk(refExt.Inlined(), bufferSize) {
			if err := stream.Send(&daspb.StreamReferencedDataResponse{
				Chunk:     chunk,
				Offset:    int64(offset),
				TotalSize: int64(totalSize),
			}); err != nil {
				return err
			}
			offset += len(chunk)
		}
		return nil
	default:
		return status.Errorf(codes.InvalidArgument, "unknown data type: %T", ref.GetData())
	}
}

// extractReferencedDataField extracts the ReferencedData value at the specified field path within
// the input message.
func extractReferencedDataField(msg protoreflect.Message, fieldPath []*daspb.StreamReferencedDataRequest_ReferencedDataPath_FieldPathElement) (*rdpb.ReferencedData, error) {
	for _, pe := range fieldPath {
		descriptor := msg.Descriptor().Fields().ByName(protoreflect.Name(pe.GetField()))
		if descriptor == nil {
			return nil, status.Errorf(codes.InvalidArgument, "field %q not found in %q", pe.GetField(), msg.Descriptor().FullName())
		}

		switch pe.GetElement().(type) {
		case *daspb.StreamReferencedDataRequest_ReferencedDataPath_FieldPathElement_Index:
			if !descriptor.IsList() {
				return nil, status.Errorf(codes.InvalidArgument, "field %q is not a list", pe.GetField())
			}
			msg = msg.Get(descriptor).List().Get(int(pe.GetIndex())).Message()
		case *daspb.StreamReferencedDataRequest_ReferencedDataPath_FieldPathElement_Key:
			if !descriptor.IsMap() {
				return nil, status.Errorf(codes.InvalidArgument, "field %q is not a map", pe.GetField())
			}
			msg = msg.Get(descriptor).Map().Get(protoreflect.ValueOfString(pe.GetKey()).MapKey()).Message()
		case nil:
			msg = msg.Get(descriptor).Message()
		default:
			return nil, status.Errorf(codes.InvalidArgument, "unknown element type: %v", pe.GetElement())
		}
	}

	var ref *rdpb.ReferencedData
	var ok bool
	var err error
	ref, ok, err = referenceddata.ToReferencedData(msg.Interface())
	if err != nil {
		return nil, err
	}
	if !ok {
		return nil, status.Errorf(codes.InvalidArgument, "path %q does not point to a ReferencedData (got: %q)", fieldPath, msg.Descriptor().FullName())
	}

	return ref, nil
}

func protoNameFromTypeURL(typeURL string) (string, error) {
	// Any.type_url is of the form "type.googleapis.com/package.name.ProtoName".
	lastPathSep := strings.LastIndex(typeURL, "/")
	if lastPathSep == -1 {
		return "", status.Errorf(codes.InvalidArgument, "invalid type URL: %q", typeURL)
	}
	return typeURL[lastPathSep+1:], nil
}

func idVersionAsKey(idv *idpb.IdVersion) []string {
	return []string{
		idv.GetId().GetPackage(),
		idv.GetId().GetName(),
		idv.GetVersion(),
	}
}
