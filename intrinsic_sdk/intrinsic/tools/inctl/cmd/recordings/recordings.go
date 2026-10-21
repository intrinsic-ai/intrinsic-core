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

// Package recordings provides an implementation of the recordings command.
package recordings

import (
	"intrinsic/tools/inctl/cmd/root"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

// Exposed for testing
var (
	checkOrgExists = true
)

// Shared flags across subcommands.
var (
	flagBagID string
)

const (
	keyProjectShort = "p"
)

var RecordingsCmd = &cobra.Command{
	Use:   "recordings",
	Short: "Provides access to recordings for a given workcell.",
	Long:  "Provides access to recordings for a given workcell.",
	// Catching common typos and potential alternatives
	SuggestFor: []string{"recording", "record", "bag"},
}

func init() {
	root.RootCmd.AddCommand(RecordingsCmd)
}

// resolveCluster returns the cluster name, preferring the "cluster" parameter
// and falling back to the deprecated "workcell" parameter.
func resolveCluster(params *viper.Viper) string {
	cluster := params.GetString("cluster")
	if cluster == "" {
		cluster = params.GetString("workcell")
	}
	return cluster
}
