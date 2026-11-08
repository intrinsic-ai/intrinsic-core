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
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
)

func TestReactionData_InsertErrors(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()
	es := s.MakeEventSignal()
	ah2 := s.MakeActionHandle()
	invalidActionHandle := ActionHandle{}
	tests := []struct {
		// initialEntries are the entries to insert first; should succeed.
		initialEntries []reactionDataEntry
		// entries are the data to then insert; should fail.
		entries []reactionDataEntry
		// wantErr is the expected error (which may be wrapped).
		wantErr error
	}{{
		entries: []reactionDataEntry{{
			Action:  &invalidActionHandle,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}},
		wantErr: ErrInvalidActionHandle,
	}, {
		entries: []reactionDataEntry{{
			Action: &ah2,
			Signal: s.MakeEventSignal(),
		}},
		wantErr: errInvalidReactionID,
	}, {
		entries: []reactionDataEntry{{
			Action:  &ah2,
			ReactID: 1,
		}},
		wantErr: ErrInvalidEventSignal,
	}, {
		initialEntries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  es,
		}},
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  es,
		}},
		wantErr: errAlreadyExists,
	}, {
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  es,
		}, {
			Action:  &ah,
			ReactID: 1,
			Signal:  es,
		}},
		wantErr: errAlreadyExists,
	}, {
		initialEntries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}},
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}},
		wantErr: errReactionIDAlreadyAssociated,
	}, {
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}, {
			Action:  &ah,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}},
		wantErr: errReactionIDAlreadyAssociated,
	}}
	for _, test := range tests {
		t.Run(fmt.Sprintf("Want err %s", test.wantErr), func(t *testing.T) {
			d := newReactionData()
			if err := d.Insert(test.initialEntries...); err != nil {
				t.Fatalf("d.Insert(%v) err is %q, want <nil>", test.initialEntries, err)
			}
			if err := d.Insert(test.entries...); !errors.Is(err, test.wantErr) {
				t.Fatalf("d.Insert(%v) err is %q, want an error that is (or wraps) %q", test.entries, err, test.wantErr)
			}
		})
	}
}

func TestReactionData_RemoveActions(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()
	ah2 := s.MakeActionHandle()
	ah3 := s.MakeActionHandle()
	es := s.MakeEventSignal()
	tests := []struct {
		// entries are the data to insert.
		entries []reactionDataEntry
		// remove are ActionHandles to then remove.
		remove []ActionHandle
		// want holds the expected output of ListSignals for various
		// ReactionIDs
		want map[ReactionID][]EventSignal
	}{{
		entries: nil,
		remove:  nil,
		want: map[ReactionID][]EventSignal{
			1: nil,
		},
	}, {
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  es,
		}, {
			Action:  &ah3,
			ReactID: 2,
			Signal:  es,
		}},
		remove: []ActionHandle{ah},
		want: map[ReactionID][]EventSignal{
			1: nil,
			2: {es},
		},
	}, {
		entries: []reactionDataEntry{{
			Action:  &ah,
			ReactID: 1,
			Signal:  s.MakeEventSignal(),
		}, {
			Action:  &ah,
			ReactID: 2,
			Signal:  s.MakeEventSignal(),
		}, {
			Action:  &ah2,
			ReactID: 3,
			Signal:  s.MakeEventSignal(),
		}, {
			Action:  &ah3,
			ReactID: 4,
			Signal:  es,
		}},
		remove: []ActionHandle{ah, ah2},
		want: map[ReactionID][]EventSignal{
			1: nil,
			2: nil,
			3: nil,
			4: {es},
		},
	}}
	for _, test := range tests {
		t.Run(fmt.Sprintf("Remove %v", test.remove), func(t *testing.T) {
			d := newReactionData()
			if err := d.Insert(test.entries...); err != nil {
				t.Fatalf("d.Insert(%v) err is %v, want <nil>", test.entries, err)
			}
			d.RemoveActions(test.remove...)
			for reaction, wantSignals := range test.want {
				got := d.ListSignals(reaction)
				if diff := cmp.Diff(wantSignals, got); diff != "" {
					t.Errorf("d.ListSignals(%d) returned diff (-want +got):\n%s", reaction, diff)
				}
			}
		})
	}
}

func TestReactionData_Clear(t *testing.T) {
	var s Session
	ah := s.MakeActionHandle()

	entries := []reactionDataEntry{{
		Action:  &ah,
		ReactID: 1,
		Signal:  s.MakeEventSignal(),
	}, {
		Action:  &ah,
		ReactID: 2,
		Signal:  s.MakeEventSignal(),
	}}
	d := newReactionData()
	if err := d.Insert(entries...); err != nil {
		t.Fatalf("d.Insert(%v) err is %v, want <nil>", entries, err)
	}
	d.Clear()
	for _, e := range entries {
		if got := d.ListSignals(e.ReactID); got != nil {
			t.Errorf("d.ListSignals(%d) = %v, want <nil>", e.ReactID, got)
		}
	}
}
