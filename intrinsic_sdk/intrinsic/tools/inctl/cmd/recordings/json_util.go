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

package recordings

import (
	"encoding/json"
	"io"
	"os"

	"github.com/spf13/cobra"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"

	"intrinsic/tools/inctl/cmd/root"
)

// JSONOutput represents the standard output envelope for JSON format
type JSONOutput struct {
	Status   string          `json:"status"`
	Message  string          `json:"message,omitempty"`
	Warnings []string        `json:"warnings,omitempty"`
	Data     json.RawMessage `json:"data,omitempty"`
}

// IsJSON returns true if the global --output flag is set to json
func IsJSON(cmd *cobra.Command) bool {
	return root.FlagOutput == "json"
}

// emitJSONSuccess outputs the given payload wrapped in the success envelope
func emitJSONSuccess(out io.Writer, payload any, warnings ...string) {
	envelope := JSONOutput{
		Status:   "success",
		Warnings: warnings,
	}

	if payload != nil {
		marshaller := protojson.MarshalOptions{UseProtoNames: true, EmitUnpopulated: true}

		if pbMsg, ok := payload.(proto.Message); ok {
			// Ensure we format protobuf messages correctly
			b, err := marshaller.Marshal(pbMsg)
			if err == nil {
				envelope.Data = json.RawMessage(b)
			}
		} else if pbSlice, ok := payload.([]proto.Message); ok {
			var rawMsgs []json.RawMessage
			for _, m := range pbSlice {
				b, err := marshaller.Marshal(m)
				if err == nil {
					rawMsgs = append(rawMsgs, json.RawMessage(b))
				}
			}
			b, err := json.Marshal(rawMsgs)
			if err == nil {
				envelope.Data = json.RawMessage(b)
			}
		} else {
			// For generic maps or structs
			b, err := json.Marshal(payload)
			if err == nil {
				envelope.Data = json.RawMessage(b)
			}
		}
	}

	enc := json.NewEncoder(out)
	enc.SetIndent("", "  ")
	enc.Encode(envelope)
}

// JSONFailFunc returns a closure that will intercept the error and output
// a formatted JSON error if JSON output is requested, and then exit with status 1.
// If JSON is not requested, it just returns the error normally.
func JSONFailFunc(cmd *cobra.Command) func(error) error {
	return func(err error) error {
		if err == nil {
			return nil
		}
		if IsJSON(cmd) {
			envelope := JSONOutput{
				Status:  "error",
				Message: err.Error(),
			}
			enc := json.NewEncoder(cmd.OutOrStdout())
			enc.SetIndent("", "  ")
			enc.Encode(envelope)
			os.Exit(1)
		}
		return err
	}
}
