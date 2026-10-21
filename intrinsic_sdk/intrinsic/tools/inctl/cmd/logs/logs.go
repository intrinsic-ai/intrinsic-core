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

// Package logs defines a command for working with various logs.
package logs

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strings"

	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/tools/inctl/cmd/root"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"

	srvpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	sklpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

const (
	keyFollow        = "follow"
	keyPrefixType    = "prefix_type"
	keyPrefixID      = "prefix_id"
	keySinceSec      = "since"
	keyTailLines     = "tail"
	keyTimestamps    = "timestamps"
	keyTypeService   = "service"
	keyTypeSkill     = "skill"
	keyTypeAsset     = "asset"
	keyHiddenDebug   = "debug"
	keyOnpremAddress = "onprem_address"
	keyContext       = "context"
	keyTimeout       = "timeout"
	keySolution      = "solution"
)

const (
	// The port through which the simulation service can be reached.
	ingressPort = 17080
)

var (
	flagContext string
)

var (
	showLogs = &cobra.Command{
		Use:     "logs",
		Aliases: []string{"slogs"},
		Example: `  inctl logs --skill <skill-id> --service <service-name>
		Multiple skills:
		inctl logs --skill "<skill-id-1> <skill-id-2>"
		inctl logs --skill <skill-id-1> --skill <skill-id-2>
		Multiple services:
		inctl logs --service "<service-name-1> <service-name-2>"
		inctl logs --service <service-name-1> --service <service-name-2>
		Explicitly specify org and solution:
		inctl logs --org <organization@project-id> --solution <solution-id> --(target) <target-name-1> ... `,
		Short: "Prints logs from skills or services in a given solution",
		Long:  "Prints target logs (skill or service) from the instance running in a given solution.",
		Args:  cobra.NoArgs,
		RunE:  runLogsCmd,
	}

	localViper = viper.New()
	cmdFlags   = cmdutils.NewCmdFlagsWithViper(localViper)
)

type targetInfo struct {
	resourceType resourceType
	resourceID   string
}

func runLogsCmd(cmd *cobra.Command, args []string) error {
	verboseDebug = cmdFlags.GetBool(keyHiddenDebug)
	verboseOut = cmd.OutOrStderr()

	skillIDs, _ := cmd.Flags().GetStringSlice(keyTypeSkill)
	serviceNames, _ := cmd.Flags().GetStringSlice(keyTypeService)
	var assetNames []string

	assetNames, _ = cmd.Flags().GetStringSlice(keyTypeAsset)


	if len(skillIDs) == 0 && len(serviceNames) == 0 && len(assetNames) == 0 {
		cmd.PrintErrln("Error: at least one target must be specified.")
		return cmd.Help()
	}

	var targets []targetInfo
	for _, id := range skillIDs {
		resourceID, err := getResourceID(rtSkill, id)
		if err != nil {
			return err
		}
		targets = append(targets, targetInfo{resourceType: rtSkill, resourceID: resourceID})
	}
	for _, name := range serviceNames {
		targets = append(targets, targetInfo{resourceType: rtService, resourceID: name})
	}

	for _, name := range assetNames {
		resourceID, err := getResourceID(rtAsset, name)
		if err != nil {
			return err
		}
		targets = append(targets, targetInfo{resourceType: rtAsset, resourceID: resourceID})
	}


	ctx, cancelFx := signal.NotifyContext(cmd.Context(), os.Interrupt, os.Kill)
	defer cancelFx()

	group, ctx := errgroup.WithContext(ctx)

	// If multiple targets are passed, enable --prefix_id by default.
	prefixID := cmdFlags.GetBool(keyPrefixID)
	if !cmd.Flag(keyPrefixID).Changed && len(targets) > 1 {
		prefixID = true
	}

	// Iterate over all targets and read logs from each of them in parallel.
	// TODO(b/452702646): Add a target check before the loop
	for _, target := range targets {
		target := target // capture range variable
		group.Go(func() error {
			params := &cmdParams{
				follow:        cmdFlags.GetBool(keyFollow),
				timestamps:    cmdFlags.GetBool(keyTimestamps),
				tailLines:     cmdFlags.GetInt(keyTailLines),
				projectName:   cmdFlags.GetFlagProject(),
				context:       cmdFlags.GetString(keyContext),
				solution:      cmdFlags.GetString(keySolution),
				org:           cmdFlags.GetFlagOrganization(),
				onpremAddress: cmdFlags.GetString(keyOnpremAddress),
				resourceType:  target.resourceType,
				resourceID:    target.resourceID,
				prefixID:      prefixID,
				prefixType:    cmdFlags.GetBool(keyPrefixType),
				sinceSeconds:  cmdFlags.GetString(keySinceSec),
			}

			return readLogsFromSolution(ctx, params, cmd.OutOrStdout())
		})
	}

	if err := group.Wait(); err != nil {
		if errors.Is(err, context.DeadlineExceeded) {
			cmd.PrintErrln("[inctl] Command timeout reached. Increase the --timeout or use --timeout=0 for infinite streaming. Exiting.")
			return nil
		}
		cmd.PrintErrf("[inctl] Error reading logs: %v\n", err)
		os.Exit(1) // we are doing custom exit, as we are doing custom error handling
	}
	return nil
}

