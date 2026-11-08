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
	"errors"
	"fmt"
	"io"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc/codes"
	"google.golang.org/protobuf/testing/protocmp"

	p2pmpb "intrinsic/icon/actions/point_to_point_move_go_proto"
	jointpb "intrinsic/icon/proto/joint_space_go_proto"
	grpcpb "intrinsic/icon/proto/v1/service_go_proto"
	servicepb "intrinsic/icon/proto/v1/service_go_proto"
	typespb "intrinsic/icon/proto/v1/types_go_proto"
	cpb "intrinsic/logging/proto/context_go_proto"

	statuspb "google.golang.org/genproto/googleapis/rpc/status"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

// actionRequestResponse is a request/response pair where wantReq is the message the
// ICON server expects to receive from the client and wantResp is the message
// the ICON client expects from the server.
type actionRequestResponse struct {
	wantReq  *servicepb.OpenSessionRequest
	wantResp *servicepb.OpenSessionResponse
	// end signifies that the streaming RPC should be closed after handling this
	// request.
	end bool
}

type stubSessionServer struct {
	grpcpb.UnimplementedIconApiServer
	// List of expected requests and the corresponding responses to return,
	// when handling the OpenSession streaming RPC method. Additional
	// entries can be added in-between requests by appending to this list.
	actionReqResps []*actionRequestResponse
}

func (s *stubSessionServer) OpenSession(srv grpcpb.IconApi_OpenSessionServer) error {
	for {
		req, err := srv.Recv()
		if err != nil {
			return fmt.Errorf("request failed: %s", err)
		}
		if len(s.actionReqResps) == 0 {
			return fmt.Errorf("missing request and response pair")
		}
		// Shift the slice and consume the first request/response pair
		var reqResp *actionRequestResponse
		reqResp, s.actionReqResps = s.actionReqResps[0], s.actionReqResps[1:]
		if reqResp.wantReq == nil {
			return fmt.Errorf("missing request")
		}
		if reqResp.wantResp == nil {
			return fmt.Errorf("missing response")
		}

		if diff := cmp.Diff(reqResp.wantReq, req, protocmp.Transform()); diff != "" {
			return fmt.Errorf("unexpected request (-want +got):\n%s", diff)
		}
		srv.Send(reqResp.wantResp)
		if reqResp.end {
			return nil
		}
	}
}

// Start a test session, it includes the required session init request.
func mustStartTestSession(t *testing.T, srv grpcpb.IconApiServer) *Session {
	c := startTestServer(t, srv)
	s, err := c.StartSession(context.Background(), []string{"my_part"}, nil)
	if err != nil {
		t.Fatalf("init session failed: %s", err)
	}
	return s
}

func TestSession_ClearAllActionsAndReactions(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_ClearAllActionsReactions{
					ClearAllActionsReactions: &servicepb.OpenSessionRequest_ClearAllActions{},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
			end:      true,
		},
	}}
	s := mustStartTestSession(t, srv)
	err := s.ClearAllActionsAndReactions()
	if err != nil {
		t.Errorf("ClearAllActionsAndReactions failed: %s", err)
	}
}

func TestSession_StartSessionPassesContext(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
				LogContext: &cpb.Context{SkillId: 123456},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
	}}
	c := startTestServer(t, srv)
	_, err := c.StartSession(context.Background(), []string{"my_part"}, &cpb.Context{SkillId: 123456})
	if err != nil {
		t.Fatalf("init session failed: %s", err)
	}
}

func TestSession_End(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_ClearAllActionsReactions{
					ClearAllActionsReactions: &servicepb.OpenSessionRequest_ClearAllActions{},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
			end:      true,
		},
	}}
	s := mustStartTestSession(t, srv)
	err := s.End()
	if err != nil {
		t.Fatal(err)
	}
	err = s.ClearAllActionsAndReactions()
	if !errors.Is(err, ErrSessionEnded) {
		t.Errorf("ClearAllActionsAndReactions err %v, want %v", err, ErrSessionEnded)
	}
}

