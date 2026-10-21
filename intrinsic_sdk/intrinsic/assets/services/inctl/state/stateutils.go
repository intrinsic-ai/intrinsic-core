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

// Package stateutils contains utility functions for introspecting and modifying the state of a
// running service asset in a solution.
package stateutils

import (
	"fmt"
	"strings"

	"intrinsic/tools/inctl/util/printer"

	"golang.org/x/text/cases"
	"golang.org/x/text/language"
	"google.golang.org/protobuf/encoding/protojson"

	systemservicestatepb "intrinsic/assets/services/proto/v1/system_service_state_go_proto"
)

// StatePrinter is a struct that contains the state of a running service asset in a solution. It
// contains the proto representation of the state and the output format to use when printing.
type StatePrinter struct {
	Proto      *systemservicestatepb.InstanceState
	OutputType printer.OutputType
}

func (p *StatePrinter) String() string {
	if p.OutputType == printer.OutputTypeJSON {
		return protojson.MarshalOptions{Indent: "  "}.Format(p.Proto)
	}

	indent := strings.Repeat(" ", 4)
	out := fmt.Sprintf(`%s:
%sState: %s`,
		p.Proto.GetName(),
		indent,
		cases.Title(language.English).String(
			strings.TrimPrefix(p.Proto.GetState().GetStateCode().String(), "STATE_CODE_"),
		),
	)

	if p.Proto.GetState().GetExtendedStatus().GetTitle() != "" {
		out += fmt.Sprintf(`
%sStatus: %s`, indent, p.Proto.GetState().GetExtendedStatus().GetTitle())
	}

	if p.Proto.GetState().GetExtendedStatus().GetUserReport().GetMessage() != "" {
		out += fmt.Sprintf(`
%sMessage: %s`, indent, p.Proto.GetState().GetExtendedStatus().GetUserReport().GetMessage())
	}

	return out
}

// ListStatesPrinter is a struct that contains the states of all running service assets in a
// solution. It contains the proto representation of the state and the output format to use when
// printing.
type ListStatesPrinter struct {
	Proto      *systemservicestatepb.ListInstanceStatesResponse
	OutputType printer.OutputType
}

func (p *ListStatesPrinter) String() string {
	if p.OutputType == printer.OutputTypeJSON {
		return protojson.MarshalOptions{Indent: "  "}.Format(p.Proto)
	}

	out := ""
	for _, state := range p.Proto.GetStates() {
		prtr := &StatePrinter{
			Proto:      state,
			OutputType: p.OutputType,
		}
		out = fmt.Sprintf("%s%s\n", out, prtr.String())
	}
	return out
}
