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

// Package version contains all commands related to versions
package version

import (
	"errors"
	"fmt"
	"io"
	"os"
	"strings"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/util/go/build" 

	"github.com/spf13/cobra"
)

const (
	// SDKVersionDefaultValue is a special value for flagSDKVersion below.
	SDKVersionDefaultValue = "unknown"
	// DevContainerVersionFilePath is the path to the dev container version file
	// which will only be present in the context of a dev container.
	devContainerVersionFilePath = "/etc/intrinsic/sdk.version"
)

var (
	// SDKVersion is the version of the used Intrinsic SDK
	// It can be changed by stamping at build time as follows:
	//
	//   Externally with Bazel (enabled by go_library.x_defs):
	//     bazel build/run
	//       --stamp
	//       --workspace_status_command="echo STABLE_SDK_VERSION intrinsic.platform.20221231.RC00"
	//       ...
	//   See https://github.com/bazelbuild/rules_go/blob/master/docs/go/core/defines_and_stamping.md#defines-and-stamping.
	SDKVersion string = SDKVersionDefaultValue


	// Overwritable pointer to build.Label() for testing.
	buildLabel = build.Label

)

type versionInfo struct {
	InctlSDKVersion     string `json:"inctlVersion,omitempty"`
	DevContainerVersion string `json:"devContainerVersion,omitempty"`
}

// String prints the versionMessage in the case of --output=text.
func (msg *versionInfo) String() string {
	var result strings.Builder
	result.WriteString(fmt.Sprintf("Inctl version: %s", msg.InctlSDKVersion))

	if msg.DevContainerVersion != "" {
		result.WriteByte('\n')
		result.WriteString(fmt.Sprintf("Dev container version: %s", msg.DevContainerVersion))
	}

	return result.String()
}

type cmdParams struct {
	flagOutput                  string
	devContainerVersionFilePath string
}

// runVersionCmd implements the version command. It is the entry-point for
// unit tests and does not rely on any global state (e.g. global flag
// variables).
func runVersionCmd(params *cmdParams, stdout io.Writer) error {
	prtr, err := printer.NewPrinterWithWriter(params.flagOutput, stdout)
	if err != nil {
		return fmt.Errorf("creating printer: %w", err)
	}

	devContainerVersion := ""
	devContainerVersionBytes, err := os.ReadFile(params.devContainerVersionFilePath)
	if err == nil {
		devContainerVersion = string(devContainerVersionBytes)
	} else if !errors.Is(err, os.ErrNotExist) {
		// We recognized that the user is likely in a dev container (version file exists) but
		// cannot determine the version.
		devContainerVersion = "cannot determine"
	}

	inctlSDKVersion := SDKVersion


	// TODO(b/336721813): We can always use the embedded build label
	// instead of stamping our own variable manually (`SDKVersion`).
	// This requires to synchronize the release workflow with updating this code.
	if inctlSDKVersion == SDKVersionDefaultValue && buildLabel() != "" {
		inctlSDKVersion = buildLabel()
	}


	prtr.Print(&versionInfo{
		InctlSDKVersion:     inctlSDKVersion,
		DevContainerVersion: devContainerVersion,
	})

	return nil
}

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "Displays Intrinsic SDK version",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		cmdParams := &cmdParams{
			flagOutput:                  root.FlagOutput,
			devContainerVersionFilePath: devContainerVersionFilePath,
		}
		return runVersionCmd(cmdParams, cmd.OutOrStdout())
	},
}

func init() {
	root.RootCmd.AddCommand(versionCmd)
}
