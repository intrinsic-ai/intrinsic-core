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
	"testing"
	"time"

	"intrinsic/testing/grpctest"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
	statusgrpc "google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"

	partstatuspb "intrinsic/icon/proto/part_status_go_proto"
	safetystatuspb "intrinsic/icon/proto/safety_status_go_proto"
	streampb "intrinsic/icon/proto/streaming_output_go_proto"
	grpcpb "intrinsic/icon/proto/v1/service_go_proto"
	pb "intrinsic/icon/proto/v1/service_go_proto"
	typespb "intrinsic/icon/proto/v1/types_go_proto"

	statuspb "google.golang.org/genproto/googleapis/rpc/status"
	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

// startTestServer starts a test server and returns an ICON client that is
// connected to it. The client connection will be closed when the test ends.
func startTestServer(t *testing.T, s grpcpb.IconApiServer) Client {
	t.Helper()
	return startTestServerWithInstanceName(t, "", s)
}

func startTestServerWithInstanceName(t *testing.T, serverInstance string, s grpcpb.IconApiServer) Client {
	t.Helper()
	srv := grpc.NewServer()
	grpcpb.RegisterIconApiServer(srv, s)
	addr := grpctest.StartServerT(t, srv)
	// suppress go/nogo-check#disallowedfunction google3/third_party/golang/grpc/grpc.WithInsecure
	c, err := InitClient(addr, WithServerInstanceHeaderValue(serverInstance), WithDialOptions(grpc.WithTransportCredentials(insecure.NewCredentials())))
	if err != nil {
		t.Fatalf("error initializing client: %v", err)
	}
	t.Cleanup(func() {
		if err := c.Close(); err != nil {
			t.Errorf("error closing client: %v", err)
		}
	})
	return c
}

func TestIconClient_InitClose(t *testing.T) {
	s := &grpcpb.UnimplementedIconApiServer{}
	startTestServer(t, s)
}

func TestIconClient_PropagatesErrors(t *testing.T) {
	s := &grpcpb.UnimplementedIconApiServer{}
	c := startTestServer(t, s)
	ctx := context.Background()
	t.Run("ActionSignatureByName", func(t *testing.T) {
		if _, err := c.ActionSignatureByName(ctx, "ignored"); err == nil {
			t.Error("expected error from ActionSignatureByName(), got nil")
		}
	})
	t.Run("Config", func(t *testing.T) {
		if _, _, err := c.Config(ctx); err == nil {
			t.Error("expected error from Config(), got nil")
		}
	})
	t.Run("Status", func(t *testing.T) {
		if _, err := c.Status(ctx); err == nil {
			t.Error("expected error from Status(), got nil")
		}
	})
	t.Run("IsActionCompatible", func(t *testing.T) {
		if _, err := c.IsActionCompatible(ctx, "ignored", "ignored"); err == nil {
			t.Error("expected error from IsActionCompatible(), got nil")
		}
	})
	t.Run("ActionSignatures", func(t *testing.T) {
		if _, err := c.ActionSignatures(ctx); err == nil {
			t.Error("expected error from ActionSignatures(), got nil")
		}
	})
	t.Run("CompatibleParts", func(t *testing.T) {
		if _, err := c.CompatibleParts(ctx, "ignored"); err == nil {
			t.Error("expected error from CompatibleParts(), got nil")
		}
	})
	t.Run("Parts", func(t *testing.T) {
		if _, err := c.Parts(ctx); err == nil {
			t.Error("expected error from Parts(), got nil")
		}
	})
	t.Run("StartSession", func(t *testing.T) {
		if _, err := c.StartSession(ctx, nil, nil); err == nil {
			t.Error("expected error from StartSession(), got nil")
		}
	})
	t.Run("Enable", func(t *testing.T) {
		if err := c.Enable(ctx); err == nil {
			t.Error("expected error from Enable(), got nil")
		}
	})
	t.Run("Disable", func(t *testing.T) {
		if err := c.Disable(ctx, AllHardware); err == nil {
			t.Error("expected error from Disable(), got nil")
		}
	})
	t.Run("ClearFaults", func(t *testing.T) {
		if err := c.ClearFaults(ctx); err == nil {
			t.Error("expected error from ClearFaults(), got nil")
		}
	})
	t.Run("OperationalStatus", func(t *testing.T) {
		if _, err := c.OperationalStatus(ctx); err == nil {
			t.Error("expected error from OperationalStatus(), got nil")
		}
	})
	t.Run("GetCellControlHardwareStatus", func(t *testing.T) {
		if _, err := c.CellControlHardwareStatus(ctx); err == nil {
			t.Error("expected error from CellControlHardwareStatus(), got nil")
		}
	})
	t.Run("GetSpeedOverride", func(t *testing.T) {
		if _, err := c.GetSpeedOverride(ctx); err == nil {
			t.Error("expected error from GetSpeedOverride(), got nil")
		}
	})
	t.Run("SetSpeedOverride", func(t *testing.T) {
		if err := c.SetSpeedOverride(ctx, 0.2); err == nil {
			t.Error("expected error from SetSpeedOverride(), got nil")
		}
	})
	t.Run("GetPartProperties", func(t *testing.T) {
		if _, err := c.GetPartProperties(ctx); err == nil {
			t.Error("expected error from GetPartProperties(), got nil")
		}
	})
	t.Run("SetPartProperties", func(t *testing.T) {
		if err := c.SetPartProperties(ctx, map[string]map[string]*pb.PartPropertyValue{}); err == nil {
			t.Error("expected error from SetPartProperties(), got nil")
		}
	})
	t.Run("RestartServer", func(t *testing.T) {
		if err := c.RestartServer(ctx); err == nil {
			t.Error("expected error from RestartServer(), got nil")
		}
	})
}

