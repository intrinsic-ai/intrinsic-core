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

// Package sessionutil provides common helpers for working with icon Sessions.
package sessionutil

import (
	"intrinsic/icon/go/icon"
)

// NextEventProvider is an interface for receiving icon session events.
// The icon.Session struct implements this interface.
type NextEventProvider interface {
	// NextEvent returns the next event from an event queue. This blocks
	// until either (i) an event is received, OR (ii) if cancel is non-nil,
	// the cancel channel is written to or closed, OR (iii) an error
	// occurs.
	NextEvent(cancel <-chan struct{}) (icon.Event, error)
}

// WaitForEventSignal calls s.NextEvent repeatedly until an event matching es
// is received. This blocks until either (i) an event matching es is received,
// OR (ii) if cancel is non-nil, the cancel channel is written to or closed, OR
// (iii) NextEvent returns with an error.
func WaitForEventSignal(s NextEventProvider, es icon.EventSignal, cancel <-chan struct{}) error {
	for {
		event, err := s.NextEvent(cancel)
		if err != nil {
			return err
		}
		if e, ok := event.(*icon.ReactionEvent); ok && e.EventSignal == es {
			return nil
		}
	}
}
