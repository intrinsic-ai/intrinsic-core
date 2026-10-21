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
	"bytes"
	"encoding/base64"
	"fmt"
	"io"
	"strings"
	"time"

	"intrinsic/tools/inctl/util/orgutil"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/timestamppb"

	bmpb "intrinsic/logging/proto/bag_metadata_go_proto"
	pb "intrinsic/logging/proto/bag_packager_service_go_proto"
)

var (
	flagStartTimestamp string
	flagEndTimestamp   string
	flagMaxNumResults  uint32
	flagClusterName    string
	flagWorkcellName   string // Deprecated
	flagCursor         string
)

var bagStatusToString = map[bmpb.BagStatus_BagStatusEnum]string{
	bmpb.BagStatus_UNSET:                   "0: Status unset",
	bmpb.BagStatus_UPLOAD_PENDING:          "1: Upload pending",
	bmpb.BagStatus_UPLOADING:               "2: Uploading...",
	bmpb.BagStatus_UPLOADED:                "3: Uploaded. Recording file generation pending",
	bmpb.BagStatus_UNCOMPLETABLE:           "4: Uploaded. Recording file generation pending with dropped data",
	bmpb.BagStatus_COMPLETED:               "5: Uploaded. Generated recording file",
	bmpb.BagStatus_UNCOMPLETABLE_COMPLETED: "6: Uploaded. Generated recording file with dropped data",
	bmpb.BagStatus_FAILED:                  "7: Failed",
}

// ListCmdRunner manages dependencies for the list command to allow for mocking in tests.
type ListCmdRunner struct {
	NewClient      func(cmd *cobra.Command) (pb.BagPackagerClient, error)
	PromptContinue func(cmd *cobra.Command) (bool, error)
}

// NewListCmd creates a new cobra command for listing recordings.
func NewListCmd(runner *ListCmdRunner) *cobra.Command {
	if runner == nil {
		runner = &ListCmdRunner{
			NewClient: func(cmd *cobra.Command) (pb.BagPackagerClient, error) {
				return newBagPackagerClient(cmd.Context(), listParams)
			},
			PromptContinue: func(cmd *cobra.Command) (bool, error) {
				var input string
				if _, err := fmt.Fscanln(cmd.InOrStdin(), &input); err != nil && err != io.EOF {
					return false, err
				}
				input = strings.ToLower(input)
				return input == "y" || input == "", nil
			},
		}
	}

	listCmd := &cobra.Command{
		Use:     "list",
		Aliases: []string{"ls"},
		Short:   "Lists available recordings for a given cluster",
		Long:    "Lists available recordings for a given cluster",
		Args:    cobra.NoArgs,
		RunE:    runner.RunE,
		Example: `  # List the latest recordings
  inctl recordings list --cluster my-cluster --org my-org

  # List recordings generated since a specific time
  inctl recordings list --cluster my-cluster --org my-org \
    --start_timestamp 2024-08-20T12:00:00Z \
    --end_timestamp 2024-08-20T14:00:00Z`,
	}

	flags := listCmd.Flags()
	flags.StringVar(&flagClusterName, "cluster", "", "The Kubernetes cluster to use.")
	flags.StringVar(&flagWorkcellName, "workcell", "", "The Kubernetes cluster to use. (Deprecated: use --cluster)")
	listCmd.Flags().MarkDeprecated("workcell", "use --cluster instead")
	flags.StringVar(&flagStartTimestamp, "start_timestamp", "", "Start timestamp in RFC3339 format for fetching recordings. eg. 2024-08-20T12:00:00Z")
	flags.StringVar(&flagEndTimestamp, "end_timestamp", "", "End timestamp in RFC3339 format for fetching recordings. eg. 2024-08-20T12:00:00Z")
	flags.Uint32Var(&flagMaxNumResults, "max_num_results", 10, "The maximum number of recordings to list per page.")
	flags.StringVar(&flagCursor, "cursor", "", "Page cursor for pagination.")

	// Bind flags to Viper to support environment variables and handle the deprecated --workcell flag.
	listParams.SetEnvPrefix("intrinsic")
	listParams.BindPFlag("cluster", flags.Lookup("cluster"))
	listParams.BindEnv("cluster")
	listParams.BindPFlag("workcell", flags.Lookup("workcell"))

	return orgutil.WrapCmd(listCmd, listParams, orgutil.WithOrgExistsCheck(func() bool { return checkOrgExists }))
}

func (r *ListCmdRunner) RunE(cmd *cobra.Command, _ []string) error {
	client, err := r.NewClient(cmd)
	if err != nil {
		return err
	}

	cluster := resolveCluster(listParams)
	if cluster == "" {
		return fmt.Errorf("must provide --cluster")
	}

	startTime, endTime, err := parseTimeFlags()
	if err != nil {
		return err
	}

	var req *pb.ListBagsRequest
	if flagCursor != "" {
		cursorBytes, err := base64.StdEncoding.DecodeString(flagCursor)
		if err != nil {
			return errors.Wrap(err, "could not parse cursor")
		}
		req = &pb.ListBagsRequest{
			OrganizationId: listParams.GetString(orgutil.KeyOrganization),
			MaxNumResults:  &flagMaxNumResults,
			Query: &pb.ListBagsRequest_Cursor{
				Cursor: cursorBytes,
			},
		}
	} else {
		req = newListBagsRequest(cluster, startTime, endTime)
	}

	return r.executeAndPaginate(cmd, client, req)
}

