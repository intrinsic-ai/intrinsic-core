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
	"strings"

	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/protobuf/encoding/prototext"

	pb "intrinsic/logging/proto/bag_packager_service_go_proto"
)

var flagURL bool

// GetCmdRunner is a struct that holds the dependencies for the get command.
// This is used to inject a mock client for testing.
type GetCmdRunner struct {
	NewClient func(cmd *cobra.Command) (pb.BagPackagerClient, error)
}

func (r *GetCmdRunner) RunE(cmd *cobra.Command, _ []string) error {
	fail := JSONFailFunc(cmd)

	client, err := r.NewClient(cmd)
	if err != nil {
		return fail(err)
	}
	req := &pb.GetBagRequest{
		BagId:         flagBagID,
		WithSignedUrl: flagURL,
	}
	resp, err := client.GetBag(cmd.Context(), req)
	if err != nil {
		if strings.Contains(err.Error(), "file does not exist") {
			return fail(fmt.Errorf("download URL requested for recording with id %q, but file is not generated yet, please generate it first with `inctl recordings generate`", flagBagID))
		}
		if strings.Contains(err.Error(), "failed to get bag record") {
			return fail(fmt.Errorf("recording with id %q does not exist", flagBagID))
		}
		return fail(err)
	}

	if IsJSON(cmd) {
		emitJSONSuccess(cmd.OutOrStdout(), resp)
		return nil
	}

	fmt.Fprint(cmd.OutOrStdout(), prototext.Format(resp))
	return nil
}

var getParams = viper.New()

func NewGetCmd(runner *GetCmdRunner) *cobra.Command {
	if runner == nil {
		runner = &GetCmdRunner{
			NewClient: func(cmd *cobra.Command) (pb.BagPackagerClient, error) {
				return newBagPackagerClient(cmd.Context(), getParams)
			},
		}
	}
	cmd := &cobra.Command{
		Use:   "get",
		Short: "Gets a ROS bag for a given recording id",
		Long:  "Gets a ROS bag for a given recording id",
		Args:  cobra.NoArgs,
		RunE:  runner.RunE,
	}
	flags := cmd.Flags()
	flags.StringVar(&flagBagID, "recording_id", "", "The recording id to get ROS bag for.")
	flags.BoolVar(&flagURL, "with_signed_url", false, "If present, generates a signed url to download the bag with.")
	cmd.MarkFlagRequired("recording_id")

	return orgutil.WrapCmd(cmd, getParams, orgutil.WithOrgExistsCheck(func() bool { return checkOrgExists }))
}

var GetCmd = NewGetCmd(nil)

func init() {
	RecordingsCmd.AddCommand(GetCmd)
}
