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

// Package main implements a transparent, byte-level gRPC proxy for simulated cameras.
// In simulated environments, it intercepts camera gRPC calls (such as CameraServer and CameraService)
// and forwards them to the centralized simulation camera mock service ("camera-sim").
// This decouples the platform deployment layer from specific perception services, providing
// modular simulation capability.
package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"net"
	"slices"
	"strings"
	"sync"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/encoding"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	sspb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	ccpb "intrinsic/perception/proto/v1/camera_config_go_proto"
	rcpb "intrinsic/resources/proto/runtime_context_go_proto"
	"intrinsic/util/proto/protoio"
)

const (
	runtimeContextPath          = "/etc/intrinsic/runtime_config.pb"
	defaultBackendAddrNamespace = "app-intrinsic-app-chart"
	maxMsgSize                  = math.MaxInt64

	cameraConfigService    = "intrinsic_proto.perception.v1.CameraConfigService"
	cameraService          = "intrinsic_proto.perception.v1.CameraService"
	dynamicReconfigService = "intrinsic_proto.services.v1.DynamicReconfiguration"
	serviceState           = "intrinsic_proto.services.v1.ServiceState"
	applyConfigMethod      = "ApplyConfiguration"
	getCameraConfigMethod  = "GetCameraConfig"
)

var allowedServices = []string{
	cameraConfigService,
	cameraService,
	dynamicReconfigService,
	serviceState,
}

type proxyState struct {
	mu           sync.RWMutex
	cameraConfig *ccpb.CameraConfig
}

func (s *proxyState) setCameraConfig(cfg *ccpb.CameraConfig) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if cfg == nil {
		s.cameraConfig = nil
		return
	}
	s.cameraConfig = proto.Clone(cfg).(*ccpb.CameraConfig)
}

func (s *proxyState) getCameraConfig() *ccpb.CameraConfig {
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.cameraConfig == nil {
		return nil
	}
	return proto.Clone(s.cameraConfig).(*ccpb.CameraConfig)
}

// --- 1. Define a Raw Codec for transparent byte-level proxying ---

const proxyCodecName = "proxy-raw"

// Register the codec so gRPC knows it exists
func init() {
	encoding.RegisterCodec(proxyCodec{})
}

// rawFrame acts as our generic message container
type rawFrame struct {
	payload []byte
}

type proxyCodec struct{}

func (proxyCodec) Marshal(v interface{}) ([]byte, error) {
	out, ok := v.(*rawFrame)
	if !ok {
		return nil, fmt.Errorf("expected *rawFrame, got %T", v)
	}
	return out.payload, nil
}

func (proxyCodec) Unmarshal(data []byte, v interface{}) error {
	dst, ok := v.(*rawFrame)
	if !ok {
		return fmt.Errorf("expected *rawFrame, got %T", v)
	}
	// Copy the data to ensure the underlying buffer isn't overwritten
	// before we finish forwarding it.
	dst.payload = make([]byte, len(data))
	copy(dst.payload, data)
	return nil
}

func (proxyCodec) Name() string {
	return proxyCodecName
}

// getBackendAddr parses the namespace from SimulationServerAddress and constructs the camera-sim backend address.
func getBackendAddr(rc *rcpb.RuntimeContext) string {
	simAddr := rc.GetSimulationServerAddress()
	if simAddr == "" {
		return fmt.Sprintf("camera-sim.%s.svc.cluster.local:8080", defaultBackendAddrNamespace)
	}

	host := simAddr
	if h, _, err := net.SplitHostPort(simAddr); err == nil {
		host = h
	}

	parts := strings.Split(host, ".")
	if len(parts) < 2 || parts[1] == "" {
		return fmt.Sprintf("camera-sim.%s.svc.cluster.local:8080", defaultBackendAddrNamespace)
	}

	return fmt.Sprintf("camera-sim.%s.svc.cluster.local:8080", parts[1])
}

// --- 2. Main Execution ---

func main() {
	rc := new(rcpb.RuntimeContext)
	if err := protoio.ReadBinaryProto(runtimeContextPath, rc); err != nil {
		log.Fatalf("Failed to read runtime context: %v", err)
	}

	state := &proxyState{}
	if rc.GetConfig() != nil {
		cfg := &ccpb.CameraConfig{}
		if err := rc.GetConfig().UnmarshalTo(cfg); err == nil {
			state.setCameraConfig(cfg)
		} else {
			log.Printf("Warning: failed to unmarshal initial CameraConfig from runtime context: %v", err)
		}
	}

	backendAddr := getBackendAddr(rc)

	// Force the backend client to use our raw codec
	conn, err := grpc.NewClient(
		backendAddr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithDefaultCallOptions(
			grpc.ForceCodec(proxyCodec{}),
			grpc.MaxCallRecvMsgSize(maxMsgSize),
			grpc.MaxCallSendMsgSize(maxMsgSize),
		),
	)
	if err != nil {
		log.Fatalf("Failed to connect to backend: %v", err)
	}
	defer conn.Close()

	// Force the proxy server to use our raw codec
	s := grpc.NewServer(
		grpc.ForceServerCodec(proxyCodec{}),
		grpc.UnknownServiceHandler(proxyHandler(conn, allowedServices, state)),
		grpc.MaxRecvMsgSize(maxMsgSize),
		grpc.MaxSendMsgSize(maxMsgSize),
	)

	proxyAddr := fmt.Sprintf("0.0.0.0:%d", rc.GetPort())
	lis, err := net.Listen("tcp", proxyAddr)
	if err != nil {
		log.Fatalf("Failed to listen on %s: %v", proxyAddr, err)
	}

	log.Printf("gRPC Proxy listening on %s, forwarding to %s", proxyAddr, backendAddr)
	if err := s.Serve(lis); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}

