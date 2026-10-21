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

package cluster

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"strings"
	"text/tabwriter"

	"intrinsic/assets/clientutils"
	"intrinsic/skills/tools/skill/cmd/dialerutil"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"
	fmpb "google.golang.org/protobuf/types/known/fieldmaskpb"

	"github.com/intrinsic-ai/insrc/incode/cloud/devicemanager/version"

	assetCataloggrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	assetCatalogpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	assetIDpb "intrinsic/assets/proto/id_go_proto"
	inversiongrpcpb "intrinsic/kubernetes/inversion/v1/inversion_go_proto"
	inversionpb "intrinsic/kubernetes/inversion/v1/inversion_go_proto"

	clustermanagergrpcpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermanageralphagrpcpb "intrinsic/frontend/cloud/api/v1alpha1/clustermanager_api_go_proto"
)

var (
	clusterName               string
	rollbackFlag              bool
	runtimeFlag               string
	osFlag                    string
	userDataFlag              string
	solutionBranchFlag        string
	skipAssetVerificationFlag bool
)

// client helps run auth'ed requests for a specific cluster
type client struct {
	tokenSource *auth.ProjectToken
	cluster     string
	project     string
	org         string
	grpcConn    *grpc.ClientConn
	grpcClient  clustermanagergrpcpb.ClustersServiceClient
	alphaClient clustermanageralphagrpcpb.ClustersServiceClient
}

type clusterInfo struct {
	rollback    bool
	mode        string
	state       string
	currentBase string
	currentOS   string
}

// status queries the update status of a cluster
func (c *client) status(ctx context.Context) (*clusterInfo, error) {
	req := clustermanagerpb.GetClusterRequest{
		ClusterId: c.cluster,
	}
	cluster, err := c.grpcClient.GetCluster(ctx, &req)
	if err != nil {
		return nil, fmt.Errorf("cluster status: %w", err)
	}
	var cp *clustermanagerpb.IPCNode
	for _, n := range cluster.GetIpcNodes() {
		if n.GetIsControlPlane() {
			cp = n
			break
		}
	}
	if cp == nil {
		return nil, fmt.Errorf("control plane not found in cluster list: %q", c.cluster)
	}
	info := &clusterInfo{
		rollback:    cluster.GetRollbackAvailable(),
		mode:        decodeUpdateMode(cluster.GetUpdateMode()),
		state:       decodeUpdateState(cluster.GetUpdateState()),
		currentBase: cluster.GetPlatformVersion(),
		currentOS:   cp.GetOsVersion(),
	}
	return info, nil
}

// setMode runs a request to set the update mode
func (c *client) setMode(ctx context.Context, mode string) error {
	pbm := encodeUpdateMode(mode)
	if pbm == clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_UNSPECIFIED {
		return fmt.Errorf("invalid mode: %s", mode)
	}
	req := clustermanagerpb.UpdateClusterRequest{
		Cluster: &clustermanagerpb.Cluster{
			ClusterName: c.cluster,
			UpdateMode:  pbm,
		},
		UpdateMask: &fmpb.FieldMask{Paths: []string{"update_mode"}},
	}
	_, err := c.grpcClient.UpdateCluster(ctx, &req)
	if err != nil {
		return fmt.Errorf("update cluster: %w", err)
	}
	return nil
}

// This is copied from clustermanager.go, but we could diverge from the strings used by
// Inversion if we prefer a different UX.
var updateModeMap = map[string]clustermanagerpb.PlatformUpdateMode{
	"off":              clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_OFF,
	"on":               clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_ON,
	"automatic":        clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_AUTOMATIC,
	"on+accept":        clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_MANUAL_WITH_ACCEPT,
	"automatic+accept": clustermanagerpb.PlatformUpdateMode_PLATFORM_UPDATE_MODE_AUTOMATIC_WITH_ACCEPT,
}

// encodeUpdateMode encodes a mode string to a proto definition
func encodeUpdateMode(mode string) clustermanagerpb.PlatformUpdateMode {
	return updateModeMap[mode]
}

var updateModeReverseMap map[clustermanagerpb.PlatformUpdateMode]string

