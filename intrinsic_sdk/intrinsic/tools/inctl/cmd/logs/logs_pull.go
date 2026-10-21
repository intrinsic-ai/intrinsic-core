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

package logs

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path"
	"strings"
	"time"

	inctlauth "intrinsic/tools/inctl/auth/auth"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"
	"google.golang.org/protobuf/types/known/timestamppb"

	"intrinsic/assets/cmdutils"
	tfpb "intrinsic/logging/textlogfetcher/proto/v1/textlogfetcher_go_proto"
	"intrinsic/tools/inctl/util/orgutil"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

const (
	keyOutputDir       = "output_dir"
	keyWorkcell        = "workcell"
	keyFilters         = "filters"
	keyInstance        = "instance"
	keyWithOpName      = "operation"
	orgHeader          = "organization-id"
	keyTypeResource    = "resource"
	keyStartTime       = "start_time"
	keyEndTime         = "end_time"
	maxFailures        = 3
	keyMaxWaitTime     = "max_wait_time"
	keyOutput          = "output"
	defaultMaxWaitTime = 15 * time.Minute
)

func newClient(ctx context.Context) (tfpb.TextLogFetcherClient, *grpc.ClientConn, error) {
	// Use NewCloudConnection which handles DNS, TLS, and authentication correctly for inctl.
	conn, err := inctlauth.NewCloudConnection(ctx, inctlauth.WithFlagValues(localViper))
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to create cloud connection")
	}
	return tfpb.NewTextLogFetcherClient(conn), conn, nil
}

type pullRequestParams struct {
	skillIDs      []string
	resourceNames []string
	instanceNames []string
	workcell      string
	org           string
	outputDir     string
	startTime     *timestamppb.Timestamp
	endTime       *timestamppb.Timestamp
	withOpName    string
	filters       []string
	maxWaitTime   time.Duration
}

type jsonOutput struct {
	Status        string `json:"status"`
	Message       string `json:"message,omitempty"`
	OperationName string `json:"operation_name,omitempty"`
	DownloadPath  string `json:"download_path,omitempty"`
}

// validatePullFlags validates the pull command flags.
func validatePullFlags(in *pullRequestParams) (*pullRequestParams, error) {
	skillIDs := in.skillIDs
	resourceNames := in.resourceNames
	instanceNames := in.instanceNames
	nonOpNameFlagSpecified := false
	withOpName := in.withOpName

	if len(skillIDs) == 0 && len(resourceNames) == 0 && len(instanceNames) == 0 && withOpName == "" {
		return nil, errors.New("at least one target (--skill, --resource, or --instance) must be specified")
	}

	if len(skillIDs) > 0 || len(resourceNames) > 0 || len(instanceNames) > 0 || in.startTime != nil || in.endTime != nil {
		nonOpNameFlagSpecified = true
	}

	workcell := in.workcell
	if workcell == "" && withOpName == "" {
		return nil, errors.New("--workcell must be specified")
	}

	org := in.org
	if org == "" {
		return nil, errors.New("--org must be specified")
	}
	if strings.Contains(org, "@") {
		org = strings.Split(org, "@")[0]
	}

	outputDir := in.outputDir
	if outputDir == "" {
		return nil, errors.New("--output_dir must be specified")
	}

	if (in.startTime != nil && in.endTime == nil) || (in.startTime == nil && in.endTime != nil) {
		return nil, errors.New("both --start_time and --end_time must be specified together")
	}

	if withOpName != "" && nonOpNameFlagSpecified {
		return nil, errors.New("--operation cannot be used with other flags")
	}

	return &pullRequestParams{
		skillIDs:      skillIDs,
		resourceNames: resourceNames,
		instanceNames: instanceNames,
		workcell:      workcell,
		org:           org,
		outputDir:     outputDir,
		startTime:     in.startTime,
		endTime:       in.endTime,
		withOpName:    withOpName,
		filters:       in.filters,
		maxWaitTime:   in.maxWaitTime,
	}, nil
}

