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
	"maps"
	"slices"
	"strconv"

	"github.com/spf13/cobra"

	pb "intrinsic/hardware/gpio/v1/gpio_service_go_proto"
	signalpb "intrinsic/hardware/gpio/v1/signal_go_proto"
)

func readSignals(ctx context.Context, opts ClientOptions, signalNames []string) error {
	ctx, client, err := makeGPIOClient(ctx, opts)
	if err != nil {
		return err
	}
	defer client.Close()

	req := &pb.ReadSignalsRequest{SignalNames: signalNames}
	resp, err := client.ReadSignals(ctx, req)
	if err != nil {
		return err
	}

	if resp.GetSignalValues() == nil || len(resp.GetSignalValues().GetValues()) == 0 {
		fmt.Println("No signals found!")
		return nil
	}

	maxSignalNameLength := 0
	for _, signalName := range signalNames {
		if len(signalName) > maxSignalNameLength {
			maxSignalNameLength = len(signalName)
		}
	}
	formatString := "%-" + strconv.Itoa(maxSignalNameLength) + "s  %-15s\n"

	fmt.Printf(formatString, "NAME", "VALUE")
	for _, name := range slices.Sorted(maps.Keys(resp.GetSignalValues().GetValues())) {
		switch val := resp.GetSignalValues().GetValues()[name].Value.(type) {
		case *signalpb.SignalValue_BoolValue:
			fmt.Printf(formatString, name, strconv.FormatBool(val.BoolValue))
		case *signalpb.SignalValue_UnsignedIntValue:
			fmt.Printf(formatString, name, strconv.FormatUint(uint64(val.UnsignedIntValue), 10))
		case *signalpb.SignalValue_IntValue:
			fmt.Printf(formatString, name, strconv.FormatInt(int64(val.IntValue), 10))
		case *signalpb.SignalValue_FloatValue:
			fmt.Printf(formatString, name, strconv.FormatFloat(float64(val.FloatValue), 'f', 6, 32))
		case *signalpb.SignalValue_DoubleValue:
			fmt.Printf(formatString, name, strconv.FormatFloat(val.DoubleValue, 'f', 6, 64))
		case *signalpb.SignalValue_Int8Value:
			fmt.Printf(formatString, name, strconv.FormatInt(int64(val.Int8Value.Value), 10))
		case *signalpb.SignalValue_UnsignedInt8Value:
			fmt.Printf(formatString, name, "0x"+strconv.FormatUint(uint64(val.UnsignedInt8Value.Value), 16))
		}
	}
	return nil
}

var flagSignalNames []string

var gpioReadSignalsCmd = &cobra.Command{
	Use:   "read-signals",
	Short: "Print the values of signals",
	RunE: func(cmd *cobra.Command, args []string) error {
		return readSignals(cmd.Context(), clientOptionsFromFlags(), flagSignalNames)
	},
}

func init() {
	gpioReadSignalsCmd.PersistentFlags().StringSliceVar(&flagSignalNames, "signal_names", []string{}, "signal names to read")
	gpioCmd.AddCommand(gpioReadSignalsCmd)
}
