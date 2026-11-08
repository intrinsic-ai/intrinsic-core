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

package icon

import (
	"context"
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	p2pmpb "intrinsic/icon/actions/point_to_point_move_go_proto"
	streamjpb "intrinsic/icon/actions/streaming_joint_position_go_proto"
	jointpb "intrinsic/icon/proto/joint_space_go_proto"
	streampb "intrinsic/icon/proto/streaming_output_go_proto"
	grpcpb "intrinsic/icon/proto/v1/service_go_proto"
	servicepb "intrinsic/icon/proto/v1/service_go_proto"
	typespb "intrinsic/icon/proto/v1/types_go_proto"

	statuspb "google.golang.org/genproto/googleapis/rpc/status"
	anypb "google.golang.org/protobuf/types/known/anypb"
	wpb "google.golang.org/protobuf/types/known/wrapperspb"
)

type writeStreamRequestResponse struct {
	wantReq  *servicepb.OpenWriteStreamRequest
	wantResp *servicepb.OpenWriteStreamResponse
}

type stubWriteStreamServer struct {
	grpcpb.UnimplementedIconApiServer
	actionReqResps []*actionRequestResponse
	streamReqResps []*writeStreamRequestResponse
}

func (s *stubWriteStreamServer) OpenSession(srv grpcpb.IconApi_OpenSessionServer) error {
	for _, reqResp := range s.actionReqResps {
		req, err := srv.Recv()
		if err != nil {
			return fmt.Errorf("request failed: %s", err)
		}
		if len(s.actionReqResps) == 0 {
			return fmt.Errorf("missing request and response pair")
		}
		if reqResp.wantReq == nil {
			return fmt.Errorf("missing request")
		}
		if reqResp.wantResp == nil {
			return fmt.Errorf("missing response")
		}

		if diff := cmp.Diff(req, reqResp.wantReq, protocmp.Transform()); diff != "" {
			return fmt.Errorf("unexpected request (-want +got):\n%s", diff)
		}
		srv.Send(reqResp.wantResp)
	}
	return nil
}

func (s *stubWriteStreamServer) OpenWriteStream(srv grpcpb.IconApi_OpenWriteStreamServer) error {
	for _, reqResp := range s.streamReqResps {
		req, err := srv.Recv()
		if err != nil {
			return fmt.Errorf("request failed: %s", err)
		}
		if len(s.streamReqResps) == 0 {
			return fmt.Errorf("missing request and response pair")
		}
		if reqResp.wantReq == nil {
			return fmt.Errorf("missing request")
		}
		if reqResp.wantResp == nil {
			return fmt.Errorf("missing response")
		}
		if diff := cmp.Diff(req, reqResp.wantReq, protocmp.Transform()); diff != "" {
			return fmt.Errorf("unexpected request (-want +got):\n%s", diff)
		}
		srv.Send(reqResp.wantResp)
	}
	return nil
}

func stubActionDesc() *ActionDescription {
	return &ActionDescription{
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.streaming_joint_position_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData: FromPartName("my_part"),
	}
}

func mustStubWriteStreamServer(t *testing.T, actionDesc *ActionDescription, msg proto.Message) *stubWriteStreamServer {
	ap, _ := anypb.New(actionDesc.Params)
	am, _ := anypb.New(msg)
	return &stubWriteStreamServer{
		actionReqResps: []*actionRequestResponse{
			// Init the session.
			{
				wantReq: &servicepb.OpenSessionRequest{
					InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
						AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
					},
				},
				wantResp: &servicepb.OpenSessionResponse{
					Status:             &statuspb.Status{Code: 0},
					InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
				},
			},
			// Create the action.
			{
				wantReq: &servicepb.OpenSessionRequest{
					ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
						AddActionsAndReactions: &typespb.ActionsAndReactions{ActionInstances: []*typespb.ActionInstance{{
							ActionInstanceId: 1,
							ActionTypeName:   actionDesc.ActionType,
							FixedParameters:  ap,
							SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
						}}},
					},
				},
				wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: 0}},
			},
			// Start the action.
			{
				wantReq: &servicepb.OpenSessionRequest{
					StartActionsRequest: &servicepb.OpenSessionRequest_StartActionsRequestData{
						ActionInstanceIds: []int64{1},
						StopActiveActions: true,
					},
				},
				wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: 0}},
			},
		},
		streamReqResps: []*writeStreamRequestResponse{
			// Create the stream.
			{
				wantReq: &servicepb.OpenWriteStreamRequest{
					AddWriteStream: &servicepb.AddStreamRequest{ActionId: 1, FieldName: "test-field-name"},
					SessionId:      1,
				},
				wantResp: &servicepb.OpenWriteStreamResponse{
					StreamOperationResponse: &servicepb.OpenWriteStreamResponse_AddStreamResponse{
						AddStreamResponse: &servicepb.AddStreamResponse{
							Status: &statuspb.Status{Code: 0},
						},
					},
				},
			},
			// Write a message to the action.
			{
				wantReq: &servicepb.OpenWriteStreamRequest{
					WriteValue: &servicepb.OpenWriteStreamRequest_WriteValue{
						Value: am,
					},
				},
				wantResp: &servicepb.OpenWriteStreamResponse{
					WriteValueResponse: &statuspb.Status{Code: 0},
				},
			},
		},
	}
}