func TestSession_AddActionWithReaction(t *testing.T) {
	parts := []string{"my_part"}
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: parts},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
	}}
	c := startTestServer(t, srv)
	s, err := c.StartSession(context.Background(), parts, nil)
	if err != nil {
		t.Fatalf("c.StartSession(context.Background(), %v) err %v, want <nil>", parts, err)
	}
	es := s.MakeEventSignal()
	actionDesc := &ActionDescription{
		Handle: s.MakeActionHandle(),
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.point_to_point_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
			GoalVelocity: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData:  FromPartName("my_part"),
		Reactions: []*Reaction{NewReaction(IsTrue("intrinsic.is_done"), EmitEventSignal(es))},
	}
	apb, _ := anypb.New(actionDesc.Params)
	actionID := int64(actionDesc.Handle.ID())
	srv.actionReqResps = append(srv.actionReqResps, &actionRequestResponse{
		wantReq: &servicepb.OpenSessionRequest{
			ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
				AddActionsAndReactions: &typespb.ActionsAndReactions{
					ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: int64(actionDesc.Handle.ID()),
						ActionTypeName:   actionDesc.ActionType,
						FixedParameters:  apb,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}},
					Reactions: []*typespb.Reaction{{
						ActionAssociation: &typespb.Reaction_ActionAssociation{
							ActionInstanceId: actionID,
						},
						Condition:          IsTrue("intrinsic.is_done"),
						ReactionInstanceId: 1,
					}},
				},
			},
		},
		wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
		end:      true,
	})
	if _, err := s.AddAction(actionDesc); err != nil {
		t.Errorf("AddAction failed: %s", err)
	}
}

func TestSession_AddActionWithSlotPartMap(t *testing.T) {
	parts := []string{"my_part"}
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: parts},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
	}}
	c := startTestServer(t, srv)
	s, err := c.StartSession(context.Background(), parts, nil)
	if err != nil {
		t.Fatalf("c.StartSession(context.Background(), %v) err %v, want <nil>", parts, err)
	}
	actionDesc := &ActionDescription{
		Handle: s.MakeActionHandle(),
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.point_to_point_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
			GoalVelocity: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData:  FromSlotPartMap(map[string]string{"arm": "my_part"}),
		Reactions: []*Reaction{},
	}
	apb, _ := anypb.New(actionDesc.Params)
	srv.actionReqResps = append(srv.actionReqResps, &actionRequestResponse{
		wantReq: &servicepb.OpenSessionRequest{
			ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
				AddActionsAndReactions: &typespb.ActionsAndReactions{
					ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: int64(actionDesc.Handle.ID()),
						ActionTypeName:   actionDesc.ActionType,
						FixedParameters:  apb,
						SlotData: &typespb.ActionInstance_SlotPartMap{
							SlotPartMap: &typespb.SlotPartMap{
								SlotNameToPartName: map[string]string{"arm": "my_part"},
							},
						},
					}},
				},
			},
		},
		wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
		end:      true,
	})
	if _, err := s.AddAction(actionDesc); err != nil {
		t.Errorf("AddAction failed: %s", err)
	}
}

func TestSession_AddActions(t *testing.T) {
	actionDesc := &ActionDescription{
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.point_to_point_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
			GoalVelocity: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData: FromPartName("my_part"),
	}
	apb, _ := anypb.New(actionDesc.Params)
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
					AddActionsAndReactions: &typespb.ActionsAndReactions{ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: 1,
						ActionTypeName:   actionDesc.ActionType,
						FixedParameters:  apb,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
			end:      true,
		},
	}}
	s := mustStartTestSession(t, srv)
	actionDesc.Handle = s.MakeActionHandle()
	if _, err := s.AddActions(actionDesc); err != nil {
		t.Errorf("AddActions failed: %s", err)
	}
}

func TestSession_AddActionFails(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
			end: true,
		},
	}}
	s := mustStartTestSession(t, srv)
	tests := []struct {
		name    string // test name
		ad      *ActionDescription
		wantErr error
	}{{
		name: "Invalid ActionHandle",
		ad: &ActionDescription{
			// Handle intentionally omitted.
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("my_part"),
		},
		wantErr: ErrInvalidActionHandle,
	}, {
		name: "Missing Response",
		ad: &ActionDescription{
			Handle:     s.MakeActionHandle(),
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("my_part"),
			Reactions:  []*Reaction{NewReaction(IsTrue("done"))}, // missing response
		},
		wantErr: ErrMissingResponse,
	}, {
		name: "Bad EventSignal",
		ad: &ActionDescription{
			Handle:     s.MakeActionHandle(),
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("my_part"),
			Reactions: []*Reaction{
				NewReaction(IsTrue("done"), EmitEventSignal(EventSignal{})), // bad EventSignal
			},
		},
		wantErr: ErrInvalidEventSignal,
	}, {
		// This is a valid request, but the `end: true` above in `srv`
		// means that the request returns an EOF.
		name: "Streaming RPC closed",
		ad: &ActionDescription{
			Handle:     s.MakeActionHandle(),
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("my_part"),
		},
		wantErr: io.EOF,
	}}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if _, err := s.AddAction(test.ad); !errors.Is(err, test.wantErr) {
				t.Fatalf("s.AddAction(%v) err is %q, want an error that is (or wraps) %q", test.ad, err, test.wantErr)
			}
		})
	}
}

