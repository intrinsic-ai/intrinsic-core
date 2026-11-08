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
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"

	p2pmpb "intrinsic/icon/actions/point_to_point_move_go_proto"
	jointpb "intrinsic/icon/proto/joint_space_go_proto"
	typespb "intrinsic/icon/proto/v1/types_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

func TestActionDescriptionProto(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()
	params := &p2pmpb.PointToPointMoveFixedParams{
		GoalPosition: &jointpb.JointVec{Joints: []float64{0}},
		GoalVelocity: &jointpb.JointVec{Joints: []float64{0}},
	}
	anyParams, _ := anypb.New(params)
	tests := []struct {
		name string // test name
		ad   *ActionDescription
		want *typespb.ActionInstance
	}{{
		name: "no params",
		ad: &ActionDescription{
			Handle:     ah,
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("arm"),
			// Params omitted.
		},
		want: &typespb.ActionInstance{
			ActionInstanceId: int64(ah.ID()),
			ActionTypeName:   "intrinsic.stop",
			SlotData:         &typespb.ActionInstance_PartName{PartName: "arm"},
		},
	}, {
		name: "with params",
		ad: &ActionDescription{
			Handle:     ah,
			ActionType: "intrinsic.stop",
			SlotData:   FromPartName("arm"),
			Params:     params,
		},
		want: &typespb.ActionInstance{
			ActionInstanceId: int64(ah.ID()),
			ActionTypeName:   "intrinsic.stop",
			SlotData:         &typespb.ActionInstance_PartName{PartName: "arm"},
			FixedParameters:  anyParams,
		},
	}, {
		name: "with params and SlotPartMap",
		ad: &ActionDescription{
			Handle:     ah,
			ActionType: "intrinsic.stop",
			SlotData:   FromSlotPartMap(map[string]string{"arm_slot": "arm"}),
			Params:     params,
		},
		want: &typespb.ActionInstance{
			ActionInstanceId: int64(ah.ID()),
			ActionTypeName:   "intrinsic.stop",
			SlotData: &typespb.ActionInstance_SlotPartMap{
				SlotPartMap: &typespb.SlotPartMap{
					SlotNameToPartName: map[string]string{"arm_slot": "arm"},
				},
			},
			FixedParameters: anyParams,
		},
	}}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			got, err := test.ad.proto()
			if err != nil {
				t.Errorf("ad=%v, ad.proto() err %q, want <nil>", test.ad, err)
			}
			if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
				t.Errorf("ad=%v, ad.proto() returned diff (-want +got):\n%s", test.ad, diff)
			}
		})
	}
}
