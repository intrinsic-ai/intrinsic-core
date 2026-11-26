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

package simoutfeedserver

import (
	"context"
	"testing"
	"time"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	vecpb "intrinsic/math/proto/vector3_go_proto"
	simoutfeedgrpcpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	simoutfeedpb "intrinsic/simulation/gazebo/proto/outfeed_service_go_proto"
	outfeedconfigpb "intrinsic/simulation/resources/outfeed_config_go_proto"
	outfeedpb "intrinsic/simulation/resources/outfeed_server_go_proto"
)

type fakeSimOutfeedService struct {
	count int
}

func trivialFakeSimOutfeed() *fakeSimOutfeedService {
	return &fakeSimOutfeedService{
		count: 0,
	}
}

func (so *fakeSimOutfeedService) ProcessOutfeed(ctx context.Context, request *simoutfeedpb.ProcessOutfeedRequest, opts ...grpc.CallOption) (*simoutfeedpb.ProcessOutfeedResponse, error) {
	if so.count == 0 {
		return &simoutfeedpb.ProcessOutfeedResponse{}, nil
	}

	so.count--
	return nil, status.Errorf(codes.Unavailable, "%d", so.count+1)
}

func TestCreate(t *testing.T) {
	tests := []struct {
		client            simoutfeedgrpcpb.OutfeedServiceClient
		outfeedConfig     *outfeedconfigpb.OutfeedConfiguration
		expectedErrorCode codes.Code
	}{
		{
			client:            nil,
			expectedErrorCode: codes.InvalidArgument,
		},
		{
			client:            trivialFakeSimOutfeed(),
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

		if _, err := Create(test.client, retryInterval, retryDuration); err != nil {
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

func TestProcessOutfeed(t *testing.T) {
	tests := []struct {
		name                string
		numUnavailableCalls int
		retryInterval       time.Duration
		retryDuration       time.Duration
		expectedErrorCode   codes.Code
	}{
		{
			name:                "Simple process outfeed",
			numUnavailableCalls: 0,
			retryInterval:       time.Second,
			retryDuration:       5 * time.Second,
			expectedErrorCode:   codes.OK,
		},
		{
			name:                "Process outfeed returns unavailable",
			numUnavailableCalls: 5,
			retryInterval:       time.Second,
			retryDuration:       10 * time.Second,
			expectedErrorCode:   codes.OK,
		},
		{
			name:                "Process outfeed times out",
			numUnavailableCalls: 11,
			retryInterval:       time.Second,
			retryDuration:       10 * time.Second,
			expectedErrorCode:   codes.DeadlineExceeded,
		},
		{
			name:                "Process outfeed times out with longer backoff",
			numUnavailableCalls: 4,
			retryInterval:       3 * time.Second,
			retryDuration:       10 * time.Second,
			expectedErrorCode:   codes.DeadlineExceeded,
		},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			simOutfeedServer, err := Create(
				&fakeSimOutfeedService{
					count: test.numUnavailableCalls,
				},
				test.retryInterval,
				test.retryDuration)
			if err != nil {
				t.Errorf("Unable to create simOutfeedServer: %v", err)
			}

			request := &outfeedpb.ProcessOutfeedRequest{
				Position: &vecpb.Vector3{X: 1.0, Y: 2.0, Z: 3.0},
			}
			if _, err := simOutfeedServer.ProcessOutfeed(context.Background(), request); err != nil {
				if st, ok := status.FromError(err); !ok {
					t.Errorf("Received non-status error: %v", err)
				} else if st.Code() != test.expectedErrorCode {
					t.Errorf("SpawnObject(%v): %q, want %q: %v", request, st.Code(), test.expectedErrorCode, st.Message())
				}
			} else if test.expectedErrorCode != codes.OK {
				t.Errorf("SpawnObject(%v): OK, want %q.", request, test.expectedErrorCode)
			}
		})
	}
}