func TestSession_AddReactions(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
					AddActionsAndReactions: &typespb.ActionsAndReactions{
						Reactions: []*typespb.Reaction{{
							Condition:          IsTrue("intrinsic.is_done"),
							ReactionInstanceId: 1,
						}, {
							Condition:          IsFalse("intrinsic.is_done"),
							ReactionInstanceId: 2,
						}},
					},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_RemoveActionAndReactionIds{
					RemoveActionAndReactionIds: &typespb.ActionAndReactionIds{ReactionIds: []int64{1}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
			end:      true,
		},
	}}
	s := mustStartTestSession(t, srv)
	es := s.MakeEventSignal()

	err := s.AddFreestandingReactions(NewReaction(IsTrue("intrinsic.is_done"), EmitEventSignal(es)),
		NewReaction(IsFalse("intrinsic.is_done"), EmitEventSignal(es)))
	if err != nil {
		t.Fatalf("AddReactions failed: %s", err)
	}
}

func TestSession_RemoveActions(t *testing.T) {
	actionDesc := &ActionDescription{
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.point_to_point_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
			GoalVelocity: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData: FromPartName("my_part"),
	}
	apb, _ := anypb.New(actionDesc.Params)
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
					AddActionsAndReactions: &typespb.ActionsAndReactions{ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: 1,
						ActionTypeName:   actionDesc.ActionType,
						FixedParameters:  apb,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_RemoveActionAndReactionIds{
					RemoveActionAndReactionIds: &typespb.ActionAndReactionIds{ActionInstanceIds: []int64{1}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
			end:      true,
		},
	}}
	s := mustStartTestSession(t, srv)
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.RemoveActions(ais[0]); err != nil {
		t.Errorf("RemoveActions failed: %s", err)
	}
}

func TestSession_RemoveActionsZeroHandleFails(t *testing.T) {
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
			end: true,
		},
	}}
	s := mustStartTestSession(t, srv)
	if err := s.RemoveActions(ActionHandle{}); err != ErrInvalidActionHandle {
		t.Errorf("err = %v, want %q", err, ErrInvalidActionHandle)
	}
}

func TestSession_StartAction(t *testing.T) {
	actionDesc := &ActionDescription{
		// TODO (b/192583873) use a constant here once implemented
		ActionType: "intrinsic.point_to_point_move",
		Params: &p2pmpb.PointToPointMoveFixedParams{
			GoalPosition: &jointpb.JointVec{
				Joints: []float64{0},
			},
			GoalVelocity: &jointpb.JointVec{
				Joints: []float64{0},
			},
		},
		SlotData: FromPartName("my_part"),
	}
	apb, _ := anypb.New(actionDesc.Params)
	srv := &stubSessionServer{actionReqResps: []*actionRequestResponse{
		{
			wantReq: &servicepb.OpenSessionRequest{
				InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
					AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
					AddActionsAndReactions: &typespb.ActionsAndReactions{ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: 1,
						ActionTypeName:   actionDesc.ActionType,
						FixedParameters:  apb,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}}},
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
		},
		{
			wantReq: &servicepb.OpenSessionRequest{
				StartActionsRequest: &servicepb.OpenSessionRequest_StartActionsRequestData{
					ActionInstanceIds: []int64{1},
					StopActiveActions: true,
				},
			},
			wantResp: &servicepb.OpenSessionResponse{
				Status:             &statuspb.Status{Code: int32(codes.OK)},
				InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 0},
			},
			end: true,
		},
	}}
	s := mustStartTestSession(t, srv)
	actionDesc.Handle = s.MakeActionHandle()
	ais, err := s.AddActions(actionDesc)
	if err != nil {
		t.Fatalf("AddActions failed: %s", err)
	}
	if err = s.StartAction(ais[0]); err != nil {
		t.Errorf("StartAction failed: %s", err)
	}
}