// proxyHandler returns a StreamHandler that acts as a transparent pipe
func proxyHandler(backendConn *grpc.ClientConn, allowedServices []string, state *proxyState) grpc.StreamHandler {
	return func(srv any, serverStream grpc.ServerStream) error {
		// A. Extract and Verify the requested method
		fullMethodName, ok := grpc.Method(serverStream.Context())
		if !ok {
			return status.Error(codes.Internal, "Low-level gRPC method name missing")
		}

		parts := strings.Split(fullMethodName, "/")
		if len(parts) < 3 || !slices.Contains(allowedServices, parts[1]) {
			log.Printf("Refusing to proxy method: %s", fullMethodName)
			return status.Errorf(codes.Unimplemented, "Proxy restricted to services: %v", allowedServices)
		}
		serviceName, methodName := parts[1], parts[2]

		log.Printf("Attempting to proxy method: %s", fullMethodName)

		// B. Metadata Propagation (Incoming -> Outgoing)
		md, ok := metadata.FromIncomingContext(serverStream.Context())
		if !ok {
			md = metadata.MD{}
		}

		clientCtx, clientCancel := context.WithCancel(metadata.NewOutgoingContext(serverStream.Context(), md.Copy()))
		defer clientCancel()

		// C. Pre-read unary request frame for fallback-capable methods before opening
		// the backend stream. This guarantees that the request payload is captured
		// before any backend communication or fallback handling takes place and avoids
		// concurrent serverStream.RecvMsg calls during fallback execution.
		var initialReqFrame *rawFrame
		if isUnaryFallbackMethod(serviceName, methodName) {
			initialReqFrame = &rawFrame{}
			if err := serverStream.RecvMsg(initialReqFrame); err != nil {
				return err
			}
		}

		// D. Initialize Backend Stream
		desc := &grpc.StreamDesc{
			ServerStreams: true,
			ClientStreams: true,
		}
		backendStream, err := backendConn.NewStream(clientCtx, desc, fullMethodName)
		if err != nil {
			return err
		}

		// E. Bidirectional Data Pipe
		errChan := make(chan error, 2)

		// Goroutine: Client -> Backend
		go func() {
			// If we pre-read the unary request frame, forward it to backend,
			// close the sending direction, and exit without touching serverStream.
			if initialReqFrame != nil {
				if err := backendStream.SendMsg(initialReqFrame); err != nil {
					if errors.Is(err, io.EOF) {
						// ClientStream.SendMsg returns io.EOF when the backend aborts the stream;
						// the authoritative gRPC status is retrieved by the Backend -> Client goroutine.
						errChan <- nil
						return
					}
					errChan <- err
					return
				}
				backendStream.CloseSend()
				errChan <- nil
				return
			}

			for {
				f := &rawFrame{}
				err := serverStream.RecvMsg(f)
				if errors.Is(err, io.EOF) {
					// Client is done sending. Close the sending direction of the backend stream.
					backendStream.CloseSend()
					errChan <- nil
					return
				}
				if err != nil {
					errChan <- err
					return
				}
				if err := backendStream.SendMsg(f); err != nil {
					if errors.Is(err, io.EOF) {
						errChan <- nil
						return
					}
					errChan <- err
					return
				}
			}
		}()

		// Goroutine: Backend -> Client
		go func() {
			// Fetch headers HERE, concurrently, to prevent deadlocking the Unary request phase
			header, err := backendStream.Header()
			if err != nil {
				errChan <- err
				return
			}
			if err := serverStream.SendHeader(header); err != nil {
				errChan <- err
				return
			}

			for {
				f := &rawFrame{}
				err := backendStream.RecvMsg(f)
				if err == io.EOF {
					errChan <- nil
					return
				}
				if err != nil {
					errChan <- err
					return
				}
				if err := serverStream.SendMsg(f); err != nil {
					errChan <- err
					return
				}
			}
		}()

		// E. Wait for completion and finalize Trailers
		for range 2 {
			if err := <-errChan; err != nil {
				// On error, cancel the backend context to tear down the other goroutine safely
				clientCancel()

				st, ok := status.FromError(err)
				if ok && (st.Code() == codes.Unimplemented || (st.Code() == codes.FailedPrecondition && strings.Contains(st.Message(), "install a Gazebo Simulator"))) {
					switch {
					case serviceName == dynamicReconfigService && methodName == applyConfigMethod:
						return handleApplyConfigurationFallback(serverStream, state, initialReqFrame.payload, st.Code())
					case serviceName == cameraConfigService && methodName == getCameraConfigMethod:
						return handleGetCameraConfigFallback(serverStream, state, st.Code())
					case serviceName == serviceState:
						return handleServiceStateFallback(serverStream, methodName, st.Code())
					}
				}
				return err
			}
		}

		// If ApplyConfiguration succeeded on the backend, update state too
		if serviceName == dynamicReconfigService && methodName == applyConfigMethod {
			updateConfigFromApplyRequest(state, initialReqFrame.payload)
		}

		// Propagate trailers (status metadata) back to the client
		serverStream.SetTrailer(backendStream.Trailer())

		return nil
	}
}

