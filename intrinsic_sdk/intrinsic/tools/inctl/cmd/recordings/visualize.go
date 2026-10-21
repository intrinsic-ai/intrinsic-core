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
	"context"
	"fmt"
	"io"
	"regexp"
	"time"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/pborman/uuid"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	leaseapigrpcpb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	leasepb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	recordingdatacategories "intrinsic/logging/proto"
	bagpackagerpb "intrinsic/logging/proto/bag_packager_service_go_proto"
	replaygrpcpb "intrinsic/logging/proto/replay_service_go_proto"
	replaypb "intrinsic/logging/proto/replay_service_go_proto"
	replayoptionspb "intrinsic/logging/proto/replay_service_options_go_proto"

	tpb "google.golang.org/protobuf/types/known/timestamppb"
)

var visualizeCmd = cobrautil.ParentOfNestedSubcommands("visualize", "Visualize Intrinsic recordings")

const (
	serviceTag         string = "inctl"
	leaseRetryInterval        = 10 * time.Second
)

var (
	flagRecordingID string
	flagDuration    string
)

type visualizationTarget interface {
	GetClusterName() string
	PrintSuccessOutput(out io.Writer, recordingID string)
	GetJSONMap(url string) map[string]interface{}
}

type leasedVisualizationTarget struct {
	ClusterName string
	Expires     time.Time
}

func (t *leasedVisualizationTarget) GetClusterName() string { return t.ClusterName }
func (t *leasedVisualizationTarget) PrintSuccessOutput(out io.Writer, recordingID string) {
	fmt.Fprintf(out, "Visualization created successfully for recording %s\n", recordingID)
	fmt.Fprintf(out, "- Visualization valid for %s, expires at %s\n", time.Until(t.Expires).Round(time.Second), t.Expires.Format(time.RFC3339))
}
func (t *leasedVisualizationTarget) GetJSONMap(url string) map[string]interface{} {
	return map[string]interface{}{
		"url":        url,
		"expires_at": t.Expires.Format(time.RFC3339),
	}
}


type clusterVisualizationTarget struct {
	ClusterName string
}

func (t *clusterVisualizationTarget) GetClusterName() string { return t.ClusterName }
func (t *clusterVisualizationTarget) PrintSuccessOutput(out io.Writer, recordingID string) {
	fmt.Fprintf(out, "Visualization created successfully for recording %s\n", recordingID)
	fmt.Fprintf(out, "- Visualization is hosted on cluster %s\n", t.ClusterName)
}
func (t *clusterVisualizationTarget) GetJSONMap(url string) map[string]interface{} {
	return map[string]interface{}{
		"url":     url,
		"cluster": t.ClusterName,
	}
}



// VisualizeCmdRunner contains the business logic for the visualize command.
// It is a separate struct to allow for dependency injection and easier testing.
type VisualizeCmdRunner struct {
	// A factory function to create a ReplayClient.
	// This is a factory because the client can only be created after a VM is leased.
	ReplayClientFactory  func(ctx context.Context, v *viper.Viper, clusterName string) (replaygrpcpb.ReplayClient, io.Closer, error)
	NewLeaseClient       func(cmd *cobra.Command) (leaseapigrpcpb.VMPoolLeaseServiceClient, error)
	NewBagPackagerClient func(cmd *cobra.Command) (bagpackagerpb.BagPackagerClient, error)
}

func (r *VisualizeCmdRunner) leaseVM(cmd *cobra.Command, v *viper.Viper, duration time.Duration, leaseClient leaseapigrpcpb.VMPoolLeaseServiceClient) (*leasepb.Lease, error) {
	out := cmd.OutOrStdout()
	if IsJSON(cmd) {
		out = io.Discard
	}
	spinner := util.NewSpinner(cmd.Context(), out, 100*time.Millisecond, util.PositionFront, util.StyleArc, util.ColorRGB, util.DirectionForward)
	spinner.Start("Leasing visualization host...")

	reservationUUID := uuid.New()
	for { // retry until lease successful.
		expires := time.Now().Add(duration)
		leaseReq := &leasepb.LeaseRequest{Pool: "", Expires: tpb.New(expires), ServiceTag: serviceTag}
		leaseReq.ReservationId = &reservationUUID

		leaseResp, err := leaseClient.Lease(cmd.Context(), leaseReq)
		if err != nil {
			if status.Code(err) == codes.PermissionDenied {
				spinner.Stop("")
				return nil, fmt.Errorf("visualization host create request failed: %v\n. Your api-key might have expired, run `inctl auth login` to refresh it and retry", err)
			}
			spinner.Interrupt(fmt.Sprintf("visualization host create request failed, retrying soon: %v\n", err))
			time.Sleep(leaseRetryInterval)
			continue
		}

		lease := leaseResp.GetLease()
		spinner.Stop(fmt.Sprintf("Visualization host started successfully: %s\n", lease.GetInstance()))
		return lease, nil
	}
}

