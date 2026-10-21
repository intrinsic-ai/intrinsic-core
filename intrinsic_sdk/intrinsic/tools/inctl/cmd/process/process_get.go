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

package process

import (
	"context"
	"fmt"
	"os"

	"intrinsic/assets/idutils"
	"intrinsic/assets/platformlevelswitch"
	"intrinsic/assets/processes/processbundle"
	installedassetspb "intrinsic/assets/proto/installed_assets_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
	behaviortreepb "intrinsic/executive/proto/behavior_tree_go_proto"
	executiveservicepb "intrinsic/executive/proto/executive_service_go_proto"
	solutionservicepb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	protoregistrygrpcpb "intrinsic/proto_tools/proto/proto_registry_go_proto"
	"intrinsic/proto_tools/registry/protoregistryclient"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoregistry"
)

var viperProcessGet = viper.New()

type messageWithBT struct {
	// A BehaviorTree or ProcessManifest (which contains a nested BehaviorTree).
	message proto.Message

	// Points to the BehaviorTree in 'message' or to 'message' itself (if
	// 'message' is a BehaviorTree).
	behaviorTree *behaviortreepb.BehaviorTree
}

func serializeToTextProto(ctx context.Context, msg messageWithBT, protoRegistry protoregistrygrpcpb.ProtoRegistryClient) ([]byte, error) {
	nodeTypes, err := MergedTypesForAllScriptNodesInTree(ctx, msg.behaviorTree)
	if err != nil {
		return nil, errors.Wrap(err, "failed creating merged Types from behavior tree script nodes")
	}

	// Marshal while resolving Intrinsic type URLs using the proto registry and
	// other type URLs using the descriptors collected from the behavior tree
	// (with the compiled-in types as a last fallback).
	marshaller := prototext.MarshalOptions{
		Resolver: protoregistryclient.NewProtoRegistryResolver(
			ctx,
			protoRegistry,
			[]protoregistryclient.Resolver{nodeTypes, protoregistry.GlobalTypes},
		),
		Indent:    "  ",
		Multiline: true,
	}
	s := marshaller.Format(msg.message)
	return []byte(s), nil
}

func serializeToBinaryProto(msg messageWithBT) ([]byte, error) {
	marshaller := proto.MarshalOptions{}
	content, err := marshaller.Marshal(msg.message)
	if err != nil {
		return nil, errors.Wrap(err, "could not marshal BT")
	}
	return content, nil
}

func serializeMessage(ctx context.Context, protoRegistry protoregistrygrpcpb.ProtoRegistryClient, msg messageWithBT, format string) ([]byte, error) {
	var data []byte
	var err error
	switch format {
	case TextProtoFormat:
		data, err = serializeToTextProto(ctx, msg, protoRegistry)
		if err != nil {
			return nil, errors.Wrapf(err, "could not serialize BT to text")
		}
	case BinaryProtoFormat:
		data, err = serializeToBinaryProto(msg)
		if err != nil {
			return nil, errors.Wrapf(err, "could not serialize BT to binary")
		}
	default:
		return nil, fmt.Errorf("unknown format %s", format)
	}

	return data, nil
}

type getProcessParams struct {
	executive       executiveservicepb.ExecutiveServiceClient
	solutionService solutionservicepb.SolutionServiceClient
	protoRegistry   protoregistrygrpcpb.ProtoRegistryClient
	installedAssets installedassetspb.InstalledAssetsClient
	nameOrId        string
	format          string
	clearTreeID     bool
	clearNodeIDs    bool
}