type stubActionSignatureByNameServer struct {
	grpcpb.UnimplementedIconApiServer
	wantReq *pb.GetActionSignatureByNameRequest
	resp    *pb.GetActionSignatureByNameResponse
}

func (s *stubActionSignatureByNameServer) GetActionSignatureByName(ctx context.Context, req *pb.GetActionSignatureByNameRequest) (*pb.GetActionSignatureByNameResponse, error) {
	if diff := cmp.Diff(req, s.wantReq, protocmp.Transform()); diff != "" {
		return nil, fmt.Errorf("unexpected request (-want +got):\n%s", diff)
	}
	return s.resp, nil
}

func TestIconClient_ActionSignatureByName(t *testing.T) {
	s := &stubActionSignatureByNameServer{
		wantReq: &pb.GetActionSignatureByNameRequest{Name: "my_action"},
		resp:    &pb.GetActionSignatureByNameResponse{ActionSignature: &typespb.ActionSignature{ActionTypeName: "my_action"}},
	}
	c := startTestServer(t, s)
	resp, err := c.ActionSignatureByName(context.Background(), "my_action")
	if err != nil {
		t.Fatalf("ActionSignatureByName failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.ActionSignature, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubConfigServer struct {
	grpcpb.UnimplementedIconApiServer
	resp *pb.GetConfigResponse
}

func (s *stubConfigServer) GetConfig(ctx context.Context, req *pb.GetConfigRequest) (*pb.GetConfigResponse, error) {
	return s.resp, nil
}

func TestIconClient_Config(t *testing.T) {
	s := &stubConfigServer{
		resp: &pb.GetConfigResponse{
			PartConfigs:  []*typespb.PartConfig{{Name: "my_part"}},
			ServerConfig: &typespb.ServerConfig{Name: "my_server"},
		},
	}
	c := startTestServer(t, s)
	gotPartConfigs, gotServerConfig, err := c.Config(context.Background())
	if err != nil {
		t.Fatalf("Config failed: %s", err)
	}
	if diff := cmp.Diff(gotPartConfigs, s.resp.PartConfigs, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
	if diff := cmp.Diff(gotServerConfig, s.resp.ServerConfig, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubStatusServer struct {
	grpcpb.UnimplementedIconApiServer
	resp *pb.GetStatusResponse
}

func (s *stubStatusServer) GetStatus(ctx context.Context, req *pb.GetStatusRequest) (*pb.GetStatusResponse, error) {
	return s.resp, nil
}

func TestIconClient_Status(t *testing.T) {
	s := &stubStatusServer{
		resp: &pb.GetStatusResponse{
			PartStatus: map[string]*partstatuspb.PartStatus{
				"my_part": {TimestampNs: 1},
			},
			SafetyStatus: &safetystatuspb.SafetyStatus{
				ModeOfSafeOperation: safetystatuspb.ModeOfSafeOperation_MODE_OF_SAFE_OPERATION_TEACHING_2,
				EstopButtonStatus:   safetystatuspb.ButtonStatus_BUTTON_STATUS_ENGAGED,
				EnableButtonStatus:  safetystatuspb.ButtonStatus_BUTTON_STATUS_DISENGAGED,
				RequestedBehavior:   safetystatuspb.RequestedBehavior_REQUESTED_BEHAVIOR_SAFE_STOP_1_TIME_MONITORED,
			},
		},
	}
	c := startTestServer(t, s)
	resp, err := c.Status(context.Background())
	if err != nil {
		t.Fatalf("Status failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubIsActionCompatibleServer struct {
	grpcpb.UnimplementedIconApiServer
	wantReq *pb.IsActionCompatibleRequest
	resp    *pb.IsActionCompatibleResponse
}

func (s *stubIsActionCompatibleServer) IsActionCompatible(ctx context.Context, req *pb.IsActionCompatibleRequest) (*pb.IsActionCompatibleResponse, error) {
	if diff := cmp.Diff(req, s.wantReq, protocmp.Transform()); diff != "" {
		return nil, fmt.Errorf("unexpected request (-want +got):\n%s", diff)
	}
	return s.resp, nil
}

func TestIconClient_IsActionCompatible(t *testing.T) {
	s := &stubIsActionCompatibleServer{
		wantReq: &pb.IsActionCompatibleRequest{ActionTypeName: "my_action", SlotData: &pb.IsActionCompatibleRequest_PartName{PartName: "my_part"}},
		resp:    &pb.IsActionCompatibleResponse{IsCompatible: true},
	}
	c := startTestServer(t, s)
	compatible, err := c.IsActionCompatible(context.Background(), "my_action", "my_part")
	if err != nil {
		t.Fatalf("IsActionCompatible failed: %s", err)
	}
	if compatible != s.resp.IsCompatible {
		t.Errorf("compatible = %t, want %t", compatible, s.resp.IsCompatible)
	}
}

type stubActionSignaturesServer struct {
	grpcpb.UnimplementedIconApiServer
	resp *pb.ListActionSignaturesResponse
}

func (s *stubActionSignaturesServer) ListActionSignatures(ctx context.Context, req *pb.ListActionSignaturesRequest) (*pb.ListActionSignaturesResponse, error) {
	return s.resp, nil
}

func TestIconClient_ActionSignatures(t *testing.T) {
	s := &stubActionSignaturesServer{
		resp: &pb.ListActionSignaturesResponse{
			ActionSignatures: []*typespb.ActionSignature{{ActionTypeName: "my_action"}},
		},
	}
	c := startTestServer(t, s)
	resp, err := c.ActionSignatures(context.Background())
	if err != nil {
		t.Fatalf("ActionSignatures failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.ActionSignatures, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubCompatiblePartsServer struct {
	grpcpb.UnimplementedIconApiServer
	wantReq *pb.ListCompatiblePartsRequest
	resp    *pb.ListCompatiblePartsResponse
}

func (s *stubCompatiblePartsServer) ListCompatibleParts(ctx context.Context, req *pb.ListCompatiblePartsRequest) (*pb.ListCompatiblePartsResponse, error) {
	if diff := cmp.Diff(req, s.wantReq, protocmp.Transform()); diff != "" {
		return nil, fmt.Errorf("unexpected request (-want +got):\n%s", diff)
	}
	return s.resp, nil
}

func TestIconClient_CompatibleParts(t *testing.T) {
	s := &stubCompatiblePartsServer{
		wantReq: &pb.ListCompatiblePartsRequest{ActionTypeNames: []string{"my_action"}},
		resp:    &pb.ListCompatiblePartsResponse{Parts: []string{"my_part"}},
	}
	c := startTestServer(t, s)
	resp, err := c.CompatibleParts(context.Background(), "my_action")
	if err != nil {
		t.Fatalf("CompatibleParts failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.Parts, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubPartsServer struct {
	grpcpb.UnimplementedIconApiServer
	resp *pb.ListPartsResponse
}

func (s *stubPartsServer) ListParts(ctx context.Context, req *pb.ListPartsRequest) (*pb.ListPartsResponse, error) {
	return s.resp, nil
}

func TestIconClient_Parts(t *testing.T) {
	s := &stubPartsServer{
		resp: &pb.ListPartsResponse{Parts: []string{"my_part"}},
	}
	c := startTestServer(t, s)
	resp, err := c.Parts(context.Background())
	if err != nil {
		t.Fatalf("Parts failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.Parts, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubEnableServer struct {
	grpcpb.UnimplementedIconApiServer
	count int
}

func (s *stubEnableServer) Enable(ctx context.Context, req *pb.EnableRequest) (*pb.EnableResponse, error) {
	s.count++
	return &pb.EnableResponse{}, nil
}

func TestIconClient_Enable(t *testing.T) {
	s := &stubEnableServer{}
	c := startTestServer(t, s)
	if err := c.Enable(context.Background()); err != nil {
		t.Fatalf("Enable failed: %s", err)
	}
	if s.count != 1 {
		t.Errorf("Enable called %d times, want 1", s.count)
	}
}

type stubDisableServer struct {
	grpcpb.UnimplementedIconApiServer
	count   int
	request *pb.DisableRequest
}

func (s *stubDisableServer) Disable(ctx context.Context, req *pb.DisableRequest) (*pb.DisableResponse, error) {
	s.count++
	s.request = req
	return &pb.DisableResponse{}, nil
}

func TestIconClient_Disable(t *testing.T) {
	s := &stubDisableServer{}
	c := startTestServer(t, s)
	if err := c.Disable(context.Background(), AllHardware); err != nil {
		t.Fatalf("Disable failed: %s", err)
	}
	if s.count != 1 {
		t.Errorf("Disable called %d times, want 1", s.count)
	}
	if s.request.GetGroup() != pb.DisableRequest_ALL_HARDWARE {
		t.Errorf("Disable called with %v, want %v", s.request.GetGroup(), pb.DisableRequest_ALL_HARDWARE)
	}
}

func TestIconClient_DisableOperationalHardwareOnly(t *testing.T) {
	s := &stubDisableServer{}
	c := startTestServer(t, s)
	if err := c.Disable(context.Background(), OperationalHardwareOnly); err != nil {
		t.Fatalf("Disable failed: %s", err)
	}
	if s.count != 1 {
		t.Errorf("Disable called %d times, want 1", s.count)
	}
	if s.request.GetGroup() != pb.DisableRequest_OPERATIONAL_HARDWARE_ONLY {
		t.Errorf("Disable called with %v, want %v", s.request.GetGroup(), pb.DisableRequest_OPERATIONAL_HARDWARE_ONLY)
	}
}

type stubClearFaultsServer struct {
	grpcpb.UnimplementedIconApiServer
	count int
}

func (s *stubClearFaultsServer) ClearFaults(ctx context.Context, req *pb.ClearFaultsRequest) (*pb.ClearFaultsResponse, error) {
	s.count++
	return &pb.ClearFaultsResponse{}, nil
}

func TestIconClient_ClearFaults(t *testing.T) {
	s := &stubClearFaultsServer{}
	c := startTestServer(t, s)
	if err := c.ClearFaults(context.Background()); err != nil {
		t.Fatalf("ClearFaults failed: %s", err)
	}
	if s.count != 1 {
		t.Errorf("ClearFaults called %d times, want 1", s.count)
	}
}

type stubOperationalStatusServer struct {
	grpcpb.UnimplementedIconApiServer
	resp *pb.GetOperationalStatusResponse
}

func (s *stubOperationalStatusServer) GetOperationalStatus(ctx context.Context, req *pb.GetOperationalStatusRequest) (*pb.GetOperationalStatusResponse, error) {
	return s.resp, nil
}

func TestIconClient_OperationalStatus(t *testing.T) {
	s := &stubOperationalStatusServer{
		resp: &pb.GetOperationalStatusResponse{OperationalStatus: &typespb.OperationalStatus{State: typespb.OperationalState_ENABLED}},
	}
	c := startTestServer(t, s)
	resp, err := c.OperationalStatus(context.Background())
	if err != nil {
		t.Fatalf("OperationalStatus failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.OperationalStatus, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

func TestIconClient_CellControlHardwareStatus(t *testing.T) {
	s := &stubOperationalStatusServer{
		resp: &pb.GetOperationalStatusResponse{CellControlHardwareStatus: &typespb.OperationalStatus{State: typespb.OperationalState_FAULTED, FaultReason: "my_fault"}},
	}
	c := startTestServer(t, s)
	resp, err := c.CellControlHardwareStatus(context.Background())
	if err != nil {
		t.Fatalf("CellControlHardwareStatus failed: %s", err)
	}
	if diff := cmp.Diff(resp, s.resp.CellControlHardwareStatus, protocmp.Transform()); diff != "" {
		t.Errorf("unexpected response (-want +got):\n%s", diff)
	}
}

type stubSpeedOverrideServer struct {
	grpcpb.UnimplementedIconApiServer
	speedOverride float64
}

func (s *stubSpeedOverrideServer) GetSpeedOverride(ctx context.Context, req *pb.GetSpeedOverrideRequest) (*pb.GetSpeedOverrideResponse, error) {
	return &pb.GetSpeedOverrideResponse{OverrideFactor: s.speedOverride}, nil
}

func (s *stubSpeedOverrideServer) SetSpeedOverride(ctx context.Context, req *pb.SetSpeedOverrideRequest) (*pb.SetSpeedOverrideResponse, error) {
	s.speedOverride = req.GetOverrideFactor()
	return &pb.SetSpeedOverrideResponse{}, nil
}

func TestIconClient_SpeedOverride(t *testing.T) {
	s := &stubSpeedOverrideServer{
		speedOverride: 1.0,
	}
	c := startTestServer(t, s)
	{
		resp, err := c.GetSpeedOverride(context.Background())
		if err != nil {
			t.Fatalf("OperationalStatus failed: %s", err)
		}
		if resp != 1.0 {
			t.Errorf("unexpected response (got %f,  want %f)", resp, 1.0)
		}
	}
	newSpeedOverride := 0.75
	err := c.SetSpeedOverride(context.Background(), newSpeedOverride)
	if err != nil {
		t.Fatalf("OperationalStatus failed: %s", err)
	}
	{
		resp, err := c.GetSpeedOverride(context.Background())
		if err != nil {
			t.Fatalf("OperationalStatus failed: %s", err)
		}
		if resp != newSpeedOverride {
			t.Errorf("unexpected response (got %f,  want %f)", resp, newSpeedOverride)
		}
	}
}

type stubPartPropertiesServer struct {
	grpcpb.UnimplementedIconApiServer
	partProperties *pb.GetPartPropertiesResponse
}

func (s *stubPartPropertiesServer) GetPartProperties(ctx context.Context, req *pb.GetPartPropertiesRequest) (*pb.GetPartPropertiesResponse, error) {
	return s.partProperties, nil
}

func (s *stubPartPropertiesServer) SetPartProperties(ctx context.Context, req *pb.SetPartPropertiesRequest) (*pb.SetPartPropertiesResponse, error) {
	s.partProperties.PartPropertiesByPartName = req.GetPartPropertiesByPartName()
	return &pb.SetPartPropertiesResponse{}, nil
}

func TestIconClient_PartProperties(t *testing.T) {
	s := &stubPartPropertiesServer{
		partProperties: &pb.GetPartPropertiesResponse{},
	}
	c := startTestServer(t, s)
	{
		resp, err := c.GetPartProperties(context.Background())
		if err != nil {
			t.Fatalf("GetPartProperties failed: %s", err)
		}
		if diff := cmp.Diff(s.partProperties, resp, protocmp.Transform()); diff != "" {
			t.Errorf("GetpartProperties returned unexpected value. Proto difference (-want +got):\n%s", diff)
		}
	}

	if err := c.SetPartProperties(context.Background(), map[string]map[string]*pb.PartPropertyValue{
		"part1": {
			"float_value": Float64PartPropertyValue(1.23),
			"bool_value":  BoolPartPropertyValue(true),
		},
	}); err != nil {
		t.Fatalf("SetPartProperties failed: %s", err)
	}

	{
		resp, err := c.GetPartProperties(context.Background())
		if err != nil {
			t.Fatalf("GetPartProperties failed: %s", err)
		}
		if actualFloatPropertyValue := resp.GetPartPropertiesByPartName()["part1"].GetPropertyValuesByName()["float_value"].GetDoubleValue(); actualFloatPropertyValue != 1.23 {
			t.Errorf("unexpected response (got %f, want 1.23)", actualFloatPropertyValue)
		}
		if actualBoolPropertyValue := resp.GetPartPropertiesByPartName()["part1"].GetPropertyValuesByName()["bool_value"].GetBoolValue(); actualBoolPropertyValue != true {
			t.Errorf("unexpected response (got %t, want true)", actualBoolPropertyValue)
		}
	}
}

type stubRestartServer struct {
	grpcpb.UnimplementedIconApiServer
	err   error
	count int
}

func (s *stubRestartServer) RestartServer(ctx context.Context, req *emptypb.Empty) (*emptypb.Empty, error) {
	s.count++
	if s.err != nil {
		return nil, s.err
	}
	return &emptypb.Empty{}, nil
}

func TestIconClient_RestartServer(t *testing.T) {
	s := &stubRestartServer{
		count: 0,
		err:   nil,
	}
	c := startTestServer(t, s)
	if err := c.RestartServer(context.Background()); err != nil {
		t.Fatalf("RestartServer failed: %s", err)
	}
	if s.count != 1 {
		t.Errorf("RestartServer called %d times, want 1", s.count)
	}

	// Test error forwarding
	s.err = errors.New("no, I'm not going to restart, I'm not tired")

	if err := c.RestartServer(context.Background()); statusgrpc.Convert(err).Message() != s.err.Error() {
		t.Errorf("RestartServer did not forward expected error (got %s, want %s)", statusgrpc.Convert(err).Message(), s.err.Error())
	}
}

// metadataCheckServer is a server implementation that simply verifies that the
// incoming request has the "x-resource-instance-name" metadata field set
// correctly.
type metadataCheckServer struct {
	wantServerInstanceValue string
}

func (s *metadataCheckServer) checkMetadata(ctx context.Context) error {
	md, _ := metadata.FromIncomingContext(ctx)
	if s.wantServerInstanceValue == "" && len(md[resourceInstanceHeaderKey]) != 0 {
		return fmt.Errorf("len(md[%q]) = %q, want 0", resourceInstanceHeaderKey, len(md[resourceInstanceHeaderKey]))
	}
	want, got := []string{s.wantServerInstanceValue}, md[resourceInstanceHeaderKey]
	if diff := cmp.Diff(want, got); diff != "" {
		return fmt.Errorf("md[%q]: diff (-want +got):\n%s", resourceInstanceHeaderKey, diff)
	}
	return nil
}

func (s *metadataCheckServer) GetActionSignatureByName(ctx context.Context, req *pb.GetActionSignatureByNameRequest) (*pb.GetActionSignatureByNameResponse, error) {
	return &pb.GetActionSignatureByNameResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetConfig(ctx context.Context, req *pb.GetConfigRequest) (*pb.GetConfigResponse, error) {
	return &pb.GetConfigResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetStatus(ctx context.Context, req *pb.GetStatusRequest) (*pb.GetStatusResponse, error) {
	return &pb.GetStatusResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) IsActionCompatible(ctx context.Context, req *pb.IsActionCompatibleRequest) (*pb.IsActionCompatibleResponse, error) {
	return &pb.IsActionCompatibleResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) ListActionSignatures(ctx context.Context, req *pb.ListActionSignaturesRequest) (*pb.ListActionSignaturesResponse, error) {
	return &pb.ListActionSignaturesResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) ListCompatibleParts(ctx context.Context, req *pb.ListCompatiblePartsRequest) (*pb.ListCompatiblePartsResponse, error) {
	return &pb.ListCompatiblePartsResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) ListParts(ctx context.Context, req *pb.ListPartsRequest) (*pb.ListPartsResponse, error) {
	return &pb.ListPartsResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) Enable(ctx context.Context, req *pb.EnableRequest) (*pb.EnableResponse, error) {
	return &pb.EnableResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) Disable(ctx context.Context, req *pb.DisableRequest) (*pb.DisableResponse, error) {
	return &pb.DisableResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) RestartServer(ctx context.Context, req *emptypb.Empty) (*emptypb.Empty, error) {
	return &emptypb.Empty{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) ClearFaults(ctx context.Context, req *pb.ClearFaultsRequest) (*pb.ClearFaultsResponse, error) {
	return &pb.ClearFaultsResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetOperationalStatus(ctx context.Context, req *pb.GetOperationalStatusRequest) (*pb.GetOperationalStatusResponse, error) {
	return &pb.GetOperationalStatusResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) OpenSession(srv grpcpb.IconApi_OpenSessionServer) error {
	if err := s.checkMetadata(srv.Context()); err != nil {
		return err
	}
	if _, err := srv.Recv(); err != nil {
		return err
	}
	resp := &pb.OpenSessionResponse{
		Status:             &statuspb.Status{Code: 0},
		InitialSessionData: &pb.OpenSessionResponse_InitialSessionData{SessionId: 0},
	}
	return srv.Send(resp)
}

func (s *metadataCheckServer) OpenWriteStream(srv grpcpb.IconApi_OpenWriteStreamServer) error {
	if err := s.checkMetadata(srv.Context()); err != nil {
		return err
	}
	return nil
}

func (s *metadataCheckServer) WatchReactions(req *pb.WatchReactionsRequest, srv grpcpb.IconApi_WatchReactionsServer) error {
	if err := s.checkMetadata(srv.Context()); err != nil {
		return err
	}
	for {
		err := srv.Send(&pb.WatchReactionsResponse{
			ReactionEvent: &typespb.ReactionEvent{
				PreviousActionInstanceId: proto.Int64(1),
				CurrentActionInstanceId:  proto.Int64(2),
			},
		})
		if err != nil {
			return err
		}
	}
}

func (s *metadataCheckServer) GetLatestStreamingOutput(ctx context.Context, req *pb.GetLatestStreamingOutputRequest) (*pb.GetLatestStreamingOutputResponse, error) {
	return &pb.GetLatestStreamingOutputResponse{
		Output: &streampb.StreamingOutput{TimestampNs: 1},
	}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetPlannedTrajectory(r *pb.GetPlannedTrajectoryRequest, srv grpcpb.IconApi_GetPlannedTrajectoryServer) error {
	return fmt.Errorf("GetPlannedTrajectory should not be called")
}

func (s *metadataCheckServer) GetSpeedOverride(ctx context.Context, req *pb.GetSpeedOverrideRequest) (*pb.GetSpeedOverrideResponse, error) {
	return &pb.GetSpeedOverrideResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) SetSpeedOverride(ctx context.Context, req *pb.SetSpeedOverrideRequest) (*pb.SetSpeedOverrideResponse, error) {
	return &pb.SetSpeedOverrideResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetPartProperties(ctx context.Context, req *pb.GetPartPropertiesRequest) (*pb.GetPartPropertiesResponse, error) {
	return &pb.GetPartPropertiesResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) SetPartProperties(ctx context.Context, req *pb.SetPartPropertiesRequest) (*pb.SetPartPropertiesResponse, error) {
	return &pb.SetPartPropertiesResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) SetLoggingMode(ctx context.Context, req *pb.SetLoggingModeRequest) (*pb.SetLoggingModeResponse, error) {
	return &pb.SetLoggingModeResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetLoggingMode(ctx context.Context, req *pb.GetLoggingModeRequest) (*pb.GetLoggingModeResponse, error) {
	return &pb.GetLoggingModeResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) GetPayload(ctx context.Context, req *pb.GetPayloadRequest) (*pb.GetPayloadResponse, error) {
	return &pb.GetPayloadResponse{}, s.checkMetadata(ctx)
}

func (s *metadataCheckServer) SetPayload(ctx context.Context, req *pb.SetPayloadRequest) (*pb.SetPayloadResponse, error) {
	return &pb.SetPayloadResponse{}, s.checkMetadata(ctx)
}

func TestIconClient_MetadataCheck(t *testing.T) {
	s := &metadataCheckServer{
		wantServerInstanceValue: "foo",
	}
	c := startTestServerWithInstanceName(t, "foo", s)
	ctx := context.Background()
	t.Run("ActionSignatureByName", func(t *testing.T) {
		if _, err := c.ActionSignatureByName(ctx, "ignored"); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("Config", func(t *testing.T) {
		if _, _, err := c.Config(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("Status", func(t *testing.T) {
		if _, err := c.Status(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("IsActionCompatible", func(t *testing.T) {
		if _, err := c.IsActionCompatible(ctx, "ignored", "ignored"); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("ActionSignatures", func(t *testing.T) {
		if _, err := c.ActionSignatures(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("CompatibleParts", func(t *testing.T) {
		if _, err := c.CompatibleParts(ctx, "ignored"); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("Parts", func(t *testing.T) {
		if _, err := c.Parts(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("StartSession", func(t *testing.T) {
		if _, err := c.StartSession(ctx, nil, nil); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("Enable", func(t *testing.T) {
		if err := c.Enable(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("Disable", func(t *testing.T) {
		if err := c.Disable(ctx, AllHardware); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("ClearFaults", func(t *testing.T) {
		if err := c.ClearFaults(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("OperationalStatus", func(t *testing.T) {
		if _, err := c.OperationalStatus(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("CellControlHardwareStatus", func(t *testing.T) {
		if _, err := c.CellControlHardwareStatus(ctx); err != nil {
			t.Errorf("CellControlHardwareStatus check err: %v", err)
		}
	})
	t.Run("Read", func(t *testing.T) {
		session, err := c.StartSession(ctx, nil, nil)
		if err != nil {
			t.Fatalf("StartSession failed: %v", err)
		}
		rs, err := session.OpenReadStream(session.MakeActionHandle())
		if err != nil {
			t.Fatalf("OpenReadStream failed: %v", err)
		}
		if _, _, err := rs.Read(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("NextEvent", func(t *testing.T) {
		session, err := c.StartSession(ctx, nil, nil)
		if err != nil {
			t.Fatalf("StartSession failed: %v", err)
		}
		cancel := make(chan struct{})
		time.AfterFunc(time.Second, func() { close(cancel) })
		if _, err := session.NextEvent(cancel); !errors.Is(err, ErrCanceled) {
			t.Errorf("s.NextEvent(cancel) err is %v, want %q", err, ErrCanceled)
		}
	})
	t.Run("GetSpeedOverride", func(t *testing.T) {
		if _, err := c.GetSpeedOverride(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("SetSpeedOverride", func(t *testing.T) {
		if err := c.SetSpeedOverride(ctx, 0.2); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("GetPartProperties", func(t *testing.T) {
		if _, err := c.GetPartProperties(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("SetPartProperties", func(t *testing.T) {
		if err := c.SetPartProperties(ctx, map[string]map[string]*pb.PartPropertyValue{}); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
	t.Run("RestartServer", func(t *testing.T) {
		if err := c.RestartServer(ctx); err != nil {
			t.Errorf("metadata check err: %v", err)
		}
	})
}
