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

package client

import (
	"context"
	"fmt"
)

// StatusState indicates type of hasStatus performed related to update operation on server
type StatusState int

const (
	// StatusUndetermined indicates that nature of action was not known, init state
	StatusUndetermined StatusState = iota
	// StatusContinue indicates in flying hasStatus update
	StatusContinue
	// StatusSuccess indicates that update operation finished successfully
	StatusSuccess
	// StatusFailure indicates that update operation finished unsuccessfully
	StatusFailure
)

var names = []string{"Undetermined", "Continue", "Success", "Failure"}

func (s StatusState) String() string {
	return names[s]
}

// ProgressUpdate represents information about upload update.
type ProgressUpdate struct {
	Status  StatusState
	Current int64
	Total   int64
	Err     error
	Message string
}

func (p ProgressUpdate) String() string {
	return fmt.Sprintf("%s: (%d/%d); Err: %v; msg: %s",
		p.Status, p.Current, p.Total, p.Err, p.Message)
}

// ProgressMonitor allows callers to receive update about uploads.
type ProgressMonitor interface {
	// UpdateProgress is called every time there is update received from server
	UpdateProgress(ref string, update ProgressUpdate)
}

type progressMonitorKeyType string

const progressMonitorCtxKey = progressMonitorKeyType("progressMonitor")

// SetProgressMonitor attaches progress monitor implementation to context
func SetProgressMonitor(ctx context.Context, monitor ProgressMonitor) context.Context {
	return context.WithValue(ctx, progressMonitorCtxKey, monitor)
}

func getProgressMonitor(ctx context.Context) ProgressMonitor {
	if monitor := ctx.Value(progressMonitorCtxKey); monitor != nil {
		return monitor.(ProgressMonitor)
	}
	return nil
}
