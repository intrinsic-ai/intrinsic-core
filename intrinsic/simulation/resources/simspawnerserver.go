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

// Package simspawnerserver defines a spawner server for when the resource is
// running in simulation and can establish a connection to the simulator. It is
// expected that the simulator exposes a service that corresponds to
// simspawnergrpc.SpawnerServiceClient and that the calling code knows which
// resource the spawner corresponds to.
package simspawnerserver

import (
	"context"
	"sync"
	"time"

	"intrinsic/simulation/resources/poserandomizer"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"gonum.org/v1/gonum/num/quat"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	sstatepb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	pointpb "intrinsic/math/proto/point_go_proto"
	posepb "intrinsic/math/proto/pose_go_proto"
	quatpb "intrinsic/math/proto/quaternion_go_proto"
	simspawnergrpcpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	simspawnerpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	spawnerconfigpb "intrinsic/simulation/resources/spawner_config_go_proto"
	spawnerpb "intrinsic/simulation/resources/spawner_server_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

// SimSpawnerServer uses a gRPC connection to the Gazebo simulator to forward
// requests to spawn objects from skills.
type SimSpawnerServer struct {
	cl             simspawnergrpcpb.SpawnerServiceClient
	retryInterval  time.Duration
	retryDuration  time.Duration
	poseRandomizer *poserandomizer.PoseRandomizer
	stateMu        sync.Mutex
	enabled        bool
}

// Create returns a new object that behaves as a spanwer resource server for the
// given connection to Gazebo and the given resource. The 'retryInterval'
// defines the interval with which we will retry sending RPCs to simulation if
// the simulator returns Unavailable, while 'retryDuration' is the amount of
// time we will spend retrying an RPC to simulation if the simulator returns
// Unavailable
func Create(
	cl simspawnergrpcpb.SpawnerServiceClient,
	cfg *spawnerconfigpb.SpawnerConfiguration,
	retryInterval time.Duration,
	retryDuration time.Duration,
) (*SimSpawnerServer, error) {
	if cl == nil {
		return nil, status.Error(codes.InvalidArgument, "Nonvalid simulation spawner service client")
	}

	poseRandomizer, err := poserandomizer.Create()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "error creating default pose randomizer: %v", err)
	}

	return &SimSpawnerServer{
		cl:             cl,
		retryInterval:  retryInterval,
		retryDuration:  retryDuration,
		poseRandomizer: poseRandomizer,
		enabled:        true,
	}, nil
}

func mulQuat(a, b *quatpb.Quaternion) *quatpb.Quaternion {
	aq := quat.Number{Real: a.GetW(), Imag: a.GetX(), Jmag: a.GetY(), Kmag: a.GetZ()}
	bq := quat.Number{Real: b.GetW(), Imag: b.GetX(), Jmag: b.GetY(), Kmag: b.GetZ()}
	result := quat.Mul(aq, bq)
	return &quatpb.Quaternion{W: result.Real, X: result.Imag, Y: result.Jmag, Z: result.Kmag}
}