func init() {
	updateModeReverseMap = make(map[clustermanagerpb.PlatformUpdateMode]string, len(updateModeMap))
	for k, v := range updateModeMap {
		updateModeReverseMap[v] = k
	}
}

// decodeUpdateMode decodes a mode proto definition into a string
func decodeUpdateMode(mode clustermanagerpb.PlatformUpdateMode) string {
	if m, ok := updateModeReverseMap[mode]; ok {
		return m
	}
	return "unknown"
}

func decodeUpdateState(state clustermanagerpb.UpdateState) string {
	switch state {
	case clustermanagerpb.UpdateState_UPDATE_STATE_UPDATING:
		return "Updating"
	case clustermanagerpb.UpdateState_UPDATE_STATE_PENDING:
		// While we handle this UpdateState it is not actually returned by the backend.
		// It gets translated to UPDATE_STATE_DEPLOYED.
		return "Pending"
	case clustermanagerpb.UpdateState_UPDATE_STATE_PENDING_APPROVAL:
		return "PendingApproval"
	case clustermanagerpb.UpdateState_UPDATE_STATE_FAULT:
		return "Fault"
	case clustermanagerpb.UpdateState_UPDATE_STATE_DEPLOYED:
		return "Deployed"
	// We no longer expose the "Blocked" state to the user.
	// It gets translated to UPDATE_STATE_DEPLOYED.
	default:
		return "Unknown"
	}
}

// getMode runs a request to read the update mode
func (c *client) getMode(ctx context.Context) (string, error) {
	req := clustermanagerpb.GetClusterRequest{
		ClusterId: c.cluster,
	}
	cluster, err := c.grpcClient.GetCluster(ctx, &req)
	if err != nil {
		return "", fmt.Errorf("cluster status: %w", err)
	}
	mode := cluster.GetUpdateMode()
	return decodeUpdateMode(mode), nil
}

// getDisplayName runs a request to read the display name
func (c *client) getDisplayName(ctx context.Context) (string, error) {
	req := clustermanagerpb.GetClusterRequest{
		ClusterId: c.cluster,
	}
	cluster, err := c.grpcClient.GetCluster(ctx, &req)
	if err != nil {
		return "", fmt.Errorf("cluster status: %w", err)
	}
	return cluster.DisplayName, nil
}

// setDisplayName runs a request to set the display name
func (c *client) setDisplayName(ctx context.Context, name string) error {
	req := clustermanagerpb.UpdateClusterRequest{
		Cluster: &clustermanagerpb.Cluster{
			ClusterName: c.cluster,
			DisplayName: name,
		},
		UpdateMask: &fmpb.FieldMask{Paths: []string{"display_name"}},
	}
	_, err := c.grpcClient.UpdateCluster(ctx, &req)
	if err != nil {
		return fmt.Errorf("update cluster: %w", err)
	}
	return nil
}

