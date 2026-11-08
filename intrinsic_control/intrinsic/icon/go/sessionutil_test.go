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

package sessionutil

import (
	"errors"
	"fmt"
	"testing"

	"intrinsic/icon/go/icon"
)

// waitForCancel is a stub implementation of NextEventProvider.
type waitForCancel struct{}

// NextEvent implements the NextEventProvider interface. It waits for cancel
// to be closed then returns icon.ErrCanceled.
func (waitForCancel) NextEvent(cancel <-chan struct{}) (icon.Event, error) {
	_ = <-cancel
	return nil, icon.ErrCanceled
}

func TestWaitForEventSignalCanceled(t *testing.T) {
	cancel := make(chan struct{})
	close(cancel)
	err := WaitForEventSignal(waitForCancel{}, icon.EventSignal{}, cancel)
	if !errors.Is(err, icon.ErrCanceled) {
		t.Errorf("sessionutil.WaitForEventSignal(waitForCancel{}, icon.EventSignal{}, cancel) err is %q, want %q", err, icon.ErrCanceled)
	}
}

// eventQueue is a fake implementation of NextEventProvider.
type eventQueue struct {
	signals []icon.EventSignal
}

// NextEvent implements the NextEventProvider interface. It pops the first
// element in q.signals and returns it as an icon.Event.
func (q *eventQueue) NextEvent(<-chan struct{}) (icon.Event, error) {
	if len(q.signals) == 0 {
		return nil, fmt.Errorf("no more events")
	}
	var s icon.EventSignal
	s, q.signals = q.signals[0], q.signals[1:]
	return &icon.ReactionEvent{EventSignal: s}, nil
}

func TestWaitForEventSignal(t *testing.T) {
	var s icon.Session
	es := s.MakeEventSignal()
	q := &eventQueue{
		signals: []icon.EventSignal{s.MakeEventSignal(), es},
	}
	if err := WaitForEventSignal(q, es, nil); err != nil {
		t.Errorf("WaitForEventSignal(q, %v, nil) err is %q, want <nil>", es, err)
	}
}
