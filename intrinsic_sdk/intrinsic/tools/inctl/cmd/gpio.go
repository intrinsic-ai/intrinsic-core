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

// Package gpio contains all commands for gpio handling.
package gpio

import (
	"context"
	"io"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"

	gpiogrpcpb "intrinsic/hardware/gpio/v1/gpio_service_go_proto"
)

var (
	flagInstanceName string
	flags            *cmdutils.CmdFlags
)

// Client is the interface for interacting with the GPIO service and closing the connection.
type Client interface {
	gpiogrpcpb.GPIOServiceClient
	io.Closer
}

type clientImpl struct {
	gpiogrpcpb.GPIOServiceClient
	conn *grpc.ClientConn
}

func (c *clientImpl) Close() error {
	if c.conn != nil {
		return c.conn.Close()
	}
	return nil
}

// ClientOptions contains options for connecting to a GPIO service.
type ClientOptions struct {
	Address      string
	Cluster      string
	Solution     string
	Org          string
	Project      string
	InstanceName string
}

func clientOptionsFromFlags() ClientOptions {
	return ClientOptions{
		Address:      flags.GetString("address"),
		Cluster:      flags.GetString("cluster"),
		Solution:     flags.GetString("solution"),
		Org:          flags.GetFlagOrganization(),
		Project:      flags.GetFlagProject(),
		InstanceName: flagInstanceName,
	}
}

func defaultDialCluster(ctx context.Context, opts ClientOptions) (context.Context, *grpc.ClientConn, error) {
	ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
	return ctx, conn, err
}

// dialCluster connects to a cluster and is a package variable to allow injecting
// mock connections in tests.
var dialCluster = defaultDialCluster

func defaultMakeGPIOClient(ctx context.Context, opts ClientOptions) (context.Context, Client, error) {
	ctx, conn, err := dialCluster(ctx, opts)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to dial cluster")
	}

	if opts.InstanceName != "" {
		ctx = metadata.AppendToOutgoingContext(ctx, "x-resource-instance-name", opts.InstanceName)
	}

	return ctx, &clientImpl{
		GPIOServiceClient: gpiogrpcpb.NewGPIOServiceClient(conn),
		conn:              conn,
	}, nil
}

// makeGPIOClient creates a GPIO client. It is a variable to allow tests
// to override it if needed.
var makeGPIOClient = defaultMakeGPIOClient

var gpioCmd = cobrautil.ParentOfNestedSubcommands("gpio", "Introspect and operate GPIO")

func initGPIOFlags() {
	gpioCmd.ResetFlags()
	flags = cmdutils.NewCmdFlags()
	flags.SetCommand(gpioCmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrgOptional()

	gpioCmd.PersistentFlags().StringVar(&flagInstanceName, "instance_name", "", "name of the GPIO instance to connect to")
	// Retain --resource_instance_name as an alias for --instance_name (deprecated).
	gpioCmd.PersistentFlags().Func("resource_instance_name", "name of the GPIO instance to connect to (deprecated: use --instance_name)", func(val string) error {
		if !gpioCmd.PersistentFlags().Changed("instance_name") {
			return gpioCmd.PersistentFlags().Lookup("instance_name").Value.Set(val)
		}
		return nil
	})
	_ = gpioCmd.PersistentFlags().MarkDeprecated("resource_instance_name", "use --instance_name instead")

	// Retain --server as an alias for --address (deprecated).
	gpioCmd.PersistentFlags().Func("server", "address and port of the GPIO server to contact (deprecated: use --address)", func(val string) error {
		if !gpioCmd.PersistentFlags().Changed("address") {
			return gpioCmd.PersistentFlags().Lookup("address").Value.Set(val)
		}
		return nil
	})
	_ = gpioCmd.PersistentFlags().MarkDeprecated("server", "use --address instead")
}

func init() {
	initGPIOFlags()
	root.RootCmd.AddCommand(gpioCmd)
}