func parseTimeFlags() (time.Time, time.Time, error) {
	var startTime, endTime time.Time
	var err error

	if flagStartTimestamp != "" {
		startTime, err = time.Parse(time.RFC3339, flagStartTimestamp)
		if err != nil {
			return time.Time{}, time.Time{}, errors.Wrapf(err, "invalid start timestamp: %s", flagStartTimestamp)
		}
	} else {
		startTime = time.Now().Add(-1000000 * time.Hour)
	}

	if flagEndTimestamp != "" {
		endTime, err = time.Parse(time.RFC3339, flagEndTimestamp)
		if err != nil {
			return time.Time{}, time.Time{}, errors.Wrapf(err, "invalid end timestamp: %s", flagEndTimestamp)
		}
	} else {
		endTime = time.Now()
	}

	return startTime, endTime, nil
}

func newListBagsRequest(cluster string, startTime, endTime time.Time) *pb.ListBagsRequest {
	return &pb.ListBagsRequest{
		OrganizationId: listParams.GetString(orgutil.KeyOrganization),
		MaxNumResults:  &flagMaxNumResults,
		Query: &pb.ListBagsRequest_ListQuery{
			ListQuery: &pb.ListBagsRequest_Query{
				WorkcellName: cluster,
				StartTime:    timestamppb.New(startTime),
				EndTime:      timestamppb.New(endTime),
			},
		},
	}
}

func (r *ListCmdRunner) executeAndPaginate(cmd *cobra.Command, client pb.BagPackagerClient, req *pb.ListBagsRequest) error {
	var nextPageCursor []byte
	var err error
	var numLines uint32

	fail := JSONFailFunc(cmd)
	isJSON := IsJSON(cmd)

	bags, nextPageCursor, err := r.executeListBagsRequest(cmd, client, req)
	if err != nil {
		return fail(err)
	}

	if isJSON {
		// In JSON mode, we emit just the requested page and return without prompting
		var pbBags []proto.Message
		for _, b := range bags {
			pbBags = append(pbBags, b)
		}
		emitJSONSuccess(cmd.OutOrStdout(), map[string]interface{}{
			"bags":             pbBags,
			"next_page_cursor": base64.StdEncoding.EncodeToString(nextPageCursor),
		})
		return nil
	}

	numLines = r.printBags(cmd, bags)

	page := 0
	seenRecordings := numLines
	for len(nextPageCursor) > 0 {
		page++
		fmt.Fprintf(cmd.OutOrStdout(), "\nSeen pages: %d | Seen recordings: %d\n", page, seenRecordings)
		fmt.Fprint(cmd.OutOrStdout(), "\nMore results further into the past are available, continue? [Y/n] ")

		cont, err := r.PromptContinue(cmd)
		if err != nil {
			return err
		}
		if !cont {
			break
		}

		req.Query = &pb.ListBagsRequest_Cursor{Cursor: nextPageCursor}
		bags, nextPageCursor, err = r.executeListBagsRequest(cmd, client, req)
		if err != nil {
			return err
		}
		numLines = r.printBags(cmd, bags)
		seenRecordings += numLines
	}

	fmt.Fprintf(cmd.OutOrStdout(), "\nNum recordings: %d\n", seenRecordings)

	return nil
}

func (r *ListCmdRunner) executeListBagsRequest(cmd *cobra.Command, client pb.BagPackagerClient, req *pb.ListBagsRequest) ([]*pb.BagRecord, []byte, error) {
	resp, err := client.ListBags(cmd.Context(), req)
	if err != nil {
		return nil, nil, err
	}
	return resp.GetBags(), resp.GetNextPageCursor(), nil
}

func (r *ListCmdRunner) printBags(cmd *cobra.Command, bags []*pb.BagRecord) uint32 {
	if len(bags) == 0 {
		fmt.Fprintln(cmd.OutOrStdout(), "No recordings found")
		return 0
	}

	var out bytes.Buffer
	const formatString = "%-22s %-22s %-40s %-55s %-40s"
	fmt.Fprintln(&out, "")
	fmt.Fprintf(&out, formatString, "Start Time", "End Time", "ID", "Status", "Description")
	fmt.Fprintln(&out)

	for _, bag := range bags {
		description := bag.GetBagMetadata().GetDescription()
		if description == "" {
			description = "<NO-DESCRIPTION>"
		}
		status := bag.GetBagMetadata().GetStatus().GetStatus()
		statusString, ok := bagStatusToString[status]
		if !ok {
			statusString = status.String()
		}
		fmt.Fprintf(&out, formatString,
			bag.GetBagMetadata().GetStartTime().AsTime().Format(time.RFC3339),
			bag.GetBagMetadata().GetEndTime().AsTime().Format(time.RFC3339),
			bag.GetBagMetadata().GetBagId(),
			statusString,
			description,
		)
		fmt.Fprintln(&out)
	}

	fmt.Fprint(cmd.OutOrStdout(), out.String())
	return uint32(len(bags))
}

var listParams = viper.New()

func init() {
	listCmd := NewListCmd(nil)
	RecordingsCmd.AddCommand(listCmd)
}
