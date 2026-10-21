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

package world

import (
	"context"
	"strings"

	"intrinsic/assets/clientutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/encoding/prototext"

	owspb "intrinsic/world/proto/object_world_service_go_proto"
)

const defaultWorldID = "world"

// flagWorldID holds the value of the --world_id flag.
var flagWorldID string

// dialCluster connects `world print` to the cluster selected by the command
// line flags. It is a variable so that tests can substitute an in-process
// server.
var dialCluster = func(ctx context.Context) (context.Context, *grpc.ClientConn, error) {
	ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to dial cluster")
	}
	return ctx, conn, nil
}

// worldView adapts a World proto to the inctl printer: String renders it as
// multi-line text proto for humans and MarshalJSON renders it as JSON for
// `--output=json`.
type worldView struct {
	world *owspb.World
}

// String returns the world in text proto format without a trailing newline.
func (v *worldView) String() string {
	return strings.TrimSuffix(prototext.MarshalOptions{Multiline: true}.Format(v.world), "\n")
}

// MarshalJSON returns the world in the canonical proto JSON mapping.
func (v *worldView) MarshalJSON() ([]byte, error) {
	return protojson.Marshal(v.world)
}

var printCmd = &cobra.Command{
	Use:   "print",
	Short: "Prints the contents of a world.",
	Long: `Prints the full contents of a world (metadata, objects and collision settings)
as retrieved from the object world service of a running solution.

The default text output is intended for humans and its exact formatting may
change. Use --output=json for output that is stable and suitable for scripts.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		prtr, err := printer.NewPrinterFromCommand(cmd)
		if err != nil {
			return errors.Wrap(err, "failed to create printer")
		}

		ctx, conn, err := dialCluster(cmd.Context())
		if err != nil {
			return err
		}
		defer conn.Close()

		client := owspb.NewObjectWorldServiceClient(conn)
		w, err := client.GetWorld(ctx, &owspb.GetWorldRequest{
			WorldId: flagWorldID,
			View:    owspb.WorldView_WORLD_VIEW_FULL,
		})
		if err != nil {
			return errors.Wrapf(err, "failed to get world %q", flagWorldID)
		}

		prtr.Println(&worldView{world: w})
		return printer.Flush(prtr)
	},
}

func init() {
	printCmd.Flags().StringVar(&flagWorldID, "world_id", defaultWorldID, "Id of the world to print.")
	WorldCmd.AddCommand(printCmd)
}