// run runs an update if one is pending
func (c *client) run(ctx context.Context) error {
	req := clustermanagerpb.SchedulePlatformUpdateRequest{
		ClusterId:  c.cluster,
		UpdateType: clustermanagerpb.SchedulePlatformUpdateRequest_UPDATE_TYPE_FORWARD,
	}
	if rollbackFlag {
		req.UpdateType = clustermanagerpb.SchedulePlatformUpdateRequest_UPDATE_TYPE_ROLLBACK
	} else if osFlag != "" || runtimeFlag != "" || solutionBranchFlag != "" {
		req.Versions = &clustermanagerpb.UpdateVersions{}
		runtimeVersion := runtimeFlag
		if strings.HasPrefix(runtimeVersion, "202") {
			// runtimeVersion is old format and not semver
			runtimeVersion = version.TranslateBaseUIToAPI(runtimeVersion)
		}
		osVersion := version.TranslateOSUIToAPI(osFlag)

		if !skipAssetVerificationFlag {
			_, conn, err := clientutils.DialCatalog(ctx, clientutils.DialCatalogOptions{
				// DialCatalog is broken and wants the qualified org name
				// Internally it's not requried due to some defaulting logic.
				// Externally it's not required when customers can avoid using the `@` logic.
				// But the way the code is written, it'll always work when the `@` is provided here.
				Org: c.org + "@" + c.project,
			})
			if err != nil {
				return fmt.Errorf("dial catalog: %w", err)
			}
			defer conn.Close()
			assetCatalogClient := assetCataloggrpcpb.NewAssetCatalogClient(conn)

			if runtimeFlag != "" {
				assetRequest := &assetCatalogpb.GetAssetRequest{}
				assetRequest.AssetId = &assetCatalogpb.GetAssetRequest_IdVersion{
					IdVersion: &assetIDpb.IdVersion{
						Id: &assetIDpb.Id{
							Package: "ai.intrinsic",
							Name:    "runtime",
						},
						Version: runtimeVersion,
					},
				}
				_, err := assetCatalogClient.GetAsset(ctx, assetRequest)
				if err != nil {
					if grpcstatus.Code(err) == codes.NotFound {
						return fmt.Errorf("runtime version %q not found", runtimeFlag)
					}
					return fmt.Errorf("get asset: %w", err)
				}
			}
			if osFlag != "" {
				assetRequest := &assetCatalogpb.GetAssetRequest{}
				assetRequest.AssetId = &assetCatalogpb.GetAssetRequest_IdVersion{
					IdVersion: &assetIDpb.IdVersion{
						Id: &assetIDpb.Id{
							Package: "ai.intrinsic",
							Name:    "os",
						},
						Version: osVersion,
					},
				}
				_, err := assetCatalogClient.GetAsset(ctx, assetRequest)
				if err != nil {
					if grpcstatus.Code(err) == codes.NotFound {
						return fmt.Errorf("OS version %q not found", osFlag)
					}
					return fmt.Errorf("get asset: %w", err)
				}
			}
		}

		if runtimeFlag != "" {
			req.Versions.BaseVersion = runtimeVersion
		}
		if osFlag != "" {
			req.Versions.OsVersion = osVersion
		}
		if userDataFlag != "" {
			req.Versions.UserData = userDataFlag
		}
		if solutionBranchFlag != "" {
			req.Versions.SolutionVersion = &clustermanagerpb.SolutionVersion{
				BranchId: solutionBranchFlag,
			}
		}
		if skipAssetVerificationFlag {
			req.UpdateType = clustermanagerpb.SchedulePlatformUpdateRequest_UPDATE_TYPE_VERSIONED_UNCHECKED
		} else {
			req.UpdateType = clustermanagerpb.SchedulePlatformUpdateRequest_UPDATE_TYPE_VERSIONED
		}
	}

	_, err := c.grpcClient.SchedulePlatformUpdate(ctx, &req)
	if err != nil {
		if st, ok := grpcstatus.FromError(err); ok {
			switch st.Code() {
			case codes.FailedPrecondition:
				return fmt.Errorf("Cannot upgrade due to current settings: %s", st.Message())
			case codes.NotFound:
				if req.UpdateType == clustermanagerpb.SchedulePlatformUpdateRequest_UPDATE_TYPE_FORWARD {
					return fmt.Errorf("Cluster does not exist, or doesn't have an update pending: %s", st.Message())
				} else {
					return fmt.Errorf("Cluster does not exist: %s", st.Message())
				}
			case codes.InvalidArgument:
				return fmt.Errorf("Request was malformed: %s", st.Message())
			case codes.AlreadyExists:
				return fmt.Errorf("Cluster solution was deployed manually. Cannot upgrade solutions that are not managed via cluster upgrade: %s", st.Message())
			case codes.Internal:
				return fmt.Errorf("Update request could not be handled by the server: %s", st.Message())
			default:
				return fmt.Errorf("Upgrade failed (status %v): %s", st.Code(), st.Message())
			}
		}
		return fmt.Errorf("cluster upgrade run: %w", err)
	}
	mode, err := c.getMode(ctx)
	if err != nil {
		fmt.Println("The upgrade got scheduled but failed to check the upgrade mode.")
	}
	if mode == "off" {
		fmt.Println("This upgrade won't start until you run `inctl cluster upgrade mode on`.")
	}
	return nil
}

