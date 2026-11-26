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

// Package simoutfeedserver defines a outfeed server for when the resource is
// running in simulation and can establish a connection to the simulator. It is
// expected that the simulator exposes a service that corresponds to
// simoutfeedgrpcpb.OutfeedServiceClient and that the calling code knows which
// resource the outfeed corresponds to.
package simoutfeedserver

import (
	"context"
	"sync"
	"time"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	sstatepb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	simoutfeedgrpcpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	simoutfeedpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	outfeedpb "intrinsic/simulation/resources/outfeed_server_go_proto"
)

// SimOutfeedServer uses a gRPC connection to the Gazebo simulator to forward
// requests to remove objects from skills.
type SimOutfeedServer struct {
	cl            simoutfeedgrpcpb.OutfeedServiceClient
	retryInterval time.Duration
	retryDuration time.Duration
	stateMu       sync.Mutex
	enabled       bool
}

// Create returns a new object that behaves as an outfeed resource server for the
// given connection to Gazebo and the given resource. The 'retryInterval'
// defines the interval with which we will retry sending RPCs to simulation if
// the simulator returns Unavailable, while 'retryDuration' is the amount of
// time we will spend retrying an RPC to simulation if the simulator returns
// Unavailable
func Create(
	cl simoutfeedgrpcpb.OutfeedServiceClient,
	retryInterval time.Duration,
	retryDuration time.Duration,
) (*SimOutfeedServer, error) {
	if cl == nil {
		return nil, status.Error(codes.InvalidArgument, "Nonvalid simulation outfeed service client")
	}

	return &SimOutfeedServer{
		cl:            cl,
		retryInterval: retryInterval,
		retryDuration: retryDuration,
		enabled:       true,
	}, nil
}

// Forwards a spawn object request to the simulator.
func (s *SimOutfeedServer) ProcessOutfeed(ctx context.Context, req *outfeedpb.ProcessOutfeedRequest) (*outfeedpb.ProcessOutfeedResponse, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	if !s.enabled {
		return nil, status.Errorf(codes.FailedPrecondition, "Outfeed is currently disabled")
	}

	if s.cl == nil {
		return nil, status.Error(codes.Internal, "Sim outfeed service doesn't have valid connection to simulator")
	}

	if req.GetPosition() == nil {
		return nil, status.Error(codes.InvalidArgument, "Outfeed requires at least a position set.")
	}
	log.InfoContextf(ctx, "Processing outfeed request: %v", req)
	simReq := &simoutfeedpb.ProcessOutfeedRequest{
		Position: req.GetPosition(),
		Bounds:   req.GetBounds(),
		Prefix:   req.GetPrefix(),
	}

	// It could be the case that we request to remove an object before the
	// simulator has had a chance to finish its reset. In this case, we may
	// be waiting for the gRPC dial to finish after at most retryInterval
	// duration. We will spin here for at most two minutes if such a condition
	// is encountered to give the resource a chance to reconnect to sim before
	// returning an error.
	startTime := time.Now()
	for {
		_, err := s.cl.ProcessOutfeed(ctx, simReq)
		if grpcStatus, ok := status.FromError(errors.Cause(err)); ok && grpcStatus.Code() == codes.Unavailable {
			if s.retryDuration-time.Since(startTime) < s.retryInterval {
				return nil, status.Errorf(codes.DeadlineExceeded, "Outfeed resource timed out when connecting to simulator: %v", err)
			}
			time.Sleep(s.retryInterval)
			continue
		}

		return &outfeedpb.ProcessOutfeedResponse{}, err
	}
}

func (s *SimOutfeedServer) GetState(_ context.Context, req *sstatepb.GetStateRequest) (*sstatepb.SelfState, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()

	// TODO: b/415240703 -- Actually report errors like not being able to connect
	// to the simulator.
	if s.enabled {
		return &sstatepb.SelfState{StateCode: sstatepb.SelfState_STATE_CODE_ENABLED}, nil
	} else {
		return &sstatepb.SelfState{StateCode: sstatepb.SelfState_STATE_CODE_DISABLED}, nil
	}
}

func (s *SimOutfeedServer) Enable(_ context.Context, req *sstatepb.EnableRequest) (*sstatepb.EnableResponse, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	s.enabled = true
	return &sstatepb.EnableResponse{}, nil
}

func (s *SimOutfeedServer) Disable(_ context.Context, req *sstatepb.DisableRequest) (*sstatepb.DisableResponse, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	s.enabled = false
	return &sstatepb.DisableResponse{}, nil
}
