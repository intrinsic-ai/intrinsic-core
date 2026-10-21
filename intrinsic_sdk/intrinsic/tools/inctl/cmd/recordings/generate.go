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

package recordings

import (
	"fmt"
	"io"
	"math/rand"
	"strings"
	"time"

	"intrinsic/tools/inctl/util"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	pb "intrinsic/logging/proto/bag_packager_service_go_proto"
)

// Number of times to status-check the recording generation after encountering a 504 timeout error
// on the initial GenerateBag call.
//
// Timeouts are from nginx, not client-side, as deadlines are infinite by default for gRPC clients:
// https://grpc.io/docs/guides/deadlines/#deadlines-on-the-client
//
// This is needed because the GenerateBag call can take a long time to complete.
const (
	maxPostTimeoutRetries = 10
	postTimeoutRetryDelay = 30 * time.Second
)

const (
	numBytesInMB           = 1024 * 1024
	largeRecordingByteSize = 50 * numBytesInMB
)

// GenerateCmdRunner manages dependencies for the generate command to allow for mocking in tests.
type GenerateCmdRunner struct {
	NewClient func(cmd *cobra.Command) (pb.BagPackagerClient, error)
}

// NewGenerateCmd creates a new cobra command for generating recordings.
func NewGenerateCmd(runner *GenerateCmdRunner) *cobra.Command {
	if runner == nil {
		runner = &GenerateCmdRunner{
			NewClient: func(cmd *cobra.Command) (pb.BagPackagerClient, error) {
				return newBagPackagerClient(cmd.Context(), generateParam)
			},
		}
	}

	generateCmd := &cobra.Command{
		Use:   "generate",
		Short: "Generates an Intrinsic recording file for a given recording id",
		Long:  "Generates an Intrinsic recording file for a given recording id",
		Args:  cobra.NoArgs,
		RunE:  runner.RunE,
	}

	flags := generateCmd.Flags()
	flags.StringVar(&flagBagID, "recording_id", "", "The recording id to generate Intrinsic recording file for.")
	generateCmd.MarkFlagRequired("recording_id")

	return orgutil.WrapCmd(generateCmd, generateParam, orgutil.WithOrgExistsCheck(func() bool { return checkOrgExists }))
}

func (r *GenerateCmdRunner) RunE(cmd *cobra.Command, _ []string) error {
	out := cmd.OutOrStdout()
	if IsJSON(cmd) {
		out = io.Discard
	}
	fail := JSONFailFunc(cmd)

	client, err := r.NewClient(cmd)
	if err != nil {
		return fail(err)
	}

	// Fetch to validate.
	getReq := &pb.GetBagRequest{
		BagId:         flagBagID,
		WithSignedUrl: false,
	}
	getResp, err := client.GetBag(cmd.Context(), getReq)
	if err != nil {
		if strings.Contains(err.Error(), "does not exist") {
			return fail(fmt.Errorf("recording with id \"%s\" does not exist", flagBagID))
		}
		return fail(err)
	}
	if getResp.GetBag().GetBagFile() != nil {
		return fail(fmt.Errorf("recording with id \"%s\" is already generated", flagBagID))
	}

	// Generate.
	recordingByteSize := getResp.GetBag().GetBagMetadata().GetTotalBytes()
	if recordingByteSize > largeRecordingByteSize {
		fmt.Fprintln(out, "")
		fmt.Fprintln(out, "WARNING:")
		fmt.Fprintln(out, fmt.Sprintf("  Recording with id \"%s\" is large (%d MB) and might take several minutes (usually up to ~15 minutes) to generate...", flagBagID, recordingByteSize/numBytesInMB))
		fmt.Fprintln(out, "  Please wait and do NOT close this terminal or attempt to generate the recording again, the server will continue processing the request.")
		fmt.Fprintln(out, "")
	}

	msg := fmt.Sprintf("Starting generation of recording with id \"%s\"...", flagBagID)
	spinner := util.NewSpinner(cmd.Context(), out, 100*time.Millisecond, util.PositionFront, util.StyleDotsConstruct, util.ColorRGB, util.DirectionForward)
	spinner.Start(msg)

	generateReq := &pb.GenerateBagRequest{
		Query: &pb.GenerateBagRequest_BagId{
			BagId: flagBagID,
		},
		OrganizationId: generateParam.GetString(orgutil.KeyOrganization),
	}
	_, err = client.GenerateBag(cmd.Context(), generateReq)
	if err != nil {
		// A server timeout is expected if the recording is large, this is usually not an error.
		//
		// It usually means that the server is still processing the request, so we should GetBag until
		// we see the file or timeout.
		if !strings.Contains(err.Error(), "504") {
			spinner.Stop("")
			return fail(err)
		}

		for i := 0; i < maxPostTimeoutRetries; i++ {
			spinner.UpdateMessage(fmt.Sprintf("Still generating%s", strings.Repeat(".", i)))

			getResp, err := client.GetBag(cmd.Context(), getReq)
			if err != nil {
				spinner.Interrupt(fmt.Sprintf("Failed to get recording with id \"%s\" to check generation status, server might still be processing: %v", flagBagID, err))
			}

			if getResp.GetBag().GetBagFile() != nil {
				break
			}

			if i == maxPostTimeoutRetries-1 {
				spinner.Stop("")
				return fail(fmt.Errorf("failed to generate recording with id \"%s\" after %d retries, try generating again or waiting longer for the recording to be generated", flagBagID, maxPostTimeoutRetries))
			}
			time.Sleep(postTimeoutRetryDelay + time.Duration(rand.Float32()*5.0)*time.Second)
		}
	}

	spinner.Stop(msg)

	if IsJSON(cmd) {
		payload := map[string]interface{}{
			"recording_id": flagBagID,
		}
		if getResp != nil && getResp.GetBag() != nil {
			payload["bag"] = getResp.GetBag()
		}
		emitJSONSuccess(cmd.OutOrStdout(), payload)
		return nil
	}

	fmt.Fprintln(out, "")
	fmt.Fprintln(out, fmt.Sprintf("Generated recording file for recording ID %s", flagBagID))
	fmt.Fprintln(out, "")
	color.C.Blue().Fprintf(out, "Next steps:\n")
	color.C.Blue().Fprintf(out, "  Download the recording:\n")
	color.C.Blue().Fprintf(out, fmt.Sprintf("    inctl recordings get --recording_id %s --with_signed_url --org %s@%s\n", flagBagID, generateParam.GetString(orgutil.KeyOrganization), generateParam.GetString(orgutil.KeyProject)))
	color.C.Blue().Fprintf(out, "\n")
	color.C.Blue().Fprintf(out, "  Visualize the recording in your browser:\n")
	color.C.Blue().Fprintf(out, fmt.Sprintf("    inctl recordings visualize create --recording_id %s --duration 1h --org %s@%s\n", flagBagID, generateParam.GetString(orgutil.KeyOrganization), generateParam.GetString(orgutil.KeyProject)))
	fmt.Fprintln(out, "")
	return nil
}

var generateParam = viper.New()

func init() {
	RecordingsCmd.AddCommand(NewGenerateCmd(nil))
}
