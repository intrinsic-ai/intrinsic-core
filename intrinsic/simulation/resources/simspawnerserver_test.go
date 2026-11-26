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

package simspawnerserver

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/testing/protocmp"

	pointpb "intrinsic/math/proto/point_go_proto"
	posepb "intrinsic/math/proto/pose_go_proto"
	quatpb "intrinsic/math/proto/quaternion_go_proto"
	vecpb "intrinsic/math/proto/vector3_go_proto"
	sopb "intrinsic/scene/proto/v1/scene_object_go_proto"
	simspawnergrpcpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	simspawnerpb "intrinsic/simulation/gazebo/proto/spawner_service_go_proto"
	spawnerconfigpb "intrinsic/simulation/resources/spawner_config_go_proto"
	spawnerpb "intrinsic/simulation/resources/spawner_server_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

type fakeSimSpawnerService struct {
	count        int
	expectedPose *posepb.Pose
	expectedName string
}

func trivialFakeSimSpawner() *fakeSimSpawnerService {
	return &fakeSimSpawnerService{
		count:        0,
		expectedPose: nil,
		expectedName: "",
	}
}

func (ss *fakeSimSpawnerService) SpawnObject(ctx context.Context, request *simspawnerpb.SpawnObjectRequest, opts ...grpc.CallOption) (*emptypb.Empty, error) {
	if ss.expectedPose != nil {
		if diff := cmp.Diff(ss.expectedPose, request.ParentTObject, protocmp.Transform()); diff != "" {
			return nil, fmt.Errorf("SpawnObjectRequest.ParentTObject(%s) diff (-want +got):\n%s", ss.expectedPose, diff)
		}
	}

	if ss.expectedName != request.Name {
		return nil, fmt.Errorf("SpawnObjectRequest.Name: got %q, want %q", request.Name, ss.expectedName)
	}

	if ss.count == 0 {
		return &emptypb.Empty{}, nil
	}

	ss.count--
	return nil, status.Errorf(codes.Unavailable, "%d", ss.count+1)
}

func TestCreate(t *testing.T) {
	tests := []struct {
		client            simspawnergrpcpb.SpawnerServiceClient
		spawnerConfig     *spawnerconfigpb.SpawnerConfiguration
		expectedErrorCode codes.Code
	}{
		{
			client:            nil,
			spawnerConfig:     nil,
			expectedErrorCode: codes.InvalidArgument,
		},
		{
			client:            trivialFakeSimSpawner(),
			spawnerConfig:     nil,
			expectedErrorCode: codes.OK,
		},
	}

	for _, test := range tests {
		clientStr := "<client>"
		if test.client == nil {
			clientStr = "nil"
		}

		retryInterval := time.Since(time.UnixMicro(0))
		retryDuration := time.Since(time.UnixMicro(0))

		if _, err := Create(test.client, test.spawnerConfig, retryInterval, retryDuration); err != nil {
			if st, ok := status.FromError(err); !ok {
				t.Errorf("Received non-status error: %v", err)
			} else if st.Code() != test.expectedErrorCode {
				t.Errorf("Create(%s): '%q', want '%q'", clientStr, st.Code(), test.expectedErrorCode)
			}
		} else if test.expectedErrorCode != codes.OK {
			t.Errorf("Create(%s): OK, want '%q.", clientStr, test.expectedErrorCode)
		}
	}
}