// stubWatchReactionsServer provides mock implementations of OpenSession
// (a bidirectional streaming RPC) and WatchReactions (a server-side streaming
// RPC).
type stubWatchReactionsServer struct {
	stubSessionServer
	// wantReq is the WatchReactions request to expect from the client.
	wantReq *servicepb.WatchReactionsRequest
	// sendResp is a channel whose contents are read and forwarded along as
	// WatchReactions responses. This gives the test control over what
	// responses are sent.
	sendResp chan *servicepb.WatchReactionsResponse
}

// WatchReactions implements the server-side streaming RPC method by forwarding
// responses read from the s.sendResp channel. This returns (ending the stream)
// when s.sendResp is closed.
func (s *stubWatchReactionsServer) WatchReactions(req *servicepb.WatchReactionsRequest, srv grpcpb.IconApi_WatchReactionsServer) error {
	want, got := s.wantReq, req
	if diff := cmp.Diff(want, got, protocmp.Transform()); diff != "" {
		return fmt.Errorf("unexpected request: diff (-want +got):\n%s", diff)
	}
	for {
		sendResp, more := <-s.sendResp
		if !more {
			// send final response, if non-nil
			if sendResp != nil {
				srv.Send(sendResp)
			}
			return nil
		}
		srv.Send(sendResp)
	}
}

func TestSession_NextEventCanceled(t *testing.T) {
	srv := &stubWatchReactionsServer{
		stubSessionServer: stubSessionServer{
			actionReqResps: []*actionRequestResponse{
				{
					wantReq: &servicepb.OpenSessionRequest{
						InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
							AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
						},
					},
					wantResp: &servicepb.OpenSessionResponse{
						Status:             &statuspb.Status{Code: int32(codes.OK)},
						InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
					},
				},
			},
		},
		wantReq:  &servicepb.WatchReactionsRequest{SessionId: 1},
		sendResp: make(chan *servicepb.WatchReactionsResponse),
	}

	s := mustStartTestSession(t, srv)
	cancel := make(chan struct{})
	time.AfterFunc(time.Millisecond, func() { close(cancel) })
	if _, err := s.NextEvent(cancel); !errors.Is(err, ErrCanceled) {
		t.Errorf("s.NextEvent(cancel) err is %v, want %q", err, ErrCanceled)
	}
}

func TestSession_NextEventReturnsWhenSessionContextCanceled(t *testing.T) {
	srv := &stubWatchReactionsServer{
		stubSessionServer: stubSessionServer{
			actionReqResps: []*actionRequestResponse{
				{
					wantReq: &servicepb.OpenSessionRequest{
						InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
							AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
						},
					},
					wantResp: &servicepb.OpenSessionResponse{
						Status:             &statuspb.Status{Code: int32(codes.OK)},
						InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
					},
				},
			},
		},
		wantReq:  &servicepb.WatchReactionsRequest{SessionId: 1},
		sendResp: make(chan *servicepb.WatchReactionsResponse),
	}

	c := startTestServer(t, srv)
	ctx, cancel := context.WithCancel(context.Background())
	s, err := c.StartSession(ctx, []string{"my_part"}, nil)
	if err != nil {
		t.Fatalf("init session failed: %s", err)
	}
	time.AfterFunc(time.Millisecond, cancel)
	if _, err := s.NextEvent(nil); errors.Is(err, context.Canceled) {
		t.Errorf("s.NextEvent(nil) err is %v, want %q", err, context.Canceled)
	}
}

