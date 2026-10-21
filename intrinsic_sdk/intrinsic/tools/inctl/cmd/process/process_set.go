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
	"io/ioutil"

	protoregistrypb "intrinsic/proto_tools/proto/proto_registry_go_proto"
	"intrinsic/proto_tools/registry/protoregistryclient"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	emptypb "google.golang.org/protobuf/types/known/emptypb"

	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
	execgrpcpb "intrinsic/executive/proto/executive_service_go_proto"
	sgrpcpb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	spb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
)

var viperProcessSet = viper.New()

// resolverToEmpty is a dummy implementation of prototext.UnmarshalOptions.Resolver that always
// returns the Empty message type for any type name or type URL.
type resolverToEmpty struct {
	empty *emptypb.Empty
}

func newResolverToEmpty() *resolverToEmpty {
	return &resolverToEmpty{empty: &emptypb.Empty{}}
}

func (d *resolverToEmpty) FindMessageByName(message protoreflect.FullName) (protoreflect.MessageType, error) {
	return d.empty.ProtoReflect().Type(), nil
}

func (d *resolverToEmpty) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	return d.empty.ProtoReflect().Type(), nil
}

func (d *resolverToEmpty) FindExtensionByName(field protoreflect.FullName) (protoreflect.ExtensionType, error) {
	return nil, errors.New("dummyResolver.FindExtensionByName is not implemented")
}

func (d *resolverToEmpty) FindExtensionByNumber(message protoreflect.FullName, field protoreflect.FieldNumber) (protoreflect.ExtensionType, error) {
	return nil, errors.New("dummyResolver.FindExtensionByNumber is not implemented")
}

func deserializeFromText(ctx context.Context, content []byte, protoRegistry protoregistrypb.ProtoRegistryClient) (*btpb.BehaviorTree, error) {
	// To unmarshal expanded Any protos from script node parameters in the given
	// behavior tree correctly, we need all the file descriptor sets from the
	// behavior tree. But to get the file descriptor sets from the behavior tree,
	// we need to unmarshal it first. We solve this by unmarshalling the behavior
	// tree in two passes.
	//
	// Pass 1: Unmarshal with a dummy resolver. All expanded Any protos are
	// unmarshalled to empty messages (more precisely, to Any protos with a
	// correct 'type_url' and empty 'data') but the file descriptor sets in the
	// behavior tree are unmarshalled correctly.
	emptyUnmarshaller := prototext.UnmarshalOptions{
		Resolver:     newResolverToEmpty(),
		AllowPartial: true,
		// Unmarshal any expanded Any proto to an Empty proto without errors. Since
		// Empty has no fields, all parsed fields are treated as unknown fields and
		// will simply be discarded.
		DiscardUnknown: true,
	}

	btWithEmptyAnys := &btpb.BehaviorTree{}
	if err := emptyUnmarshaller.Unmarshal(content, btWithEmptyAnys); err != nil {
		return nil, errors.Wrapf(err, "could not parse input file in first pass")
	}

	nodeTypes, err := MergedTypesForAllScriptNodesInTree(ctx, btWithEmptyAnys)
	if err != nil {
		return nil, errors.Wrap(err, "failed creating merged Types from behavior tree script nodes")
	}

	// Pass 2: Unmarshal while resolving Intrinsic type URLs using the proto
	// registry and other type URLs using the descriptors collected from the
	// behavior tree (with the compiled-in types as a last fallback).
	unmarshaller := prototext.UnmarshalOptions{
		Resolver: protoregistryclient.NewProtoRegistryResolver(
			ctx,
			protoRegistry,
			[]protoregistryclient.Resolver{nodeTypes, protoregistry.GlobalTypes},
		),
		AllowPartial:   true,
		DiscardUnknown: true,
	}

	bt := &btpb.BehaviorTree{}
	if err := unmarshaller.Unmarshal(content, bt); err != nil {
		return nil, errors.Wrapf(err, "could not parse input file in second pass")
	}

	return bt, nil
}

func deserializeFromBinary(ctx context.Context, content []byte) (*btpb.BehaviorTree, error) {
	bt := &btpb.BehaviorTree{}
	if err := proto.Unmarshal(content, bt); err != nil {
		return nil, errors.Wrapf(err, "could not parse input file")
	}
	return bt, nil
}

type setProcessParams struct {
	executive       execgrpcpb.ExecutiveServiceClient
	solutionService sgrpcpb.SolutionServiceClient
	protoRegistry   protoregistrypb.ProtoRegistryClient
	name            string
	format          string
	content         []byte
	clearTreeID     bool
	clearNodeIDs    bool
}

func deserializeBT(ctx context.Context, format string, content []byte, protoRegistry protoregistrypb.ProtoRegistryClient) (*btpb.BehaviorTree, error) {
	var err error
	var bt *btpb.BehaviorTree

	switch format {
	case TextProtoFormat:
		bt, err = deserializeFromText(ctx, content, protoRegistry)
		if err != nil {
			return nil, errors.Wrapf(err, "could not deserialize BT from text")
		}
	case BinaryProtoFormat:
		bt, err = deserializeFromBinary(ctx, content)
		if err != nil {
			return nil, errors.Wrapf(err, "could not deserialize BT from binary")
		}
	default:
		return nil, fmt.Errorf("unknown format %s", format)
	}

	return bt, nil
}