var fetchCmd = &cobra.Command{
	Use:   "pull",
	Short: "Fetches and downloads text logs for specified assets and time range.",
	Long: `Fetches and downloads text logs for the given assets and time range into a single archive for download.

To specify multiple assets, you can use a comma-separated list or repeat the flag:
  --skill "skill1,skill2"
  --skill skill1 --skill skill2
  --resource resource1 --resource resource2
  --instance resource1/instance1 --instance resource2/instance2

Example:
  inctl logs pull --skill my-skill --workcell my-workcell --output_dir /tmp/logs
  inctl logs pull --resource my-resource --workcell my-workcell --start_time 2025-03-09T12:00:00Z --end_time 2025-03-16T12:00:00Z --output_dir /tmp/logs
  inctl logs pull --resource my-resource --instance my-resource/instance-1 --workcell my-workcell --output_dir /tmp/logs
	inctl logs pull --resource my-resource --instance my-resource/instance-1 --workcell my-workcell --output_dir /tmp/logs --filters "hello" "world"
  inctl logs pull --operation=<previous-operation-name> --output_dir /tmp/logs`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		isJSON := pullCmdFlags.GetString(keyOutput) == "json"

		fail := func(err error) error {
			if isJSON {
				output := jsonOutput{
					Status:  "error",
					Message: err.Error(),
				}
				enc := json.NewEncoder(cmd.OutOrStdout())
				enc.SetIndent("", "  ")
				enc.Encode(output)
				os.Exit(1)
			}
			return err
		}

		var startTime, endTime *timestamppb.Timestamp
		startTimeStr := pullCmdFlags.GetString(keyStartTime)
		endTimeStr := pullCmdFlags.GetString(keyEndTime)
		if startTimeStr != "" {
			t, err := time.Parse(time.RFC3339, startTimeStr)
			if err != nil {
				return fail(errors.Wrapf(err, "invalid --start_time value: %s", startTimeStr))
			}
			startTime = timestamppb.New(t)
		}

		if endTimeStr != "" {
			t, err := time.Parse(time.RFC3339, endTimeStr)
			if err != nil {
				return fail(errors.Wrapf(err, "invalid --end_time value: %s", endTimeStr))
			}
			endTime = timestamppb.New(t)
		}

		wt, err := time.ParseDuration(pullCmdFlags.GetString(keyMaxWaitTime))
		if err != nil {
			return fail(errors.Errorf("invalid --max_wait_time value: %s", pullCmdFlags.GetString(keyMaxWaitTime)))
		}
		if wt < 0 {
			return fail(errors.Errorf("--max_wait_time must be non-negative"))
		}

		in := &pullRequestParams{
			skillIDs:      pullCmdFlags.GetStringSlice(keyTypeSkill),
			resourceNames: pullCmdFlags.GetStringSlice(keyTypeResource),
			instanceNames: pullCmdFlags.GetStringSlice(keyInstance),
			workcell:      pullCmdFlags.GetString(keyWorkcell),
			org:           cmdFlags.GetString(orgutil.KeyOrganization),
			outputDir:     pullCmdFlags.GetString(keyOutputDir),
			startTime:     startTime,
			endTime:       endTime,
			withOpName:    pullCmdFlags.GetString(keyWithOpName),
			filters:       pullCmdFlags.GetStringSlice(keyFilters),
			maxWaitTime:   wt,
		}

		params, err := validatePullFlags(in)
		if err != nil {
			return fail(err)
		}

		fileInfo, err := os.Stat(params.outputDir)
		if err != nil {
			return fail(errors.Errorf("%s specified cannot be found: %s", params.outputDir, err.Error()))
		}
		if !fileInfo.IsDir() {
			return fail(errors.Errorf("%s is not a directory", params.outputDir))
		}

		client, conn, err := newClient(ctx)
		if err != nil {
			return fail(errors.Wrap(err, "failed to create connection"))
		}
		defer conn.Close()

		assetRefs, err := buildAssetReferences(params.skillIDs, params.resourceNames, params.instanceNames)
		if err != nil {
			return fail(err)
		}

		ctx = metadata.AppendToOutgoingContext(ctx, orgHeader, params.org)
		var opName string
		if params.withOpName == "" {
			req := &tfpb.AssetLogsQueryRequest{
				AssetReferences: assetRefs,
				StartTime:       params.startTime,
				EndTime:         params.endTime,
				WorkcellName:    params.workcell,
				Filters:         params.filters,
			}

			op, err := client.CreateAssetLogsPackage(ctx, req)
			if err != nil {
				return fail(errors.Wrap(err, "failed to initiate log packaging"))
			}

			printIfNotJSON(cmd.OutOrStdout(), isJSON, "Operation started: %s\n", op.GetName())
			opName = op.GetName()
		} else {
			opName = params.withOpName
		}

		printIfNotJSON(cmd.OutOrStdout(), isJSON, "Waiting for operation %s to complete...\n", opName)
		var op *lropb.Operation
		failureCount := 0
		waitStartTime := time.Now()

		for {
			if time.Since(waitStartTime) > params.maxWaitTime {
				return fail(errors.Errorf("spent %s waiting for operation %s to complete.\n Run with --operation=%s to reinitialize waiting", waitStartTime, opName, opName))
			}
			var err error
			op, err = client.GetOperation(ctx, &lropb.GetOperationRequest{Name: opName})
			if err != nil {
				failureCount++
				printIfNotJSON(cmd.OutOrStderr(), isJSON, "Failed to get operation status (attempt %d/%d): %v\n", failureCount, maxFailures, err)
				if failureCount > maxFailures {
					return fail(errors.Wrapf(err, "failed to get operation status after %d failures", maxFailures))
				}
				time.Sleep(time.Second)
				continue
			}

			if op.GetError() != nil {
				return fail(errors.Errorf("operation failed: %s", op.GetError().Message))
			}

			if op.Done {
				printIfNotJSON(cmd.OutOrStdout(), isJSON, "Operation completed.\n")
				break
			}

			time.Sleep(5 * time.Second)
		}

		var resp tfpb.AssetLogsPackageResponse
		if err := op.GetResponse().UnmarshalTo(&resp); err != nil {
			return fail(errors.Wrap(err, "failed to unmarshal response"))
		}

		if params.outputDir == "" {
			if isJSON {
				output := jsonOutput{
					Status:        "success",
					OperationName: opName,
				}
				enc := json.NewEncoder(cmd.OutOrStdout())
				enc.SetIndent("", "  ")
				enc.Encode(output)
			} else {
				fmt.Fprintf(cmd.OutOrStdout(), "Since --output_dir is not specified, you can download the logs from %s\n", resp.SignedUrl)
			}
			return nil
		}

		out := cmd.OutOrStdout()
		if isJSON {
			out = io.Discard
		}

		if err := downloadFile(ctx, resp.SignedUrl, params.outputDir, out); err != nil {
			return fail(err)
		}

		if isJSON {
			u, err := url.Parse(resp.SignedUrl)
			if err != nil {
				return fail(errors.Wrap(err, "failed to parse signed URL"))
			}
			filename := path.Base(u.Path)
			downloadPath := ""
			if filename != "" {
				downloadPath = path.Join(params.outputDir, filename)
			}
			output := jsonOutput{
				Status:        "success",
				OperationName: opName,
				DownloadPath:  downloadPath,
			}
			enc := json.NewEncoder(cmd.OutOrStdout())
			enc.SetIndent("", "  ")
			enc.Encode(output)
		}

		return nil
	},
}