func TestWriteStream_End(t *testing.T) {
	actionDesc := stubActionDesc()
	msg := &streamjpb.StreamingJointPositionParams{}
	srv := mustStubWriteStreamServer(t, actionDesc, msg)
	s := mustStartTestSession(t, srv)
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.StartAction(ais[0]); err != nil {
		t.Errorf("StartAction failed: %s", err)
	}
	st, err := s.OpenWriteStream(context.Background(), ais[0], "test-field-name")
	if err != nil {
		t.Fatalf("OpenWriteStream failed: %s", err)
	}
	if err = st.Close(); err != nil {
		t.Fatalf("Close failed: %s", err)
	}
	if err = st.Write(msg); !errors.Is(err, ErrStreamClosed) {
		t.Fatalf("Write err %v, want %v", err, ErrStreamClosed)
	}
}

func TestWriteStream_Write(t *testing.T) {
	actionDesc := stubActionDesc()
	msg := &streamjpb.StreamingJointPositionParams{}
	srv := mustStubWriteStreamServer(t, actionDesc, msg)
	s := mustStartTestSession(t, srv)
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.StartAction(ais[0]); err != nil {
		t.Errorf("StartAction failed: %s", err)
	}
	st, err := s.OpenWriteStream(context.Background(), ais[0], "test-field-name")
	if err != nil {
		t.Fatalf("OpenWriteStream failed: %s", err)
	}
	if err = st.Write(msg); err != nil {
		t.Fatalf("Write failed: %s", err)
	}
}

type readStreamRequestResponse struct {
	wantReq  *servicepb.GetLatestStreamingOutputRequest
	wantResp *servicepb.GetLatestStreamingOutputResponse
}

type stubReadStreamServer struct {
	grpcpb.UnimplementedIconApiServer
	actionReqResps []*actionRequestResponse
	streamReqResps []*readStreamRequestResponse
}

func (s *stubReadStreamServer) OpenSession(srv grpcpb.IconApi_OpenSessionServer) error {
	for _, reqResp := range s.actionReqResps {
		req, err := srv.Recv()
		if err != nil {
			return fmt.Errorf("request failed: %s", err)
		}
		if len(s.actionReqResps) == 0 {
			return fmt.Errorf("missing request and response pair")
		}
		if reqResp.wantReq == nil {
			return fmt.Errorf("missing request")
		}
		if reqResp.wantResp == nil {
			return fmt.Errorf("missing response")
		}

		if diff := cmp.Diff(req, reqResp.wantReq, protocmp.Transform()); diff != "" {
			return fmt.Errorf("unexpected request (-want +got):\n%s", diff)
		}
		srv.Send(reqResp.wantResp)
	}
	return nil
}

func (s *stubReadStreamServer) GetLatestStreamingOutput(ctx context.Context, req *servicepb.GetLatestStreamingOutputRequest) (*servicepb.GetLatestStreamingOutputResponse, error) {
	if len(s.streamReqResps) == 0 {
		return nil, fmt.Errorf("missing request and response pair")
	}
	var reqResp *readStreamRequestResponse
	reqResp, s.streamReqResps = s.streamReqResps[0], s.streamReqResps[1:]
	if diff := cmp.Diff(req, reqResp.wantReq, protocmp.Transform()); diff != "" {
		return nil, fmt.Errorf("unexpected request (-want +got):\n%s", diff)
	}
	return reqResp.wantResp, nil
}

