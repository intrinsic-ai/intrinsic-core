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
	"slices"
	"strconv"
	"strings"

	"github.com/spf13/cobra"

	pb "intrinsic/hardware/gpio/v1/gpio_service_go_proto"
	spb "intrinsic/hardware/gpio/v1/signal_go_proto"
)

func listSignals(ctx context.Context, opts ClientOptions) error {
	ctx, client, err := makeGPIOClient(ctx, opts)
	if err != nil {
		return err
	}
	defer client.Close()

	signals, err := client.GetSignalDescriptions(ctx, &pb.GetSignalDescriptionsRequest{})
	if err != nil {
		return err
	}
	sds := signals.GetSignalDescriptions()

	maxSignalNameLength := 0
	for _, signal := range sds {
		if len(signal.SignalName) > maxSignalNameLength {
			maxSignalNameLength = len(signal.SignalName)
		}
	}

	formatString := "%-" + strconv.Itoa(maxSignalNameLength) + "s %-2s %-10s\n"

	fmt.Printf(formatString, "NAME", "RW", "TYPE")
	// We mutate the underlying proto, but it's fine since we're not using it after this.
	slices.SortFunc(sds, func(a, b *spb.SignalDescription) int {
		return strings.Compare(a.GetSignalName(), b.GetSignalName())
	})
	for _, signal := range sds {
		rw := ""
		if signal.GetCanRead() {
			rw += "r"
		}
		if signal.GetCanWrite() {
			rw += "w"
		}

		fmt.Printf(formatString, signal.SignalName, rw, signal.GetType())
	}
	return nil
}

var gpioListSignalsCmd = &cobra.Command{
	Use:   "list-signals",
	Short: "Print a list of signals",
	RunE: func(cmd *cobra.Command, args []string) error {
		return listSignals(cmd.Context(), clientOptionsFromFlags())
	},
}

func init() {
	gpioCmd.AddCommand(gpioListSignalsCmd)
}