func TestSpawnObject(t *testing.T) {
	testSpawnerConfig := &spawnerconfigpb.SpawnerConfiguration{}

	tests := []struct {
		name                   string
		numUnavailableCalls    int
		retryInterval          time.Duration
		retryDuration          time.Duration
		request                *spawnerpb.SpawnObjectRequest
		expectedRandomizedPose *posepb.Pose
		expectedName           string
		expectedErrorCode      codes.Code
	}{
		{
			name:                "Simple spawn object",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			retryDuration:       5 * time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizePosition:    false,
				RandomizeOrientation: false,
				SceneObject:          &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedName:           "",
			expectedErrorCode:      codes.OK,
		},
		{
			name:                "Named spawn object",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			retryDuration:       5 * time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				Name:                 "test_object_name",
				RandomizePosition:    false,
				RandomizeOrientation: false,
				SceneObject:          &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedName:           "test_object_name",
			expectedErrorCode:      codes.OK,
		},
		{
			name:                "Spawn object returns unavailable",
			numUnavailableCalls: 5,
			retryInterval:       time.Second,
			retryDuration:       10 * time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizePosition:    false,
				RandomizeOrientation: false,
				SceneObject:          &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.OK,
		},
		{
			name:                "Spawn object times out",
			numUnavailableCalls: 11,
			retryInterval:       time.Second,
			retryDuration:       10 * time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizePosition:    false,
				RandomizeOrientation: false,
				SceneObject:          &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.DeadlineExceeded,
		},
		{
			name:                "Spawn object times out with longer backoff",
			numUnavailableCalls: 4,
			retryInterval:       3 * time.Second,
			retryDuration:       10 * time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizePosition:    false,
				RandomizeOrientation: false,
				SceneObject:          &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.DeadlineExceeded,
		},
		{
			name:                "Spawn object errors with both position config and request randomization",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizePosition:             true,
				PositionalRandomizationDomain: &vecpb.Vector3{X: 1, Y: 2, Z: 3},
				SceneObject:                   &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.InvalidArgument,
		},
		{
			name:                "Spawn object errors with both rotation config and request randomization",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				RandomizeOrientation: true,
				OrientationRandomization: &spawnerpb.SpawnObjectRequest_RandomizeAnyOrientation{
					RandomizeAnyOrientation: true,
				},
				SceneObject: &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.InvalidArgument,
		},
		{
			name:                "Spawn object doesn't randomize empty bounding boxes",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				PositionalRandomizationDomain: &vecpb.Vector3{X: 0, Y: 0, Z: 0},
				SpawnOffset: &posepb.Pose{
					Position:    &pointpb.Point{X: 1, Y: 2, Z: 3},
					Orientation: &quatpb.Quaternion{X: 0, Y: 0, Z: 0, W: 1},
				},
				SceneObject: &sopb.SceneObject{},
			},
			expectedRandomizedPose: &posepb.Pose{
				Position:    &pointpb.Point{X: 1, Y: 2, Z: 3},
				Orientation: &quatpb.Quaternion{X: 0, Y: 0, Z: 0, W: 1},
			},
			expectedErrorCode: codes.OK,
		},
		{
			name:                "Spawn with non-empty bounding box succeeds",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				PositionalRandomizationDomain: &vecpb.Vector3{X: 1, Y: 2, Z: 3},
				SpawnOffset: &posepb.Pose{
					Position:    &pointpb.Point{X: 1, Y: 2, Z: 3},
					Orientation: &quatpb.Quaternion{X: 0, Y: 0, Z: 0, W: 1},
				},
				SceneObject: &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.OK,
		},
		{
			name:                "Spawn with any rotation succeeds",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				PositionalRandomizationDomain: &vecpb.Vector3{X: 1, Y: 2, Z: 3},
				OrientationRandomization: &spawnerpb.SpawnObjectRequest_RandomizeAnyOrientation{
					RandomizeAnyOrientation: true,
				},
				SpawnOffset: &posepb.Pose{
					Position:    &pointpb.Point{X: 1, Y: 2, Z: 3},
					Orientation: &quatpb.Quaternion{X: 0, Y: 0, Z: 0, W: 1},
				},
				SceneObject: &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.OK,
		},
		{
			name:                "Spawn with axially constrained rotation succeeds",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			request: &spawnerpb.SpawnObjectRequest{
				PositionalRandomizationDomain: &vecpb.Vector3{X: 1, Y: 2, Z: 3},
				OrientationRandomization: &spawnerpb.SpawnObjectRequest_AxiallyConstrainedOrientation{
					AxiallyConstrainedOrientation: &vecpb.Vector3{X: 1, Y: 1, Z: 1},
				},
				SpawnOffset: &posepb.Pose{
					Position:    &pointpb.Point{X: 1, Y: 2, Z: 3},
					Orientation: &quatpb.Quaternion{X: 0, Y: 0, Z: 0, W: 1},
				},
				SceneObject: &sopb.SceneObject{},
			},
			expectedRandomizedPose: nil,
			expectedErrorCode:      codes.OK,
		},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			simSpawnerServer, err := Create(
				&fakeSimSpawnerService{
					count:        test.numUnavailableCalls,
					expectedPose: test.expectedRandomizedPose,
					expectedName: test.expectedName,
				},
				testSpawnerConfig,
				test.retryInterval,
				test.retryDuration)
			if err != nil {
				t.Errorf("Unable to create simSpawnerServer: %v", err)
			}

			if _, err := simSpawnerServer.SpawnObject(context.Background(), test.request); err != nil {
				if st, ok := status.FromError(err); !ok {
					t.Errorf("Received non-status error: %v", err)
				} else if st.Code() != test.expectedErrorCode {
					t.Errorf("SpawnObject(%v): '%q', want '%q'.", test.request, st.Code(), test.expectedErrorCode)
				}
			} else if test.expectedErrorCode != codes.OK {
				t.Errorf("SpawnObject(%v): OK, want '%q'.", test.request, test.expectedErrorCode)
			}
		})
	}
}
