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
	"github.com/google/go-cmp/cmp/cmpopts"
	"google.golang.org/protobuf/testing/protocmp"

	typespb "intrinsic/icon/proto/v1/types_go_proto"
)

func TestReactionExtractEventSignals(t *testing.T) {
	var s Session
	es := s.MakeEventSignal()
	es2 := s.MakeEventSignal()
	tests := []struct {
		name     string // nested function invocation as string
		reaction *Reaction
		output   []EventSignal
	}{{
		name:     "NewReaction(IsTrue(\"foo\"))",
		reaction: NewReaction(IsTrue("foo")),
		output:   nil,
	}, {
		name:     "NewReaction(IsTrue(\"foo\"), EmitEventSignal(es))",
		reaction: NewReaction(IsTrue("foo"), EmitEventSignal(es)),
		output:   []EventSignal{es},
	}, {
		name:     "NewReaction(IsTrue(\"foo\"),  StartActionInRealTime(s.MakeActionHandle()), EmitEventSignal(es), EmitEventSignal(es2))",
		reaction: NewReaction(IsTrue("foo"), StartActionInRealTime(s.MakeActionHandle()), EmitEventSignal(es), EmitEventSignal(es2)),
		output:   []EventSignal{es, es2},
	}}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			output, err := test.reaction.extractEventSignals()
			if err != nil {
				t.Errorf("err %q, want nil", err)
			}
			if got, want := output, test.output; !cmp.Equal(got, want, cmpopts.EquateEmpty()) {
				t.Errorf("%s = %v, want %v", test.name, got, want)
			}
		})
	}
}

func TestReactionExtractEventSignalsError(t *testing.T) {
	name := "NewReaction(IsTrue(\"foo\"), &EmitEventSignalResponse{})"
	r := NewReaction(IsTrue("foo"), &EmitEventSignalResponse{})
	if _, err := r.extractEventSignals(); err != ErrInvalidEventSignal {
		t.Errorf("%q err is %q, want %q", name, err, ErrInvalidEventSignal)
	}
}

func TestReactionProto(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()
	ah2 := s.MakeActionHandle()
	es := s.MakeEventSignal()
	tests := []struct {
		name     string // nested function invocation as string
		reaction *Reaction
		action   ActionHandle
		id       ReactionID
		want     *typespb.Reaction
	}{{
		name:     "NewReaction(IsTrue(\"foo\"), StartActionInRealTime(ah2))",
		reaction: NewReaction(IsTrue("foo"), StartActionInRealTime(ah2)),
		action:   ah,
		id:       ReactionID(123),
		want: &typespb.Reaction{
			ActionAssociation: &typespb.Reaction_ActionAssociation{
				ActionInstanceId:     int64(ah.ID()),
				StopAssociatedAction: true,
			},
			Condition:          IsTrue("foo"),
			ReactionInstanceId: int64(123),
			Response:           &typespb.Response{StartActionInstanceId: int64(ah2.ID())},
		},
	}, {
		name:     "NewReaction(IsTrue(\"foo\"), StartParallelActionInRealTime(ah2))",
		reaction: NewReaction(IsTrue("foo"), StartParallelActionInRealTime(ah2)),
		action:   ah,
		id:       ReactionID(123),
		want: &typespb.Reaction{
			ActionAssociation: &typespb.Reaction_ActionAssociation{
				ActionInstanceId: int64(ah.ID()),
			},
			Condition:          IsTrue("foo"),
			ReactionInstanceId: int64(123),
			Response:           &typespb.Response{StartActionInstanceId: int64(ah2.ID())},
		},
	}, {
		name:     "NewReaction(IsTrue(\"foo\"), EmitEventSignal(es))",
		reaction: NewReaction(IsTrue("foo"), EmitEventSignal(es)),
		action:   ah,
		id:       ReactionID(123),
		want: &typespb.Reaction{
			ActionAssociation: &typespb.Reaction_ActionAssociation{
				ActionInstanceId: int64(ah.ID()),
			},
			Condition:          IsTrue("foo"),
			ReactionInstanceId: 123,
		},
	}}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			got, err := test.reaction.proto(&test.action, test.id)
			if err != nil {
				t.Errorf(".proto(%v, %d) err %q, want nil", test.action, test.id, err)
			}
			if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
				t.Errorf(".proto(%v, %d) returned diff (-want +got):\n%s", test.action, test.id, diff)
			}
		})
	}
}

func TestReactionProtoErrors(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()
	tests := []struct {
		name     string // nested function invocation as string
		reaction *Reaction
		action   ActionHandle
		id       ReactionID
		err      error
	}{
		{
			name:     "NewReaction(IsTrue(\"foo\"), StartActionInRealTime(ah))",
			reaction: NewReaction(IsTrue("foo"), StartActionInRealTime(ah)),
			action:   ActionHandle{},
			id:       ReactionID(123),
			err:      ErrInvalidActionHandle,
		},
		{
			name:     "NewReaction(IsTrue(\"foo\"), StartParallelActionInRealTime(ah))",
			reaction: NewReaction(IsTrue("foo"), StartParallelActionInRealTime(ah)),
			action:   ActionHandle{},
			id:       ReactionID(123),
			err:      ErrInvalidActionHandle,
		},
		{
			name:     "NewReaction(IsTrue(\"foo\"))",
			reaction: NewReaction(IsTrue("foo")),
			action:   ah,
			id:       ReactionID(123),
			err:      ErrMissingResponse,
		},
		{
			name:     "NewReaction(IsTrue(\"foo\"), StartActionInRealTime(ActionHandle{}))",
			reaction: NewReaction(IsTrue("foo"), StartActionInRealTime(ActionHandle{})),
			action:   ah,
			id:       ReactionID(123),
			err:      ErrInvalidActionHandle,
		},
		{
			name:     "NewReaction(IsTrue(\"foo\"), StartParallelActionInRealTime(ActionHandle{}))",
			reaction: NewReaction(IsTrue("foo"), StartParallelActionInRealTime(ActionHandle{})),
			action:   ah,
			id:       ReactionID(123),
			err:      ErrInvalidActionHandle,
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if _, err := test.reaction.proto(&test.action, test.id); err != test.err {
				t.Errorf(".proto(%v, %d) err is %q, want %q", test.action, test.id, err, test.err)
			}
		})
	}
}