func mustStubReadStreamServer(t *testing.T, actionDesc *ActionDescription, msg proto.Message) *stubReadStreamServer {
	ai := int64(1)
	a, err := anypb.New(msg)
	if err != nil {
		t.Fatal(err)
	}
	ap, err := anypb.New(actionDesc.Params)
	if err != nil {
		t.Fatal(err)
	}
	return &stubReadStreamServer{
		actionReqResps: []*actionRequestResponse{
			// Init the session.
			{
				wantReq: &servicepb.OpenSessionRequest{
					InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
						AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
					},
				},
				wantResp: &servicepb.OpenSessionResponse{
					Status:             &statuspb.Status{Code: 0},
					InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
				},
			},
			// Create the action.
			{
				wantReq: &servicepb.OpenSessionRequest{
					ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
						AddActionsAndReactions: &typespb.ActionsAndReactions{
							ActionInstances: []*typespb.ActionInstance{{
								ActionInstanceId: ai,
								ActionTypeName:   actionDesc.ActionType,
								FixedParameters:  ap,
								SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
							}},
						},
					},
				},
				wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: 0}},
			},
			// Start the action.
			{
				wantReq: &servicepb.OpenSessionRequest{
					StartActionsRequest: &servicepb.OpenSessionRequest_StartActionsRequestData{
						ActionInstanceIds: []int64{ai},
						StopActiveActions: true,
					},
				},
				wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: 0}},
			},
		},
		streamReqResps: []*readStreamRequestResponse{
			// Read a message from the action.
			{
				wantReq: &servicepb.GetLatestStreamingOutputRequest{
					// SessionId: <s.id>
					ActionId: uint64(1),
				},
				wantResp: &servicepb.GetLatestStreamingOutputResponse{
					Output: &streampb.StreamingOutput{TimestampNs: 1, Payload: a},
				},
			},
		},
	}
}

func TestReadStream_Read(t *testing.T) {
	actionDesc := stubActionDesc()
	msg := &wpb.FloatValue{Value: 1.123}
	srv := mustStubReadStreamServer(t, actionDesc, msg)
	s := mustStartTestSession(t, srv)
	srv.streamReqResps[0].wantReq.SessionId = s.id
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.StartAction(ais[0]); err != nil {
		t.Errorf("StartAction failed: %s", err)
	}
	st, err := s.OpenReadStream(ais[0])
	if err != nil {
		t.Fatalf("OpenWriteStream failed: %s", err)
	}
	ts, resp, err := st.Read(context.Background())
	if err != nil {
		t.Fatalf("Read failed: %s", err)
	}
	if ts != 1 {
		t.Fatalf("missing timestamp: %v", ts)
	}
	rec := &wpb.FloatValue{}
	if err = resp.UnmarshalTo(rec); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(rec, msg, protocmp.Transform()); diff != "" {
		t.Fatalf("unexpected request (-want +got):\n%s", diff)
	}
}

func TestReadStream_ReadUnpacked(t *testing.T) {
	actionDesc := stubActionDesc()
	msg := &wpb.FloatValue{Value: 1.123}
	srv := mustStubReadStreamServer(t, actionDesc, msg)
	s := mustStartTestSession(t, srv)
	srv.streamReqResps[0].wantReq.SessionId = s.id
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.StartAction(ais[0]); err != nil {
		t.Errorf("StartAction failed: %s", err)
	}
	st, err := s.OpenReadStream(ais[0])
	if err != nil {
		t.Fatalf("OpenWriteStream failed: %s", err)
	}
	rec := &wpb.FloatValue{}
	ts, err := st.ReadUnpacked(context.Background(), rec)
	if err != nil {
		t.Fatalf("Read failed: %s", err)
	}
	if ts != 1 {
		t.Fatalf("missing timestamp: %v", ts)
	}
	if diff := cmp.Diff(rec, msg, protocmp.Transform()); diff != "" {
		t.Fatalf("unexpected request (-want +got):\n%s", diff)
	}
}