func (c *client) clearFaults(ctx context.Context) error {
	req := clustermanagerpb.ClearUpdateFaultRequest{
		ClusterId: c.cluster,
	}
	_, err := c.grpcClient.ClearUpdateFault(ctx, &req)
	if err != nil {
		return err
	}
	return nil
}

func (c *client) close() error {
	if c.grpcConn != nil {
		return c.grpcConn.Close()
	}
	return nil
}

func newTokenSource(project string) (*auth.ProjectToken, error) {
	configuration, err := auth.NewStore().GetConfiguration(project)
	if err != nil {
		if errors.Is(err, os.ErrNotExist) {
			return nil, &dialerutil.ErrCredentialsNotFound{
				CredentialName: project,
				Err:            err,
			}
		}
		return nil, fmt.Errorf("get configuration for project %q: %w", project, err)
	}
	token, err := configuration.GetDefaultCredentials()
	if err != nil {
		return nil, fmt.Errorf("get default credentials for project %q: %w", project, err)
	}
	return token, nil
}

func newClient(ctx context.Context, org, project, cluster string) (context.Context, client, error) {
	ts, err := newTokenSource(project)
	if err != nil {
		return nil, client{}, err
	}
	params := dialerutil.DialInfoParams{
		Cluster:  cluster,
		CredName: project,
		CredOrg:  org,
	}
	ctx, conn, err := dialerutil.DialConnectionCtx(ctx, params)
	if err != nil {
		return nil, client{}, fmt.Errorf("create grpc client: %w", err)
	}
	return ctx, client{
		tokenSource: ts,
		cluster:     cluster,
		project:     project,
		org:         org,
		grpcConn:    conn,
		grpcClient:  clustermanagergrpcpb.NewClustersServiceClient(conn),
		alphaClient: clustermanageralphagrpcpb.NewClustersServiceClient(conn),
	}, nil
}

const modeCmdDesc = `
Read/Write the current update mechanism mode

There are 3 modes on the system:

- 'off': no updates can run
- 'on': updates go to the IPC when triggered with inctl or the IPC manager
- 'automatic': updates go to the IPC as soon as they are available

You can add the "+accept" suffix to require acceptance of the update on the
IPC. Acceptance is normally performed through the HMI, although for testing
you can also use "inctl cluster upgrade accept".
`

var modeCmd = &cobra.Command{
	Use:   "mode",
	Short: "Read/Write the current update mechanism mode",
	Long:  modeCmdDesc,
	// at most one arg, the mode
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client: %w", err)
		}
		defer c.close()
		switch len(args) {
		case 0:
			mode, err := c.getMode(ctx)
			if err != nil {
				return fmt.Errorf("get cluster upgrade mode:\n%w", err)
			}
			ot := printer.GetFlagOutputType(cmd)
			if ot == printer.OutputTypeJSON {
				b, err := json.Marshal(map[string]string{"mode": mode})
				if err != nil {
					return err
				}
				fmt.Fprintln(cmd.OutOrStdout(), string(b))
			} else {
				fmt.Printf("update mechanism mode: %s\n", mode)
			}
			return nil
		case 1:
			if err := c.setMode(ctx, args[0]); err != nil {
				return fmt.Errorf("set cluster upgrade mode:\n%w", err)
			}
			return nil
		default:
			return fmt.Errorf("invalid number of arguments. At most 1: %d", len(args))
		}
	},
}

const runCmdDesc = `
Run an upgrade on the specified cluster. With no arguments it will upgrade to
the latest stable release if available. Arguments can specify the OS or
runtime versions to use an older or newer build.

Warning: Depending on the mode of the cluster (inctl cluster upgrade mode),
the update will:

-   mode=automatic: be ignored, as the cluster will automatically return to the
    latest available version.
-   mode=on: execute right away. Make sure the cluster is safe and ready to
    upgrade. It might reboot in the process.
-   mode=on+accept: not execute until approved by the operator or with
    "inctl cluster upgrade accept".
-   mode=off: be rejected and the command will fail.

Examples:

# Upgrade to latest stable OS and runtime releases:
inctl cluster upgrade run --org my_org@my-project --cluster node-fc66c2ab-5770-43b8-aefe-a36a2f356fb1

# Upgrade to specific OS and runtime releases:
inctl cluster upgrade run --os 20251126.RC01 --runtime 0.20260126.0-RC07 \
  --org my_org@my-project --cluster node-fc66c2ab-5770-43b8-aefe-a36a2f356fb1

# Undo the last automatic upgrade:
inctl cluster upgrade run --rollback \
  --org my_org@my-project --cluster node-fc66c2ab-5770-43b8-aefe-a36a2f356fb1
`