func downloadProcess(ctx context.Context, params *getProcessParams) (messageWithBT, error) {
	if params.nameOrId == "" {
		activeBT, err := getActiveBT(ctx, params.executive)
		if err != nil {
			return messageWithBT{}, errors.Wrap(err, "could not get active behavior tree")
		}
		return messageWithBT{
			message:      activeBT,
			behaviorTree: activeBT,
		}, nil
	}

	// Try installed assets if nameOrId looks like an asset ID.
	id, err := idutils.IDProtoFromString(params.nameOrId)
	if err == nil {
		asset, err := params.installedAssets.GetInstalledAsset(
			ctx,
			&installedassetspb.GetInstalledAssetRequest{
				Id:   id,
				View: viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL,
			},
		)

		if err == nil {
			// Explicitly do not support getting Process assets as binary protos. The
			// proper binary representation for Process assets on disk are bundle
			// files which are not supported by this command.
			//
			// We cannot perform this check earlier since we don't know whether the
			// given nameOrId really is an asset ID or a legacy process name.
			if params.format == BinaryProtoFormat {
				return messageWithBT{}, fmt.Errorf("--process_format=%v is not supported for getting process assets", BinaryProtoFormat)
			}

			// Convert asset to a manifest which can be inspected as textproto and is
			// also suitable for creating a bundle file from it.
			manifest, err := processbundle.ManifestFromAsset(asset.DeploymentData.GetProcess().Process)
			if err != nil {
				return messageWithBT{}, errors.Wrap(err, "could not convert asset to manifest")
			}

			return messageWithBT{
				message:      manifest,
				behaviorTree: manifest.BehaviorTree,
			}, nil
		}

		// If we see a NotFound error, proceed and try legacy lookup. Even if it
		// looks like an asset ID it can still be a legacy proccess name name such
		// as "my_tree.bt.pb".
		if status.Code(err) != codes.NotFound {
			return messageWithBT{}, errors.Wrap(err, "could not query installed assets")
		}
	}

	// Try the legacy lookup if we weren't successful so far
	namedBT, err := params.solutionService.GetBehaviorTree(
		ctx, &solutionservicepb.GetBehaviorTreeRequest{Name: params.nameOrId},
	)
	if err != nil {
		return messageWithBT{}, errors.Wrap(err, "could not get named behavior tree")
	}
	return messageWithBT{
		message:      namedBT,
		behaviorTree: namedBT,
	}, nil
}

func getProcess(ctx context.Context, params *getProcessParams) ([]byte, error) {
	// Returns a BehaviorTree or ProcessManifest depending on the source
	outputMsg, err := downloadProcess(ctx, params)
	if err != nil {
		return nil, errors.Wrap(err, "could not download process")
	}

	clearTree(outputMsg.behaviorTree, params.clearTreeID, params.clearNodeIDs)

	return serializeMessage(ctx, params.protoRegistry, outputMsg, params.format)
}

var processGetCmd = platformlevelswitch.WrapCmd(
	&cobra.Command{
		Use:   "get [asset_id]",
		Short: "Get a process from a solution.",
		Long: `Get a process from a currently deployed solution.

One positional argument: Get the Process asset with the given ID as installed in the solution. The output is an intrinsic_proto.processes.ProcessManifest proto.
$ inctl process get --org my_org --solution my_solution_id [--output_file /tmp/process.txtpb] [--process_format textproto|binaryproto] com.example.my_process

No positional argument: Get the "active" process which is currently loaded in the executive. The output is an intrinsic_proto.executive.BehaviorTree proto.
$ inctl process get --org my_org --solution my_solution_id [--output_file /tmp/process.txtpb] [--process_format textproto|binaryproto]

[legacy process support]
One positional argument: Get the legacy process with the given name as stored in the solution. The output is an intrinsic_proto.executive.BehaviorTree proto.
$ inctl process get --org my_org --solution my_solution_id [--output_file /tmp/process.txtpb] [--process_format textproto|binaryproto] "My Process"`,
		Args: cobra.RangeArgs(0, 1),
		RunE: func(cmd *cobra.Command, args []string) error {
			nameOrId := ""
			if len(args) == 1 {
				nameOrId = args[0]
			}
			projectName := viperProcessGet.GetString(orgutil.KeyProject)
			orgName := viperProcessGet.GetString(orgutil.KeyOrganization)
			ctx, conn, err := connectToCluster(cmd.Context(), projectName,
				orgName, flagServerAddress,
				flagSolutionName, flagClusterName)
			if err != nil {
				return errors.Wrapf(err, "could not dial connection")
			}
			defer conn.Close()

			content, err := getProcess(ctx, &getProcessParams{
				executive:       executiveservicepb.NewExecutiveServiceClient(conn),
				solutionService: solutionservicepb.NewSolutionServiceClient(conn),
				protoRegistry:   protoregistrygrpcpb.NewProtoRegistryClient(conn),
				installedAssets: installedassetspb.NewInstalledAssetsClient(conn),
				nameOrId:        nameOrId,
				format:          flagProcessFormat,
				clearTreeID:     flagClearTreeID,
				clearNodeIDs:    flagClearNodeIDs,
			})
			if err != nil {
				return errors.Wrapf(err, "could not get process")
			}

			if flagOutputFile != "" {
				if err := os.WriteFile(flagOutputFile, content, 0o644); err != nil {
					return errors.Wrapf(err, "could not write to file %s", flagOutputFile)
				}
				return nil
			}

			fmt.Println(string(content))

			return nil
		},
	},
	viperProcessGet,
)

func init() {
	addCommonGetSetFlags(processGetCmd)
	processGetCmd.Flags().StringVar(&flagOutputFile, "output_file", "", "If set, writes the process to the given file instead of stdout.")
}