// downloadFile downloads the file from the given signed URL to the specified output directory.
func downloadFile(ctx context.Context, signedURL, outputDir string, out io.Writer) error {
	if outputDir == "" {
		return fmt.Errorf("output directory is required")
	}

	u, err := url.Parse(signedURL)
	if err != nil {
		return errors.Wrap(err, "failed to parse signed URL")
	}

	filename := path.Base(u.Path)
	outputPath := path.Join(outputDir, filename)

	fmt.Fprintf(out, "Downloading logs to %s ...\n", outputPath)

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, signedURL, nil)
	if err != nil {
		return errors.Wrap(err, "failed to create http request")
	}

	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return errors.Wrap(err, "failed to download file")
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return errors.Errorf("failed to download file, status code: %d", resp.StatusCode)
	}

	outFile, err := os.Create(outputPath)
	if err != nil {
		return errors.Wrap(err, "failed to create output file")
	}
	defer outFile.Close()

	_, err = io.Copy(outFile, resp.Body)
	if err != nil {
		return errors.Wrap(err, "failed to save file")
	}

	fmt.Fprintf(out, "Successfully downloaded to %s\n", outputPath)
	return nil
}

func printIfNotJSON(w io.Writer, isJSON bool, format string, a ...any) {
	if !isJSON {
		fmt.Fprintf(w, format, a...)
	}
}