// runCmd is the command to trigger or execute an update
var runCmd = &cobra.Command{
	Use:   "run",
	Short: "Assign or start an upgrade to the latest or a specified release.",
	Long:  runCmdDesc,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()

		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		qOrgName := orgutil.QualifiedOrg(projectName, orgName)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client:\n%w", err)
		}
		defer c.close()
		err = c.run(ctx)
		if err != nil {
			return fmt.Errorf("cluster upgrade run:\n%w", err)
		}

		fmt.Printf("update for cluster %q in %q kicked off successfully.\n", clusterName, qOrgName)
		fmt.Printf("monitor running `inctl cluster upgrade --org %s --cluster %s\n`", qOrgName, clusterName)
		return nil
	},
}

const upgradeCmdDesc = `
Shows the currently installed OS and base version, and provides subcommands to manage software upgrades.
With no command it will show the current installed OS and base version.

Example:

inctl cluster upgrade --org my_org@my-project --cluster node-fc66c2ab-5770-43b8-aefe-a36a2f356fb1
`

type upgradeStatusInfo struct {
	Project           string `json:"project"`
	Cluster           string `json:"cluster"`
	Mode              string `json:"mode"`
	State             string `json:"state"`
	RollbackAvailable bool   `json:"rollbackAvailable"`
	Runtime           string `json:"runtime"`
	OS                string `json:"os"`
}

// clusterUpgradeCmd is the base command to query the upgrade state
var clusterUpgradeCmd = &cobra.Command{
	Use:   "upgrade",
	Short: "Show or upgrade Intrinsic software on a target cluster",
	Long:  upgradeCmdDesc,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()

		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client:\n%w", err)
		}
		defer c.close()
		ui, err := c.status(ctx)
		if err != nil {
			return fmt.Errorf("cluster status:\n%w", err)
		}
		osVer := version.TranslateOSAPIToUI(ui.currentOS)
		runtime := ui.currentBase
		if strings.HasPrefix(runtime, "0.0.1") {
			// assume this is old format and needs translation
			runtime = version.TranslateBaseAPIToUI(runtime)
		}

		ot := printer.GetFlagOutputType(cmd)
		if ot == printer.OutputTypeJSON {
			b, err := json.Marshal(upgradeStatusInfo{
				Project:           projectName,
				Cluster:           clusterName,
				Mode:              ui.mode,
				State:             ui.state,
				RollbackAvailable: ui.rollback,
				Runtime:           runtime,
				OS:                osVer,
			})
			if err != nil {
				return err
			}
			fmt.Fprintln(cmd.OutOrStdout(), string(b))
		} else {
			w := tabwriter.NewWriter(os.Stdout, 0, 0, 3, ' ', 0)
			fmt.Fprintf(w, "project\tcluster\tmode\tstate\trollback available\truntime\tos\n")
			fmt.Fprintf(w, "%s\t%s\t%s\t%s\t%v\t%s\t%s\n", projectName, clusterName, ui.mode, ui.state, ui.rollback, runtime, osVer)
			w.Flush()
		}
		return nil
	},
}

