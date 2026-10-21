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

// Package icon contains all commands for icon handling.
package icon

import (
	"context"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/icon/go/icon"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"

	"github.com/pkg/errors"
	"google.golang.org/grpc/metadata"
)

var (
	flagVerbose      bool
	flagInstanceName string
	flags            *cmdutils.CmdFlags
)

// makeIconClient is a variable instead of a regular function to allow tests
// to override it and inject a mock client (e.g., connecting via a Unix domain socket).
var makeIconClient = func(ctx context.Context) (context.Context, icon.Client, error) {
	ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to dial cluster")
	}

	if flagInstanceName != "" {
		ctx = metadata.AppendToOutgoingContext(ctx, "x-resource-instance-name", flagInstanceName)
	}
	client := icon.InitClientFromConn(conn)

	return ctx, client, nil
}

var iconCmd = cobrautil.ParentOfNestedSubcommands("icon", "Introspect and operate ICON")

func init() {
	flags = cmdutils.NewCmdFlags()
	flags.SetCommand(iconCmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrgOptional()

	iconCmd.PersistentFlags().StringVar(&flagInstanceName, "instance_name", "", "name of the ICON instance to connect to")

	root.RootCmd.AddCommand(iconCmd)
}