func TestSession_NextEventIgnoresNonReactionEvents(t *testing.T) {
	srv := &stubWatchReactionsServer{
		stubSessionServer: stubSessionServer{
			actionReqResps: []*actionRequestResponse{
				{
					wantReq: &servicepb.OpenSessionRequest{
						InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
							AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
						},
					},
					wantResp: &servicepb.OpenSessionResponse{
						Status:             &statuspb.Status{Code: int32(codes.OK)},
						InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
					},
				},
			},
		},
		wantReq:  &servicepb.WatchReactionsRequest{SessionId: 1},
		sendResp: make(chan *servicepb.WatchReactionsResponse, 1),
	}
	var previousActionInstanceID int64 = 1
	var currentActionInstanceID int64 = 2
	s := mustStartTestSession(t, srv)
	srv.sendResp <- &servicepb.WatchReactionsResponse{
		ReactionEvent: &typespb.ReactionEvent{
			PreviousActionInstanceId: &previousActionInstanceID,
			CurrentActionInstanceId:  &currentActionInstanceID,
			// ReactionId omitted
		},
	}
	cancel := make(chan struct{})
	time.AfterFunc(time.Millisecond, func() { close(cancel) })
	if _, err := s.NextEvent(cancel); !errors.Is(err, ErrCanceled) {
		t.Errorf("s.NextEvent(cancel) err is %v, want %q", err, ErrCanceled)
	}
}

func TestSession_WatchReactionsCanceledWhileWaitingForNextEventToBeCalled(t *testing.T) {
	srv := &stubWatchReactionsServer{
		stubSessionServer: stubSessionServer{
			actionReqResps: []*actionRequestResponse{
				{
					wantReq: &servicepb.OpenSessionRequest{
						InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
							AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
						},
					},
					wantResp: &servicepb.OpenSessionResponse{
						Status:             &statuspb.Status{Code: int32(codes.OK)},
						InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
					},
				},
			},
		},
		wantReq:  &servicepb.WatchReactionsRequest{SessionId: 1},
		sendResp: make(chan *servicepb.WatchReactionsResponse, 1),
	}

	c := startTestServer(t, srv)
	ctx, cancel := context.WithCancel(context.Background())
	s, err := c.StartSession(ctx, []string{"my_part"}, nil)
	if err != nil {
		t.Fatalf("init session failed: %s", err)
	}
	done := s.MakeEventSignal()
	ad := &ActionDescription{
		Handle:     s.MakeActionHandle(),
		ActionType: "noop",
		SlotData:   FromPartName("my_part"),
		Reactions: []*Reaction{{
			Condition: IsTrue("done"),
			Responses: []Response{EmitEventSignal(done)},
		}},
	}
	actionID := int64(ad.Handle.ID())
	srv.actionReqResps = append(srv.actionReqResps, &actionRequestResponse{
		wantReq: &servicepb.OpenSessionRequest{
			ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
				AddActionsAndReactions: &typespb.ActionsAndReactions{
					ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: actionID,
						ActionTypeName:   ad.ActionType,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}},
					Reactions: []*typespb.Reaction{{
						ActionAssociation: &typespb.Reaction_ActionAssociation{
							ActionInstanceId: actionID,
						},
						Condition:          IsTrue("done"),
						ReactionInstanceId: 1,
					}},
				},
			},
		},
		wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
	})
	if _, err := s.AddAction(ad); err != nil {
		t.Fatalf("s.AddAction(%v) err is %v, want <nil>", ad, err)
	}
	srv.sendResp <- &servicepb.WatchReactionsResponse{
		ReactionEvent: &typespb.ReactionEvent{
			PreviousActionInstanceId: &actionID,
			CurrentActionInstanceId:  &actionID,
			ReactionId:               1,
		},
	}

	time.Sleep(time.Second) // Attempt to move past `client.Recv()` in watchReactions.
	cancel()
	time.Sleep(time.Second) // Attempt to ensure that `<-ctx.Done` is selected.
	// Drain the event channel if `events <- &ReactionEvent{...}` was selected.
	for err = nil; err == nil; _, err = s.NextEvent(nil) {
	}
	// The gRPC error contains context.Cancelled.Error() but does not wrap it.
	if !strings.Contains(err.Error(), context.Canceled.Error()) {
		t.Errorf("s.NextEvent(nil) err is %v, want error to contain %q", err, context.Canceled.Error())
	}
}

