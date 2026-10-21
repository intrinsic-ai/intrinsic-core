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

package main

/*
#include "intrinsic/util/cgo/c_types.h"
*/
import "C"

import (
	"context"
	"fmt"
	"math"
	"runtime/cgo"

	log "github.com/golang/glog"
	"go.opencensus.io/plugin/ocgrpc"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	wrapperpb "intrinsic/conductor/proto/wrapper_bridge_go_proto"
	rspb "intrinsic/config/proto/resource_set_go_proto"
	rdbgrpcpb "intrinsic/resources/proto/runtime_db_go_proto"
	"intrinsic/resources/service/resourcereader"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/resources/service/resourceworld"
	resourcesetservicepb "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service_go_proto"
	"intrinsic/util/cgo/cgoconvert"
	objectworldservicepb "intrinsic/world/public/proto/object_world_service_go_proto"
)

// handlePanic recovers from Go panics and propagates them as error statuses to the C++ caller,
// preventing process aborts across the CGO boundary.
func handlePanic(statusOut C.go_c_StatusOut) {
	if r := recover(); r != nil {
		err := fmt.Errorf("panic in Go: %v", r)
		cgoconvert.TranslateStatusOut(statusOut, err)
	}
}

//export go_c_ResourceWorldCreate
func go_c_ResourceWorldCreate(hssAddress, owsAddress C.go_c_StringIn, readerHandle C.go_c_Handle, handleOut *C.go_c_Handle, statusOut C.go_c_StatusOut) {
	defer handlePanic(statusOut)
	initGoRuntime()

	hss := cgoconvert.TranslateStringIn(hssAddress)
	ows := cgoconvert.TranslateStringIn(owsAddress)

	hssConn, err := grpc.NewClient(
		hss,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(1024*1024*1024)),
	)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
	resourceSetClient := resourcesetservicepb.NewHotSharedStateResourceSetServiceClient(hssConn)

	wconn, err := grpc.NewClient(
		ows,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
	)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
	objectWorldClient := objectworldservicepb.NewObjectWorldServiceClient(wconn)

	rgoHandle := cgo.Handle(readerHandle)
	rr, ok := rgoHandle.Value().(*resourcereader.Client)
	if !ok {
		cgoconvert.TranslateStatusOut(statusOut, fmt.Errorf("handle does not contain *resourcereader.Client"))
		return
	}

	client := resourceworld.NewClient(resourceworld.NewClientOpts{
		RR:        rr,
		RSSClient: resourceSetClient,
		OWSClient: objectWorldClient,
	})

	*handleOut = C.go_c_Handle(cgo.NewHandle(client))
}

//export go_c_ResourceReaderCreate
func go_c_ResourceReaderCreate(hssAddress, rtrAddress C.go_c_StringIn, handleOut *C.go_c_Handle, statusOut C.go_c_StatusOut) {
	defer handlePanic(statusOut)
	initGoRuntime()

	hss := cgoconvert.TranslateStringIn(hssAddress)
	rtr := cgoconvert.TranslateStringIn(rtrAddress)

	hssConn, err := grpc.NewClient(
		hss,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(1024*1024*1024)),
	)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
	resourceSetClient := resourcesetservicepb.NewHotSharedStateResourceSetServiceClient(hssConn)

	runtimeDBConn, err := grpc.NewClient(
		rtr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithStatsHandler(new(ocgrpc.ClientHandler)),
		grpc.WithDefaultCallOptions(grpc.MaxCallRecvMsgSize(math.MaxInt)),
		grpc.WithDefaultCallOptions(grpc.MaxCallSendMsgSize(math.MaxInt)),
	)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
	runtimeDBClient := rdbgrpcpb.NewRuntimeDbClient(runtimeDBConn)
	resourceTypeRuntimeClient := resourcetyperuntime.CreateClient(runtimeDBClient)

	rr := resourcereader.NewClient(resourcereader.NewClientOpts{
		RSSClient: resourceSetClient,
		RTRClient: resourceTypeRuntimeClient,
	})

	*handleOut = C.go_c_Handle(cgo.NewHandle(rr))
}

//export go_c_ResourceWorldClose
func go_c_ResourceWorldClose(h C.go_c_Handle) {
	// Catches panics to prevent crash.
	defer func() {
		if r := recover(); r != nil {
			log.Errorf("Panic in go_c_ResourceWorldClose: %v", r)
		}
	}()
	cgo.Handle(h).Delete()
}

//export go_c_ResourceReaderClose
func go_c_ResourceReaderClose(h C.go_c_Handle) {
	// Catches panics to prevent crash.
	defer func() {
		if r := recover(); r != nil {
			log.Errorf("Panic in go_c_ResourceReaderClose: %v", r)
		}
	}()
	cgo.Handle(h).Delete()
}

//export go_c_ResourceWorldUpdateResourceSetWorldRelations
func go_c_ResourceWorldUpdateResourceSetWorldRelations(handle C.go_c_Handle, localWorldID C.go_c_StringIn, statusOut C.go_c_StatusOut) {
	defer handlePanic(statusOut)
	client, ok := cgo.Handle(handle).Value().(*resourceworld.Client)
	if !ok {
		cgoconvert.TranslateStatusOut(statusOut, fmt.Errorf("handle does not contain *resourceworld.Client"))
		return
	}
	worldID := cgoconvert.TranslateStringIn(localWorldID)
	ctx := context.Background()
	err := client.UpdateResourceSetWorldRelations(ctx, worldID)
	cgoconvert.TranslateStatusOut(statusOut, err)
}

//export go_c_ResourceReaderGeometricResourceInstanceData
func go_c_ResourceReaderGeometricResourceInstanceData(handle C.go_c_Handle, rsIn C.go_c_ProtoIn, out C.go_c_ProtoOut, statusOut C.go_c_StatusOut) {
	defer handlePanic(statusOut)
	reader, ok := cgo.Handle(handle).Value().(*resourcereader.Client)
	if !ok {
		cgoconvert.TranslateStatusOut(statusOut, fmt.Errorf("handle does not contain *resourcereader.Client"))
		return
	}

	rs := &rspb.ResourceSet{}
	if err := cgoconvert.TranslateProtoIn(rsIn, rs); err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}

	ctx := context.Background()
	data, err := reader.GeometricResourceInstanceData(ctx, rs)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}

	resp := &wrapperpb.GeometricResourceSetData{
		Data: data,
	}
	if err := cgoconvert.TranslateProtoOut(out, resp); err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
}

//export go_c_ResourceReaderGeometricResourceSetData
func go_c_ResourceReaderGeometricResourceSetData(handle C.go_c_Handle, out C.go_c_ProtoOut, statusOut C.go_c_StatusOut) {
	defer handlePanic(statusOut)
	reader, ok := cgo.Handle(handle).Value().(*resourcereader.Client)
	if !ok {
		cgoconvert.TranslateStatusOut(statusOut, fmt.Errorf("handle does not contain *resourcereader.Client"))
		return
	}

	ctx := context.Background()
	data, owu, err := reader.GetGeometricResourceSetData(ctx)
	if err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}

	resp := &wrapperpb.GeometricResourceSetData{
		Data: data,
		Owu:  owu,
	}
	if err := cgoconvert.TranslateProtoOut(out, resp); err != nil {
		cgoconvert.TranslateStatusOut(statusOut, err)
		return
	}
}
