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

// Package cluster contains the externally available commands for cluster handling.
package cluster

import (
	"context"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/viper"
	"google.golang.org/grpc"
)

const (
	// KeyIntrinsic is used across inctl cluster to specify the prefix for viper's env integration.
	KeyIntrinsic = "intrinsic"
)

// ClusterCmdViper is used across inctl cluster to integrate cmdline parsing with environment variables.
var ClusterCmdViper = viper.New()

// ClusterCmd is the `inctl cluster` command.
var ClusterCmd = orgutil.WrapCmd(cobrautil.ParentOfNestedSubcommands(
	root.ClusterCmdName, "Workcell cluster handling"), ClusterCmdViper)

func init() {
	root.RootCmd.AddCommand(ClusterCmd)
}

func NewCloudConn(ctx context.Context) (*grpc.ClientConn, error) {
	return auth.NewCloudConnection(ctx, auth.WithFlagValues(ClusterCmdViper))
}