var pullCmdFlags *cmdutils.CmdFlags

// init registers the fetch command with the showLogs command.
func init() {
	showLogs.AddCommand(fetchCmd)

	pullCmdFlags = cmdutils.NewCmdFlagsWithViper(localViper)
	pullCmdFlags.SetCommand(fetchCmd)

	pullCmdFlags.StringSlice(keyTypeResource, []string{}, "The names of the resources to include.")
	pullCmdFlags.StringSlice(keyTypeSkill, []string{}, "The skill IDs to include.")
	pullCmdFlags.StringSlice(keyInstance, []string{}, "The instances of the resources to include (format: resource/instance).")
	pullCmdFlags.String(keyWorkcell, "", "The name of the workcell.")
	pullCmdFlags.String(keyStartTime, "", "The start time of the logs to include (RFC3339 format).")
	pullCmdFlags.String(keyEndTime, "", "The end time of the logs to include (RFC3339 format).")
	pullCmdFlags.StringSlice(keyFilters, []string{}, "Additional regex filters to apply to the query.")
	pullCmdFlags.String(keyWithOpName, "", "Use this flag to reuse the operation name from a previous fetch command.")
	pullCmdFlags.String(keyOutputDir, "", "The directory to save the logs to.")
	pullCmdFlags.String(keyMaxWaitTime, defaultMaxWaitTime.String(), "The maximum time to wait for the operation to complete.")
	pullCmdFlags.String(keyOutput, "", "Output format. Supported values: json")
}

// buildAssetReferences builds a list of asset references from the given skill IDs, resource names, and instance names.
func buildAssetReferences(skillIDs, resourceNames, instanceNames []string) ([]*tfpb.AssetReference, error) {
	var assetRefs []*tfpb.AssetReference

	// De-duplicate skill IDs and add them.
	uniqueSkills := make(map[string]bool)
	for _, id := range skillIDs {
		if id == "" {
			continue
		}
		if uniqueSkills[id] {
			continue
		}
		uniqueSkills[id] = true

		assetRefs = append(assetRefs, &tfpb.AssetReference{
			AssetReference: &tfpb.AssetReference_Skill{
				Skill: &tfpb.SkillReference{Id: strings.TrimSpace(id)},
			},
		})
	}

	// De-deplicate any resources
	uniqueResources := make(map[string]bool)
	for _, name := range resourceNames {
		if name == "" {
			continue
		}
		if uniqueResources[name] {
			continue
		}
		uniqueResources[name] = true

		assetRefs = append(assetRefs, &tfpb.AssetReference{
			AssetReference: &tfpb.AssetReference_Resource{
				Resource: &tfpb.ResourceReference{Id: strings.TrimSpace(name)},
			},
		})
	}

	// De-deplicate any instances
	uniqueInstances := make(map[string]bool)
	for _, inst := range instanceNames {
		parts := strings.SplitN(inst, "/", 2)
		if len(parts) != 2 {
			return nil, errors.Errorf("invalid instance format %q, expected resource/instance", inst)
		}
		if uniqueInstances[inst] {
			continue
		}
		uniqueInstances[inst] = true

		resourceID := strings.TrimSpace(parts[0])
		instanceID := strings.TrimSpace(parts[1])

		assetRefs = append(assetRefs, &tfpb.AssetReference{
			AssetReference: &tfpb.AssetReference_Resource{
				Resource: &tfpb.ResourceReference{
					Id:         resourceID,
					InstanceId: &instanceID,
				},
			},
		})
	}

	return assetRefs, nil
}