// buildDenylist generates a list of exact-match regexes to pass to the visualization
// backend based on the excluded categories provided as CLI flags.
//
// It resolves overlapping category rules by prioritizing inclusion over exclusion.
// E.g., if a topic matches both an excluded category (like "debug_data": "executive.*")
// and an included category (like "skill_data": "executive.extended_status"),
// the topic will NOT be excluded.
func buildDenylist(cmd *cobra.Command, eventSources []string) []string {
	cats := recordingdatacategories.GetRecordingDataCategories()
	var excludedCategories, includedCategories []string
	for _, cat := range cats.GetCategories() {
		if b, err := cmd.Flags().GetBool("exclude_" + cat.GetCategoryId()); err == nil && b {
			excludedCategories = append(excludedCategories, cat.GetCategoryId())
		} else {
			includedCategories = append(includedCategories, cat.GetCategoryId())
		}
	}

	// Build compiled regexes for excluded and included categories.
	excludedRegexes := make([]*regexp.Regexp, 0)
	includedRegexes := make([]*regexp.Regexp, 0)

	for _, cat := range cats.GetCategories() {
		isExcluded := false
		for _, excl := range excludedCategories {
			if cat.GetCategoryId() == excl {
				isExcluded = true
				break
			}
		}

		for _, srcRegexStr := range cat.GetEventSources() {
			// RE2 is compatible with Go's regexp package.
			re, err := regexp.Compile(srcRegexStr)
			if err != nil {
				// Ignore invalid regexes from the config just in case.
				continue
			}
			if isExcluded {
				excludedRegexes = append(excludedRegexes, re)
			} else {
				includedRegexes = append(includedRegexes, re)
			}
		}
	}

	var denylist []string
	for _, topic := range eventSources {
		matchesExcluded := false
		for _, re := range excludedRegexes {
			if re.MatchString(topic) {
				matchesExcluded = true
				break
			}
		}

		if matchesExcluded {
			matchesIncluded := false
			for _, re := range includedRegexes {
				if re.MatchString(topic) {
					matchesIncluded = true
					break
				}
			}

			// If it matches an excluded category but NO included categories, we must exclude it.
			if !matchesIncluded {
				// We append the EXACT string anchored to ensure it only matches this specific topic
				// in the backend's RE2 filter.
				exactRegex := fmt.Sprintf("^%s$", regexp.QuoteMeta(topic))
				denylist = append(denylist, exactRegex)
			}
		}
	}
	return denylist
}

func (r *VisualizeCmdRunner) createLeasedTarget(cmd *cobra.Command, v *viper.Viper, fail func(error) error) (*leasedVisualizationTarget, error) {
	duration, err := time.ParseDuration(flagDuration)
	if err != nil {
		return nil, fail(fmt.Errorf("Duration '%v' entered is not valid, use something like '30m' or '1h': %v", flagDuration, err))
	}

	leaseClient, err := r.NewLeaseClient(cmd)
	if err != nil {
		return nil, fail(fmt.Errorf("could not create visualization host client: %v", err))
	}

	lease, err := r.leaseVM(cmd, v, duration, leaseClient)
	if err != nil {
		return nil, fail(fmt.Errorf("visualization host creation failed: %v", err))
	}
	return &leasedVisualizationTarget{
		ClusterName: lease.GetInstance(),
		Expires:     lease.GetExpires().AsTime(),
	}, nil
}

// RunE executes the visualize command.
func (r *VisualizeCmdRunner) RunE(cmd *cobra.Command, _ []string) error {
	v := visualizeCreateParam
	out := cmd.OutOrStdout()

	if IsJSON(cmd) {
		out = io.Discard
	}
	fail := JSONFailFunc(cmd)


	var target visualizationTarget
	var err error

	if flagClusterName == "" && flagDuration == "" {
		return fail(fmt.Errorf("required flag(s) \"duration\" or \"cluster\" must be set"))
	}
	if flagClusterName != "" {
		target = &clusterVisualizationTarget{ClusterName: flagClusterName}
	} else {
		target, err = r.createLeasedTarget(cmd, v, fail)
		if err != nil {
			return err
		}
	}






	bagPackagerClient, err := r.NewBagPackagerClient(cmd)
	if err != nil {
		return fail(fmt.Errorf("could not create bag packager client: %v", err))
	}

	bagResp, err := bagPackagerClient.GetBag(cmd.Context(), &bagpackagerpb.GetBagRequest{BagId: flagRecordingID})
	if err != nil {
		return fail(fmt.Errorf("failed to get recording metadata: %v", err))
	}

	var eventSources []string
	for _, source := range bagResp.GetBag().GetBagMetadata().GetEventSources() {
		eventSources = append(eventSources, source.GetEventSourceWithTypeHints().GetEventSource())
	}
	denylist := buildDenylist(cmd, eventSources)

	var visualizationOpts *replayoptionspb.VisualizationOptions
	if len(denylist) > 0 {
		visualizationOpts = &replayoptionspb.VisualizationOptions{
			DefaultVisualizerFilters: &replayoptionspb.FilterOptions{
				EventSources: &replayoptionspb.FilterOptions_RegexFilterOptions{
					DenylistRegexes: denylist,
				},
			},
		}
	}

	replayClient, closer, err := r.ReplayClientFactory(cmd.Context(), v, target.GetClusterName())
	if err != nil {
		return fail(err)
	}
	if closer != nil {
		defer closer.Close()
	}

	req := &replaypb.VisualizeRecordingRequest{
		RecordingId:          flagRecordingID,
		VisualizationOptions: visualizationOpts,
		// TODO(methyldragon) - Load and add options.
	}

	spinner := util.NewSpinner(cmd.Context(), out, 100*time.Millisecond, util.PositionFront, util.StyleWaveThrough, util.ColorRGB, util.DirectionForward)
	spinner.Start("Loading visualization onto host...")

	resp, err := replayClient.VisualizeRecording(cmd.Context(), req)
	if err != nil {
		spinner.Stop("")
		if status.Code(err) == codes.AlreadyExists {
			return fail(fmt.Errorf("%w", err))
		}
		return fail(fmt.Errorf("failed to visualize recording, did you generate it first with `inctl recordings generate`? Error: %w", err))
	}

	if IsJSON(cmd) {
		emitJSONSuccess(cmd.OutOrStdout(), target.GetJSONMap(resp.GetUrl()))
		return nil
	}
	spinner.Stop("Visualization loaded successfully")

	fmt.Fprintln(out, "")
	target.PrintSuccessOutput(out, flagRecordingID)
	fmt.Fprintln(out, "")
	fmt.Fprintln(out, "Data will load into the visualization over the next few minutes. You will know it is done when data stops appearing in the timeline.")
	color.C.BlueBackground().White().Fprintf(out, "\nLink to visualization: %s", resp.GetUrl())
	fmt.Fprintln(out, "")

	return nil
}

