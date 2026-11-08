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
	"errors"
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"

	typespb "intrinsic/icon/proto/v1/types_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
	wrappb "google.golang.org/protobuf/types/known/wrapperspb"
)

func TestServerConfig_FromProtoFails(t *testing.T) {
	proto := []*typespb.PartConfig{{
		Name: "arm",
	}, {
		Name: "arm",
	}}
	if _, err := ServerConfigFromProto(proto); !errors.Is(err, ErrInternal) {
		t.Errorf("ServerConfigFromProto(%v) err is %v, want ErrInternal", proto, err)
	}
}

func TestServerConfig_PartConfigs(t *testing.T) {
	config := &wrappb.FloatValue{Value: 1.0}
	any, _ := anypb.New(config)
	proto := []*typespb.PartConfig{{
		Name:   "arm",
		Config: any,
	}}
	sc, err := ServerConfigFromProto(proto)
	if err != nil {
		t.Fatalf("ServerConfigFromProto(%v) err is %v, want <nil>", proto, err)
	}
	got, want := sc.PartConfigs(), map[string]*typespb.PartConfig{"arm": proto[0]}
	if diff := cmp.Diff(want, got, protocmp.Transform()); diff != "" {
		t.Errorf("sc.PartConfigs() returned diff (-want +got):\n%s", diff)
	}
}

func TestServerConfig_PartConfigNotFound(t *testing.T) {
	proto := []*typespb.PartConfig{{
		Name: "arm",
	}}
	sc, err := ServerConfigFromProto(proto)
	if err != nil {
		t.Fatalf("ServerConfigFromProto(%v) err is %v, want <nil>", proto, err)
	}
	part := "gripper"
	if _, err := sc.PartConfig(part); !errors.Is(err, ErrNotFound) {
		t.Fatalf("sc.PartConfig(%q) err is %v, want ErrNotFound", part, err)
	}
}

func TestServerConfig_PartConfig(t *testing.T) {
	config := &wrappb.FloatValue{Value: 1.0}
	any, _ := anypb.New(config)
	part := "arm"
	proto := []*typespb.PartConfig{{
		Name:   part,
		Config: any,
	}}
	sc, err := ServerConfigFromProto(proto)
	if err != nil {
		t.Fatalf("ServerConfigFromProto(%v) err is %v, want <nil>", proto, err)
	}
	got, err := sc.PartConfig(part)
	if err != nil {
		t.Fatalf("sc.PartConfig(%q) err is %v, want <nil>", part, err)
	}
	want := proto[0]
	if diff := cmp.Diff(want, got, protocmp.Transform()); diff != "" {
		t.Errorf("sc.PartConfig(%q) returned diff (-want +got):\n%s", part, diff)
	}
}

func TestServerConfig_PartConfigUnpackedNotFound(t *testing.T) {
	proto := []*typespb.PartConfig{{
		Name: "arm",
	}}
	sc, err := ServerConfigFromProto(proto)
	if err != nil {
		t.Fatalf("ServerConfigFromProto(%v) err is %v, want <nil>", proto, err)
	}
	part := "gripper"
	if err := sc.PartConfigUnpacked(part, nil); !errors.Is(err, ErrNotFound) {
		t.Fatalf("sc.PartConfigUnpacked(%q) err is %v, want ErrNotFound", part, err)
	}
}

func TestServerConfig_PartConfigUnpacked(t *testing.T) {
	config := &wrappb.FloatValue{Value: 1.0}
	any, _ := anypb.New(config)
	part := "arm"
	proto := []*typespb.PartConfig{{
		Name:   part,
		Config: any,
	}}
	sc, err := ServerConfigFromProto(proto)
	if err != nil {
		t.Fatalf("ServerConfigFromProto(%v) err is %v, want <nil>", proto, err)
	}
	var got wrappb.FloatValue
	if err := sc.PartConfigUnpacked(part, &got); err != nil {
		t.Fatalf("sc.PartConfigUnpacked(%q, &got) err is %v, want <nil>", part, err)
	}
	want := config
	if diff := cmp.Diff(want, &got, protocmp.Transform()); diff != "" {
		t.Errorf("sc.PartConfig(%q) returned diff (-want +got):\n%s", part, diff)
	}
}
