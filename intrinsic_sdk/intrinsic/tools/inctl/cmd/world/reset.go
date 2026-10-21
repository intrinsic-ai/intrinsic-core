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
	"fmt"

	"intrinsic/assets/clientutils"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	pb "intrinsic/conductor/proto/conductor_go_proto"
)

var makeConductorClient = func(ctx context.Context) (context.Context, *grpc.ClientConn, error) {
	ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to dial cluster")
	}
	return ctx, conn, nil
}

var resetCmd = &cobra.Command{
	Use:   "reset",
	Short: "Resets the belief world to match the initial world, then restarts simulation from the new belief world.",
	Long: `Resets the belief world to match the initial world, then restarts simulation from the new belief world.

If running on real hardware, this command won't physically move any robots.
Additionally, resetting simulation can sometimes take more than a few seconds.`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		ctx, conn, err := makeConductorClient(ctx)
		if err != nil {
			return errors.Wrap(err, "unable to create conductor client")
		}
		defer conn.Close()

		client := pb.NewConductorServiceClient(conn)

		_, err = client.Reset(ctx, &pb.ResetRequest{})
		if err != nil {
			return errors.Wrap(err, "failed to reset world")
		}

		fmt.Printf("Successfully reset worlds\n")
		return nil
	},
}

func init() {
	WorldCmd.AddCommand(resetCmd)
}