var visualizeCreateParam = viper.New()

// NewVisualizeCmd creates a new cobra command for the visualize create command.
func NewVisualizeCmd(runner *VisualizeCmdRunner) *cobra.Command {
	if runner == nil {
		runner = &VisualizeCmdRunner{
			ReplayClientFactory: func(ctx context.Context, v *viper.Viper, clusterName string) (replaygrpcpb.ReplayClient, io.Closer, error) {
				conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(v), auth.WithCluster(clusterName))
				if err != nil {
					return nil, nil, err
				}
				return replaygrpcpb.NewReplayClient(conn), conn, nil
			},
			NewLeaseClient: func(cmd *cobra.Command) (leaseapigrpcpb.VMPoolLeaseServiceClient, error) {
				conn, err := auth.NewCloudConnection(cmd.Context(), auth.WithFlagValues(visualizeCreateParam))
				if err != nil {
					return nil, err
				}
				return leaseapigrpcpb.NewVMPoolLeaseServiceClient(conn), nil
			},
			NewBagPackagerClient: func(cmd *cobra.Command) (bagpackagerpb.BagPackagerClient, error) {
				return newBagPackagerClient(cmd.Context(), visualizeCreateParam)
			},
		}
	}
	cmd := &cobra.Command{
		Use:   "create",
		Short: "Creates a visualization of a recording in a hosted version of Rerun.io",
		Long:  "Creates a visualization of a recording in a hosted version of Rerun.io",
		Args:  cobra.NoArgs,
		RunE:  runner.RunE,
	}
	// TODO(methyldragon) - Add optional flags for:
	//   - options .pbtxt filepath (for options)
	cmd.Flags().StringVar(&flagRecordingID, "recording_id", "", "The recording id to visualize.")
	cmd.Flags().StringVarP(&flagDuration, "duration", "d", "", "Desired duration for the visualization to be accessible.")

	cats := recordingdatacategories.GetRecordingDataCategories()
	for _, cat := range cats.GetCategories() {
		desc := fmt.Sprintf("Exclude %s. %s", cat.GetDisplayName(), cat.GetDescription())
		cmd.Flags().Bool("exclude_"+cat.GetCategoryId(), false, desc)
	}

	cmd.MarkFlagRequired("recording_id")


	// Note:
	//   We use a cluster flag instead of the `AddFlagsAddressClusterSolution` since visualizations are tied to the
	//   VM leasing flow as opposed to potentially solutions.
	cmd.Flags().StringVar(&flagClusterName, "cluster", "", "Optional already leased cluster name to use. IMPORTANT: the cluster must not already be hosting a recording, this command might fail silently, or worse, cause double the data to be loaded.")
	cmd.MarkFlagsMutuallyExclusive("cluster", "duration")



	return orgutil.WrapCmd(cmd, visualizeCreateParam, orgutil.WithOrgExistsCheck(func() bool { return checkOrgExists }))
}

var visualizeCreateCmd = NewVisualizeCmd(nil)

func init() {
	RecordingsCmd.AddCommand(visualizeCmd)
	visualizeCmd.AddCommand(visualizeCreateCmd)

	// TODO(methyldragon) - Add ability to stop and list visualizations once we can support visualizations as a cloud concept.
	//
	// Until then, create is the only command. It might seem redundant, but it will allow us to avoid
	// having users change what command they call down the line.
}