func TestSession_NextEvent(t *testing.T) {
	srv := &stubWatchReactionsServer{
		stubSessionServer: stubSessionServer{
			actionReqResps: []*actionRequestResponse{
				{
					wantReq: &servicepb.OpenSessionRequest{
						InitialSessionData: &servicepb.OpenSessionRequest_InitialSessionData{
							AllocateParts: &typespb.PartList{Part: []string{"my_part"}},
						},
					},
					wantResp: &servicepb.OpenSessionResponse{
						Status:             &statuspb.Status{Code: int32(codes.OK)},
						InitialSessionData: &servicepb.OpenSessionResponse_InitialSessionData{SessionId: 1},
					},
				},
			},
		},
		wantReq:  &servicepb.WatchReactionsRequest{SessionId: 1},
		sendResp: make(chan *servicepb.WatchReactionsResponse, 1),
	}

	s := mustStartTestSession(t, srv)
	done := s.MakeEventSignal()
	ad := &ActionDescription{
		Handle:     s.MakeActionHandle(),
		ActionType: "noop",
		SlotData:   FromPartName("my_part"),
		Reactions: []*Reaction{{
			Condition: IsTrue("done"),
			Responses: []Response{EmitEventSignal(done)},
		}},
	}
	actionID := int64(ad.Handle.ID())
	srv.actionReqResps = append(srv.actionReqResps, &actionRequestResponse{
		wantReq: &servicepb.OpenSessionRequest{
			ActionRequest: &servicepb.OpenSessionRequest_AddActionsAndReactions{
				AddActionsAndReactions: &typespb.ActionsAndReactions{
					ActionInstances: []*typespb.ActionInstance{{
						ActionInstanceId: actionID,
						ActionTypeName:   ad.ActionType,
						SlotData:         &typespb.ActionInstance_PartName{PartName: "my_part"},
					}},
					Reactions: []*typespb.Reaction{{
						ActionAssociation: &typespb.Reaction_ActionAssociation{
							ActionInstanceId: actionID,
						},
						Condition:          IsTrue("done"),
						ReactionInstanceId: 1,
					}},
				},
			},
		},
		wantResp: &servicepb.OpenSessionResponse{Status: &statuspb.Status{Code: int32(codes.OK)}},
	})
	if _, err := s.AddAction(ad); err != nil {
		t.Fatalf("s.AddAction(%v) err is %v, want <nil>", ad, err)
	}
	time.AfterFunc(time.Millisecond, func() {
		srv.sendResp <- &servicepb.WatchReactionsResponse{
			ReactionEvent: &typespb.ReactionEvent{
				PreviousActionInstanceId: &actionID,
				CurrentActionInstanceId:  &actionID,
				ReactionId:               1,
			},
		}
	})
	event, err := s.NextEvent(nil)
	if err != nil {
		t.Fatalf("s.NextEvent(nil) err is %v, want <nil>", err)
	}
	re, ok := event.(*ReactionEvent)
	if !ok {
		t.Fatalf("event has type %T, event.(*ReactionEvent) type conversion failed", event)
	}
	if re.EventSignal != done {
		t.Errorf("re.EventSignal=%v, want %v", re.EventSignal, done)
	}
}

func TestSession_DefaultActionHandleIsZero(t *testing.T) {
	var h ActionHandle
	if !h.IsZero() {
		t.Errorf("h.IsZero() is false: want true")
	}
}

func TestSession_ActionHandleIDsAreUnique(t *testing.T) {
	n := 1000 // number of ActionHandles to generate
	hs := make(map[ActionID]struct{})
	var s Session
	for i := 0; i < n; i++ {
		id := s.MakeActionHandle().ID()
		if _, ok := hs[id]; ok {
			t.Errorf("hs[%d] already exists; duplicate key %d", id, id)
		}
		hs[id] = struct{}{}
	}
}

func TestSession_ActionHandlesAreUnique(t *testing.T) {
	n := 1000 // number of ActionHandles to generate
	hs := make(map[ActionHandle]struct{})
	var s Session
	for i := 0; i < n; i++ {
		h := s.MakeActionHandle()
		if _, ok := hs[h]; ok {
			t.Errorf("hs[%d] already exists; duplicate key %d", h, h)
		}
		hs[h] = struct{}{}
	}
}

func TestSession_DefaultEventSignalIsEmpty(t *testing.T) {
	var h EventSignal
	if !h.IsEmpty() {
		t.Errorf("h.IsEmpty() is false, want true")
	}
}

func TestSession_EventSignalsAreUnique(t *testing.T) {
	n := 1000 // number of EventSignals to generate
	hs := make(map[EventSignal]struct{})
	var s Session
	for i := 0; i < n; i++ {
		h := s.MakeEventSignal()
		if _, ok := hs[h]; ok {
			t.Errorf("hs[%d] already exists; duplicate key %d", h, h)
		}
		hs[h] = struct{}{}
	}
}
