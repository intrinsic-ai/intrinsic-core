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

package gpio

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	codespb "google.golang.org/grpc/codes"
	"google.golang.org/protobuf/encoding/prototext"

	pb "intrinsic/hardware/gpio/v1/gpio_service_go_proto"
	signalpb "intrinsic/hardware/gpio/v1/signal_go_proto"
)

// tryConvertSignalValue attempts to convert value into a SignalValue message,
// first by attempting to unmarshal it as pbtxt, then by comparing it to
// various versions of "true" and "false".
func tryConvertSignalValue(value string) (*signalpb.SignalValue, error) {
	var signalValue = new(signalpb.SignalValue)
	if err := prototext.Unmarshal([]byte(value), signalValue); err == nil {
		return signalValue, nil
	}

	lowerValue := strings.ToLower(value)
	if lowerValue == "true" {
		signalValue.Value = &signalpb.SignalValue_BoolValue{BoolValue: true}
		return signalValue, nil
	}

	if lowerValue == "false" {
		signalValue.Value = &signalpb.SignalValue_BoolValue{BoolValue: false}
		return signalValue, nil
	}

	return nil, fmt.Errorf("failed to convert value argument %q to a SignalValue", value)
}

func writeSignal(ctx context.Context, opts ClientOptions, signalName, value string) error {
	val, err := tryConvertSignalValue(value)
	if err != nil {
		return err
	}

	ctx, client, err := makeGPIOClient(ctx, opts)
	if err != nil {
		return err
	}
	defer client.Close()

	initialReq := &pb.OpenWriteSessionRequest{
		InitialSessionData: &pb.OpenWriteSessionRequest_InitialSessionData{
			SignalNames: []string{signalName},
		},
	}
	session, err := client.OpenWriteSession(ctx)
	if err != nil {
		return errors.Wrap(err, "open")
	}
	defer func() {
		if err := session.CloseSend(); err != nil {
			fmt.Printf("Failed to close write session: %v\n", err)
		}
	}()

	if err := session.Send(initialReq); err != nil {
		return errors.Wrap(err, "failed to send initial req")
	}
	resp, err := session.Recv()
	if err != nil {
		return errors.Wrap(err, "failed to receive")
	}

	if got, want := resp.GetStatus().GetCode(), int32(codespb.OK); got != want {
		return fmt.Errorf("initial session failed, got %v, want %v", got, want)
	}

	writeReq := &pb.OpenWriteSessionRequest{
		ActionRequest: &pb.OpenWriteSessionRequest_WriteSignals{
			WriteSignals: &pb.WriteSignalsRequest{
				SignalValues: &signalpb.SignalValueSet{
					Values: map[string]*signalpb.SignalValue{
						signalName: val,
					},
				},
			},
		},
	}

	if err := session.Send(writeReq); err != nil {
		return errors.Wrap(err, "failed to send write req")
	}

	writeResp, err := session.Recv()
	if err != nil {
		return errors.Wrap(err, "failed to receive write resp")
	}
	if got, want := writeResp.GetStatus().GetCode(), int32(codespb.OK); got != want {
		return fmt.Errorf("write failed, got %v, want %v", got, want)
	}
	return nil
}

var (
	flagSignalName string
	flagValue      string
)

var gpioWriteSignalsCmd = &cobra.Command{
	Use:   "write-signal",
	Short: "Sets the value of a signal",
	RunE: func(cmd *cobra.Command, args []string) error {
		return writeSignal(cmd.Context(), clientOptionsFromFlags(), flagSignalName, flagValue)
	},
}

func init() {
	gpioWriteSignalsCmd.PersistentFlags().StringVar(&flagSignalName, "signal_name", "", "signal name to set")
	gpioWriteSignalsCmd.PersistentFlags().StringVar(&flagValue, "value", "false", "value to set")
	gpioCmd.AddCommand(gpioWriteSignalsCmd)
}