func setProcess(ctx context.Context, params *setProcessParams) error {
	bt, err := deserializeBT(ctx, params.format, params.content, params.protoRegistry)
	if err != nil {
		return errors.Wrapf(err, "could not deserialize BT")
	}

	clearTree(bt, params.clearTreeID, params.clearNodeIDs)

	if params.name == "" {
		if err := setBT(ctx, params.executive, bt); err != nil {
			return errors.Wrapf(err, "could not set active behavior tree")
		}
	} else {
		if _, err := params.solutionService.CreateBehaviorTree(ctx, &spb.CreateBehaviorTreeRequest{
			BehaviorTreeId: params.name,
			BehaviorTree:   bt,
		}); err != nil {
			return errors.Wrapf(err, "could not create behavior tree in the solution")
		}
	}

	return nil
}

var processSetCmd = orgutil.WrapCmd(
	&cobra.Command{
		Use:   "set [legacy process name]",
		Short: "Set a process in a solution.",
		Long: `Set a process in a currently deployed solution.

The input is a intrinsic_proto.executive.BehaviorTree proto in binary or text format.

No positional argument: Create a new operation with the input process in the executive.
$ inctl process set --org my_org --solution my_solution_id --input_file /tmp/my-process.textproto [--process_format textproto|binaryproto]

[legacy process support (deprecated)]
One positional argument: Store the input process as a legacy process with the given name in the solution. Afterwards, the process can be loaded in the Flowstate frontend and then be executed from there.
$ inctl process set --org my_org --solution my_solution_id --input_file /tmp/my-process.textproto [--process_format textproto|binaryproto] "My Process"

Deprecated: Migrate your Process to a Process Asset instead:
1. Ensure the "name" field in your BehaviorTree proto matches your desired display name (e.g. name: "My Process").
2. Create a ProcessManifest textproto (e.g. my_process_manifest.txtpb) with ProcessMetadata:
   # proto-file: intrinsic/assets/processes/proto/process_manifest.proto
   # proto-message: intrinsic_proto.processes.ProcessManifest
   metadata {
     id {
       package: "com.example"
       name: "my_process"
     }
     display_name: "My Process"
     vendor {
       display_name: "Example"
     }
     documentation {
       description: "My Process description."
     }
   }
3. Add an intrinsic_process target in your BUILD file to build a Process Asset bundle (.bundle.tar):
   load("@ai_intrinsic_sdks//intrinsic/assets/processes/build_defs:processes.bzl", "intrinsic_process")

   intrinsic_process(
       name = "my_process",
       behavior_tree = ":my-process.textproto",
       manifest = ":my_process_manifest.txtpb",
   )
4. Build the target and install the bundle with "inctl asset install":
   $ bazel build //path/to:my_process
   $ inctl asset install bazel-bin/path/to/my_process.bundle.tar --org my_org --solution my_solution_id

Alternatively, to migrate an existing legacy Process in a Solution:
- In Flowstate: Open the Process, set its Process Asset metadata (ID and vendor) in the details panel, and save it.
- In Python: Load the Process, call bt.set_asset_metadata(id="com.example.my_process", vendor="Example"), and call solution.processes.save(bt).`,
		Args: cobra.RangeArgs(0, 1),
		RunE: func(cmd *cobra.Command, args []string) error {
			if flagInputFile == "" {
				return fmt.Errorf("--input_file must be specified")
			}

			name := ""
			if len(args) == 1 {
				name = args[0]
				color.C.Yellow().Fprintf(
					cmd.ErrOrStderr(),
					"WARNING: Setting a process with a name writes a legacy Process and is deprecated. Migrate your Process to a Process Asset instead (run \"inctl process set --help\" for migration instructions).\n",
				)
			}
			projectName := viperProcessSet.GetString(orgutil.KeyProject)
			orgName := viperProcessSet.GetString(orgutil.KeyOrganization)
			ctx, conn, err := connectToCluster(cmd.Context(), projectName,
				orgName, flagServerAddress,
				flagSolutionName, flagClusterName)
			if err != nil {
				return errors.Wrapf(err, "could not dial connection")
			}
			defer conn.Close()

			content, err := ioutil.ReadFile(flagInputFile)
			if err != nil {
				return errors.Wrapf(err, "could not read input file")
			}

			if err = setProcess(ctx, &setProcessParams{
				executive:       execgrpcpb.NewExecutiveServiceClient(conn),
				solutionService: sgrpcpb.NewSolutionServiceClient(conn),
				protoRegistry:   protoregistrypb.NewProtoRegistryClient(conn),
				content:         content,
				name:            name,
				format:          flagProcessFormat,
				clearTreeID:     flagClearTreeID,
				clearNodeIDs:    flagClearNodeIDs,
			}); err != nil {
				return errors.Wrapf(err, "could not set BT")
			}

			if name == "" {
				fmt.Println("BT loaded successfully to the executive. To edit behavior tree in the frontend: Process -> Load -> From executive")
			} else {
				fmt.Println("BT added to the solution. To edit and execute the process in the frontend: Process -> Load -> <process name>")
			}

			return nil
		},
	},
	viperProcessSet,
)

func init() {
	addCommonGetSetFlags(processSetCmd)
	processSetCmd.Flags().StringVar(&flagInputFile, "input_file", "", "File from which to read the process.")
}