func getResourceID(resType resourceType, target string) (string, error) {
	// If the target is a textproto file, we will try to parse it and extract the ID from it.
	if strings.HasSuffix(target, ".textproto") {
		file, err := os.Open(target)
		if err != nil {
			return "", fmt.Errorf("cannot open manifest file: %w", err)
		}
		defer file.Close()
		content, err := io.ReadAll(file)
		if err != nil {
			return "", fmt.Errorf("cannot read manifest file: %w", err)
		}

		switch resType {
		case rtService:
			var manifest srvpb.ServiceManifest
			if err := prototext.Unmarshal(content, &manifest); err != nil {
				return "", fmt.Errorf("cannot parse manifest: %w", err)
			}
			return idutils.IDFrom(manifest.Metadata.Id.Package, manifest.Metadata.Id.Name)
		case rtSkill:
			var manifest sklpb.SkillManifest
			if err := prototext.Unmarshal(content, &manifest); err != nil {
				return "", fmt.Errorf("cannot parse manifest: %w", err)
			}
			return idutils.IDFrom(manifest.Id.Package, manifest.Id.Name)
		default:
			return "", fmt.Errorf("unexpected type %d", resType)
		}
	}

	// We didn't really get a file, so we will treat it as ID
	k8sNormalized := target
	if resType != rtSkill {
		// for the non-skill resources, we need to normalize labels
		k8sNormalized = strings.ReplaceAll(target, "_", "-")
		k8sNormalized = strings.ReplaceAll(k8sNormalized, ".", "-")
	}
	return k8sNormalized, nil
}

func init() {
	root.RootCmd.AddCommand(showLogs)
	cmdFlags.SetCommand(showLogs)

	cmdFlags.AddFlagsProjectOrg()

	showLogs.Flags().String(keySolution, "", "Solution ID from which logs will be read.")
	showLogs.Flags().String(keyContext, "", "The Kubernetes cluster to use")
	showLogs.Flags().String(keyTimeout, "300s", "Absolute wall-clock time that the command will run for before terminating (e.g. 300s, 1h). Note: this terminates the command even if logs are still actively streaming. Increase the --timeout or use --timeout=0 for infinite streaming.")
	showLogs.Flags().Bool(keyPrefixType, false, "Prefix each log line with the asset type, e.g., '[Skill]' or '[Service]'.")
	showLogs.Flags().Bool(keyPrefixID, false, "Prefix each log line with the asset ID, e.g., '[my-skill-id]'. Enabled for multiple targets are provided.")
	showLogs.Flags().Bool(keyFollow, false, "Whether to follow the solution logs.")
	showLogs.Flags().Bool(keyTimestamps, false, "Whether to include timestamps on each log line.")
	showLogs.Flags().MarkDeprecated(keyTimestamps, "Flag is deprecated. Timestamps are always added.")
	showLogs.Flags().Int(keyTailLines, 10, "(optional) The number of recent log lines to display. An input number less than 0 shows all log lines.")
	showLogs.Flags().String(keySinceSec, "", "Show logs starting since value. Value is either relative (e.g 10m) or \ndate time in RFC3339 format (e.g: 2006-01-02T15:04:05Z07:00)")
	showLogs.Flags().StringSlice(keyTypeSkill, []string{}, "(optional) Indicates logs source is a skill (or a list of skills)")
	showLogs.Flags().StringSlice(keyTypeService, []string{}, "(optional) Indicates logs source is a service (or a list of services)")
	showLogs.Flags().StringSlice(keyTypeAsset, []string{}, "(optional) Indicates logs source is a generic asset (or a list of assets)")
	showLogs.Flags().Bool(keyHiddenDebug, false, "Prints extensive debug messages")
	showLogs.Flags().String(keyOnpremAddress, "", "The onprem address (host:port) of the workcell. Used to circumvent the need of routing through the cloud, if the workcell is running in the same network as the inctl")

	// Bind all flags to viper
	if err := localViper.BindPFlags(showLogs.Flags()); err != nil {
		showLogs.PrintErrln("Internal error: ", err)
	}

	cmdFlags.MarkHidden(keyContext, keyTypeAsset)
}