// acceptCmd is the command to accept an update on the IPC in the '+accept' modes.
var acceptCmd = &cobra.Command{
	Use:   "accept",
	Short: "Accept an upgraded Intrinsic software on target cluster",
	Long:  "Accept an upgraded Intrinsic software (OS and intrinsic-base) on target cluster.",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()

		consoleIO := bufio.NewReadWriter(
			bufio.NewReader(cmd.InOrStdin()),
			bufio.NewWriter(cmd.OutOrStdout()))

		conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(ClusterCmdViper), auth.WithCluster(clusterName))
		if err != nil {
			return err
		}
		defer conn.Close()

		client := inversiongrpcpb.NewIpcUpdaterClient(conn)
		uir, err := client.ReportUpdateInfo(ctx, &inversionpb.GetUpdateInfoRequest{})
		if err != nil {
			return fmt.Errorf("update info request: %w", err)
		}
		if uir.GetState() != inversionpb.UpdateInfo_STATE_UPDATE_AVAILABLE {
			return fmt.Errorf("update not available")
		}

		fmt.Fprintf(consoleIO,
			"Update from %s to %s is available.\nAre you sure you want to accept the update? [y/n] ",
			uir.GetCurrent().GetVersionId(), uir.GetAvailable().GetVersionId())
		consoleIO.Flush()
		response, err := consoleIO.ReadString('\n')
		if err != nil {
			return fmt.Errorf("read response: %w", err)
		}
		response = strings.ToLower(strings.TrimSpace(response))
		if response != "y" {
			return fmt.Errorf("user did not confirm: %q", response)
		}

		if _, err := client.ApproveUpdate(ctx, &inversionpb.ApproveUpdateRequest{
			Approved: &inversionpb.IntrinsicVersion{
				VersionId: uir.GetAvailable().GetVersionId(),
			},
		}); err != nil {
			return fmt.Errorf("accept update: %w", err)
		}
		return nil
	},
}

const displayNameCmdDesc = `
Read/Write the display name of the cluster.
`

var displayNameCmd = &cobra.Command{
	Use:   "displayname",
	Short: "Read/Write the display name",
	Long:  displayNameCmdDesc,
	// at most one arg, the mode
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client: %w", err)
		}
		defer c.close()
		switch len(args) {
		case 0:
			displayName, err := c.getDisplayName(ctx)
			if err != nil {
				return fmt.Errorf("get cluster displayname:\n%w", err)
			}
			ot := printer.GetFlagOutputType(cmd)
			if ot == printer.OutputTypeJSON {
				b, err := json.Marshal(map[string]string{"displayName": displayName})
				if err != nil {
					return err
				}
				fmt.Fprintln(cmd.OutOrStdout(), string(b))
			} else {
				fmt.Printf("display name: %q\n", displayName)
			}
			return nil
		case 1:
			if err := c.setDisplayName(ctx, args[0]); err != nil {
				return fmt.Errorf("set cluster displayname:\n%w", err)
			}
			return nil
		default:
			return fmt.Errorf("invalid number of arguments. At most 1: %d", len(args))
		}
	},
}

type upgradeReportInfo struct {
	State       string `json:"state"`
	VersionID   string `json:"versionId,omitempty"`
	UpdateNotes string `json:"updateNotes,omitempty"`
}

// reportCmd is the command to report information about an upgrade.
var reportCmd = &cobra.Command{
	Use:   "report",
	Short: "Report information about an upgrade on the target cluster",
	Long:  "Report information about an upgrade of Intrinsic software (OS and intrinsic-base) on the target cluster.",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		ctx := cmd.Context()

		consoleIO := bufio.NewWriter(cmd.OutOrStdout())

		conn, err := auth.NewCloudConnection(ctx, auth.WithFlagValues(ClusterCmdViper), auth.WithCluster(clusterName))
		if err != nil {
			return err
		}
		defer conn.Close()

		client := inversiongrpcpb.NewIpcUpdaterClient(conn)
		uir, err := client.ReportUpdateInfo(ctx, &inversionpb.GetUpdateInfoRequest{})
		if err != nil {
			return fmt.Errorf("update info request: %w", err)
		}

		ot := printer.GetFlagOutputType(cmd)
		if ot == printer.OutputTypeJSON {
			info := upgradeReportInfo{
				State: uir.GetState().String(),
			}
			if uir.GetState() == inversionpb.UpdateInfo_STATE_UPDATE_AVAILABLE {
				info.VersionID = uir.GetAvailable().GetVersionId()
				info.UpdateNotes = uir.GetAvailable().GetUpdateNotes()
			}
			b, err := json.Marshal(info)
			if err != nil {
				return err
			}
			fmt.Fprintln(cmd.OutOrStdout(), string(b))
			return nil
		}

		switch uir.GetState() {
		case inversionpb.UpdateInfo_STATE_UPDATE_RUNNING:
			fmt.Fprintf(consoleIO, "Upgrade is running.\n")
		case inversionpb.UpdateInfo_STATE_UPDATE_AVAILABLE:
			fmt.Fprintf(consoleIO, "Upgrade to %q is available.\n", uir.GetAvailable().GetVersionId())
			if uir.GetAvailable().GetUpdateNotes() != "" {
				fmt.Fprintf(consoleIO, "Upgrade notes:\n%q\n", uir.GetAvailable().GetUpdateNotes())
			}
		case inversionpb.UpdateInfo_STATE_UP_TO_DATE:
			fmt.Fprintf(consoleIO, "Cluster is up to date.\n")
		default:
			fmt.Fprintf(consoleIO, "System is in an unexpected state %q. Please contact support for further assistance.", uir.GetState())
		}
		consoleIO.Flush()
		return nil
	},
}