// isUnaryFallbackMethod returns true if the RPC is a unary method that supports
// local fallback handling when the backend returns Unimplemented or Gazebo FailedPrecondition.
func isUnaryFallbackMethod(serviceName, methodName string) bool {
	return (serviceName == dynamicReconfigService && methodName == applyConfigMethod) ||
		(serviceName == cameraConfigService && methodName == getCameraConfigMethod) ||
		(serviceName == serviceState && (methodName == "GetState" || methodName == "Enable" || methodName == "Disable"))
}

// updateConfigFromApplyRequest unmarshals an ApplyConfigurationRequest payload and
// updates the in-memory proxyState if a valid CameraConfig is present.
func updateConfigFromApplyRequest(state *proxyState, reqPayload []byte) {
	if state == nil || len(reqPayload) == 0 {
		return
	}
	var req drpb.ApplyConfigurationRequest
	if err := proto.Unmarshal(reqPayload, &req); err != nil || req.GetConfiguration() == nil {
		return
	}
	cfg := &ccpb.CameraConfig{}
	if err := req.GetConfiguration().UnmarshalTo(cfg); err == nil {
		state.setCameraConfig(cfg)
		log.Printf("DynamicReconfiguration: updated in-memory CameraConfig")
	}
}

// handleApplyConfigurationFallback serves a no-op success response when the backend
// does not implement DynamicReconfiguration.ApplyConfiguration, while updating in-memory state.
func handleApplyConfigurationFallback(serverStream grpc.ServerStream, state *proxyState, reqPayload []byte, code codes.Code) error {
	log.Printf("DynamicReconfiguration.ApplyConfiguration proxy failed with %v, falling back to faked no-op", code)
	updateConfigFromApplyRequest(state, reqPayload)
	return serverStream.SendMsg(&rawFrame{payload: []byte{}})
}

// handleGetCameraConfigFallback returns the current in-memory CameraConfig when
// the backend does not implement CameraConfigService.GetCameraConfig.
func handleGetCameraConfigFallback(serverStream grpc.ServerStream, state *proxyState, code codes.Code) error {
	log.Printf("CameraConfigService.GetCameraConfig proxy failed with %v, falling back to faked response", code)
	var cfg *ccpb.CameraConfig
	if state != nil {
		cfg = state.getCameraConfig()
	}
	if cfg == nil {
		return status.Errorf(codes.FailedPrecondition, "No active camera config set")
	}
	payload, err := proto.Marshal(cfg)
	if err != nil {
		log.Printf("Failed to marshal CameraConfig response: %v", err)
		return status.Errorf(codes.Internal, "failed to marshal camera config: %v", err)
	}
	return serverStream.SendMsg(&rawFrame{payload: payload})
}

// handleServiceStateFallback returns a faked response for ServiceState methods when unsupported on backend.
func handleServiceStateFallback(serverStream grpc.ServerStream, method string, code codes.Code) error {
	log.Printf("ServiceState proxy failed with %v, falling back to faked %s.%s", code, serviceState, method)

	var resp proto.Message
	switch method {
	case "GetState":
		resp = &sspb.SelfState{
			StateCode: sspb.SelfState_STATE_CODE_ENABLED,
		}
	case "Enable":
		resp = &sspb.EnableResponse{}
	case "Disable":
		resp = &sspb.DisableResponse{}
	default:
		return status.Errorf(codes.Unimplemented, "unimplemented faked method: %s", method)
	}

	payload, err := proto.Marshal(resp)
	if err != nil {
		log.Printf("Failed to marshal faked %s response: %v", method, err)
		return status.Errorf(codes.Internal, "failed to marshal faked response: %v", err)
	}
	return serverStream.SendMsg(&rawFrame{payload: payload})
}