func (s *SimSpawnerServer) poseForRequest(request *spawnerpb.SpawnObjectRequest) (*posepb.Pose, error) {
	origin := &posepb.Pose{
		Position:    &pointpb.Point{X: 0.0, Y: 0, Z: 0.0},
		Orientation: &quatpb.Quaternion{X: 0.0, Y: 0.0, Z: 0.0, W: 1.0},
	}

	if offset := request.GetSpawnOffset(); offset != nil {
		origin = offset
	}

	generatedPose := s.poseRandomizer.GeneratePose()

	if !request.RandomizePosition {
		generatedPose.Position = origin.Position
	} else if request.GetPositionalRandomizationDomain() != nil {
		return nil, status.Error(codes.InvalidArgument, "Cannot set both 'randomize_position' and 'positional_randomization_domain' in request")
	}

	if !request.RandomizeOrientation {
		generatedPose.Orientation = origin.Orientation
	} else {
		switch request.OrientationRandomization.(type) {
		default:
			return nil, status.Error(codes.InvalidArgument, "Cannot set both 'randomize_orientation' and one of 'orientation_randomization' in request")
		case nil:
			// do nothing.
		}
	}

	positionBBox := request.GetPositionalRandomizationDomain()
	if positionBBox != nil {
		xmin := generatedPose.GetPosition().GetX() - positionBBox.GetX()*0.5
		ymin := generatedPose.GetPosition().GetY() - positionBBox.GetY()*0.5
		zmin := generatedPose.GetPosition().GetZ() - positionBBox.GetZ()*0.5
		xmax := generatedPose.GetPosition().GetX() + positionBBox.GetX()*0.5
		ymax := generatedPose.GetPosition().GetY() + positionBBox.GetY()*0.5
		zmax := generatedPose.GetPosition().GetZ() + positionBBox.GetZ()*0.5
		generatedPose.Position = poserandomizer.BoundingBoxPositionDomain(
			xmin, ymin, zmin, xmax, ymax, zmax).Generate()
	}

	switch m := request.OrientationRandomization.(type) {
	case *spawnerpb.SpawnObjectRequest_RandomizeAnyOrientation:
		generatedPose.Orientation = poserandomizer.ArbitraryRotation().Generate()
	case *spawnerpb.SpawnObjectRequest_AxiallyConstrainedOrientation:
		axis := m.AxiallyConstrainedOrientation
		d, err := poserandomizer.AxiallyConstrainedRotation(axis.GetX(), axis.GetY(), axis.GetZ())
		if err != nil {
			return nil, err
		}
		generatedPose.Orientation = mulQuat(generatedPose.Orientation, d.Generate())
	case nil:
		// do nothing
	default:
		return nil, status.Error(codes.Unimplemented, "Unsupported orientation randomization")
	}

	return generatedPose, nil
}

// Forwards a spawn object request to the simulator.
func (s *SimSpawnerServer) SpawnObject(ctx context.Context, request *spawnerpb.SpawnObjectRequest) (*emptypb.Empty, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	if !s.enabled {
		return nil, status.Errorf(codes.FailedPrecondition, "Spawner is currently disabled")
	}

	if s.cl == nil {
		return nil, status.Error(codes.Internal, "Sim spawner service doesn't have valid connection to simulator")
	}

	sceneObject := request.GetSceneObject()
	if sceneObject == nil {
		return nil, status.Error(codes.InvalidArgument, "Spawner must receive a valid scene object to spawn.")
	}

	generatedPose, err := s.poseForRequest(request)
	if err != nil {
		return nil, err
	}
	log.InfoContextf(ctx, "Spawning object with pose: %v", generatedPose)

	// It could be the case that we request to spawn an object before the
	// simulator has had a chance to finish its reset. In this case, we may
	// be waiting for the gRPC dial to finish after at most retryInterval
	// duration. We will spin here for at most two minutes if such a condition
	// is encountered to give the resource a chance to reconnect to sim before
	// returning an error.
	startTime := time.Now()
	for {
		spawnRequest := &simspawnerpb.SpawnObjectRequest{
			SceneObject:   sceneObject,
			ParentTObject: generatedPose,
			Parent:        request.GetParent(),
			Name:          request.GetName(),
		}

		_, err = s.cl.SpawnObject(ctx, spawnRequest)
		if grpcStatus, ok := status.FromError(errors.Cause(err)); ok && grpcStatus.Code() == codes.Unavailable {
			if s.retryDuration-time.Since(startTime) < s.retryInterval {
				return &emptypb.Empty{}, status.Errorf(codes.DeadlineExceeded, "Spawner resource timed out when connecting to simulator: %v", err)
			}
			time.Sleep(s.retryInterval)
			continue
		}

		return &emptypb.Empty{}, err
	}
}

// Returns a potential spawn pose for the given spawn request.
func (s *SimSpawnerServer) GetSpawnPose(_ context.Context, req *spawnerpb.SpawnObjectRequest) (*posepb.Pose, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	if !s.enabled {
		return nil, status.Errorf(codes.FailedPrecondition, "Spawner is currently disabled")
	}
	return s.poseForRequest(req)
}

func (s *SimSpawnerServer) GetState(_ context.Context, req *sstatepb.GetStateRequest) (*sstatepb.SelfState, error) {
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

func (s *SimSpawnerServer) Enable(_ context.Context, req *sstatepb.EnableRequest) (*sstatepb.EnableResponse, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	s.enabled = true
	return &sstatepb.EnableResponse{}, nil
}

func (s *SimSpawnerServer) Disable(_ context.Context, req *sstatepb.DisableRequest) (*sstatepb.DisableResponse, error) {
	s.stateMu.Lock()
	defer s.stateMu.Unlock()
	s.enabled = false
	return &sstatepb.DisableResponse{}, nil
}
