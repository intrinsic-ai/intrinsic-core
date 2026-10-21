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

package organization

import (
	"context"
	"fmt"
	"time"

	"intrinsic/tools/inctl/util/accounts/accounts"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"

	pb "intrinsic/kubernetes/accounts/service/api/invitations/v1/invitations_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

func init() {
	joinCmd.Flags().StringVar(&flagInvitationToken, "token", "", "The token of the invitation to accept.")
	organizationCmd.AddCommand(joinCmd)
}

var joinCmdHelp = `
Accept an invitation to join an organization using an invitation token.

Example:

		inctl organization join 24d7ab1f-8c55-4903-9352-4ce421bef264
		inctl organization join --token=24d7ab1f-8c55-4903-9352-4ce421bef264
`

var joinCmd = &cobra.Command{
	Use:   "join [token]",
	Short: "Accept an invitation to join an organization.",
	Long:  joinCmdHelp,
	Args:  cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		token, err := resolveInvitationTokenArgOrFlag(args)
		if err != nil {
			return err
		}
		ctx := cmd.Context()
		cl, err := newInvitationsV1Client(ctx)
		if err != nil {
			return err
		}
		req := &pb.ApplyInvitationRequest{
			Token: token,
		}
		if flagDebugRequests {
			protoPrint(req)
		}
		fmt.Printf("Joining organization via invitation token %q...\n", token)
		lrop, err := cl.ApplyInvitation(ctx, req)
		if err != nil {
			return fmt.Errorf("failed to apply invitation: %w", err)
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		// The service does not expose a GetOperation method; calling ApplyInvitation again polls operation status.
		// Note: ApplyInvitation can be flaky when repeatedly invoked for the same invitation while pending.
		// If ApplyInvitation returns an error during polling, swallow the transient error and return an unfinished operation.
		getOp := func(ctx context.Context, _ *lropb.GetOperationRequest, opts ...grpc.CallOption) (*lropb.Operation, error) {
			op, err := cl.ApplyInvitation(ctx, req, opts...)
			if err != nil {
				return &lropb.Operation{Name: lrop.GetName(), Done: false}, nil
			}
			return op, nil
		}
		lrop, err = accounts.WaitForOperation(ctx, getOp, lrop, 2*time.Minute)
		if err != nil {
			return fmt.Errorf("failed to wait for operation: %w", err)
		}
		if flagDebugRequests {
			protoPrint(lrop)
		}
		if lrop.GetError() != nil {
			return fmt.Errorf("failed to join organization: %v", lrop.GetError())
		}
		fmt.Printf("Successfully joined organization.\n")
		return nil
	},
}
