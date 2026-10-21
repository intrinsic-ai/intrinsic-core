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

// Package operationmode provides features related to operation mode.
package operationmode

import opmodepb "intrinsic/config/proto/operation_mode_go_proto"

const (
	// Real is the command line string to start in real mode.
	Real = "real"
	// Sim is the command line string to start in sim mode.
	Sim = "sim"
)

// FromString transforms an operation mode string into a proto enum value.
func FromString(mode string) opmodepb.OperationMode {
	switch mode {
	case Real:
		return opmodepb.OperationMode_REAL_HARDWARE
	case Sim:
		return opmodepb.OperationMode_SIMULATION
	default:
		return opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED
	}
}