const clearFaultsCmdDesc = `
Clear a platform update fault state for a specific cluster.

If the cluster's update_state is 'Fault', this command will clear the fault,
allowing further update operations.

Note: If the cluster's update_mode is enabled (e.g. 'on', 'automatic'),
clearing the fault may trigger an immediate retry of the last failed update attempt.
`

var clearFaultsCmd = &cobra.Command{
	Use:   "clear-faults",
	Short: "Clear a platform update fault state for a specific cluster",
	Long:  clearFaultsCmdDesc,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		projectName := ClusterCmdViper.GetString(orgutil.KeyProject)
		orgName := ClusterCmdViper.GetString(orgutil.KeyOrganization)
		ctx, c, err := newClient(ctx, orgName, projectName, clusterName)
		if err != nil {
			return fmt.Errorf("cluster upgrade client: %w", err)
		}
		defer c.close()
		if err := c.clearFaults(ctx); err != nil {
			return fmt.Errorf("clear cluster upgrade faults:\n%w", err)
		}
		fmt.Printf("Cluster %q update fault cleared successfully.\n", clusterName)
		return nil
	},
}

func init() {
	ClusterCmd.AddCommand(displayNameCmd)
	displayNameCmd.PersistentFlags().StringVar(&clusterName, "cluster", "", "Name of cluster to upgrade.")
	displayNameCmd.MarkPersistentFlagRequired("cluster")
	ClusterCmd.AddCommand(clusterUpgradeCmd)
	clusterUpgradeCmd.PersistentFlags().StringVar(&clusterName, "cluster", "", "Name of cluster to upgrade.")
	clusterUpgradeCmd.MarkPersistentFlagRequired("cluster")
	clusterUpgradeCmd.AddCommand(runCmd)
	runCmd.PersistentFlags().BoolVar(&rollbackFlag, "rollback", false, "Whether to trigger a rollback update instead.")
	runCmd.PersistentFlags().StringVar(&osFlag, "os", "", "The os version to upgrade to.")
	runCmd.PersistentFlags().StringVar(&runtimeFlag, "base", "", "Deprecated. Use --runtime instead.")
	runCmd.PersistentFlags().MarkDeprecated("base", "Use --runtime instead.")
	runCmd.PersistentFlags().StringVar(&runtimeFlag, "runtime", "", "The runtime version to upgrade to.")
	runCmd.PersistentFlags().StringVar(&userDataFlag, "user-data", "", "Optional data describing the update.")
	runCmd.PersistentFlags().BoolVar(&skipAssetVerificationFlag, "skip-asset-verification", false, "If true, the update will skip asset catalog verification.")
	runCmd.PersistentFlags().StringVar(&solutionBranchFlag, "solution-branch", "", "The solution branch to upgrade to.")
	clusterUpgradeCmd.AddCommand(modeCmd)
	clusterUpgradeCmd.AddCommand(acceptCmd)
	clusterUpgradeCmd.AddCommand(reportCmd)
	clusterUpgradeCmd.AddCommand(clearFaultsCmd)
}
