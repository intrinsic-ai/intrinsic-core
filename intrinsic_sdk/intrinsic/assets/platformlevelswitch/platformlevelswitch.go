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

// Package platformlevelswitch reexports enterprise-only CLI, identity, and authentication utilities for enterprise builds, while defaulting to no-op implementations for IOC core builds.
package platformlevelswitch

import (
	"context"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

// AppendOrgToOutgoingContext appends the org option into outgoing context metadata.
var AppendOrgToOutgoingContext = func(ctx context.Context, org string) (context.Context, error) {
	return ctx, nil
}

// WrapCmd injects project/org flags and sets up required organization verification for cobra commands.
var WrapCmd = func(cmd *cobra.Command, vipr *viper.Viper) *cobra.Command {
	return cmd
}

// WrapCmdOptional injects optional project/org flags and handling into the command.
var WrapCmdOptional = func(cmd *cobra.Command, vipr *viper.Viper) *cobra.Command {
	return cmd
}
