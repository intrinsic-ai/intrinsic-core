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

// Package process contains all commands for handling processes (behavior trees).
package process

import (
	"context"
	"fmt"
	"os"
	"strings"

	"intrinsic/executive/go/behaviortree"
	behaviortreepb "intrinsic/executive/proto/behavior_tree_go_proto"
	executiveservicepb "intrinsic/executive/proto/executive_service_go_proto"
	runmetadatapb "intrinsic/executive/proto/run_metadata_go_proto"
	"intrinsic/skills/tools/skill/cmd/dialerutil"
	"intrinsic/skills/tools/skill/cmd/solutionutil"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/util/proto/fieldbehavior"
	"intrinsic/util/proto/registryutil"

	longrunningpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

const (
	keyFilter = "filter"
)

const (
	// TextProtoFormat is the textproto output format.
	TextProtoFormat = "textproto"
	// BinaryProtoFormat is the binary proto output format.
	BinaryProtoFormat = "binaryproto"
)

var (
	flagServerAddress string
	flagSolutionName  string
	flagClusterName   string
	flagInputFile     string
	flagOutputFile    string
	flagClearTreeID   bool
	flagClearNodeIDs  bool
	flagProcessFormat string

	flagClearElementIDs bool

)

type idCleaner struct {
	treeIDs bool
	nodeIDs bool

	elementIDs bool

}

func (c *idCleaner) Visit(ctx context.Context, element behaviortree.VisitElement) error {
	if tree := element.Tree(); tree != nil && c.treeIDs {
		tree.TreeId = nil
	}
	if node := element.Node(); node != nil && c.nodeIDs {
		node.Id = nil
	}

	if c.elementIDs {
		if tree := element.Tree(); tree != nil {
			tree.ElementId = nil
		}
		if node := element.Node(); node != nil {
			node.ElementId = nil
		}
		if cond := element.Condition(); cond != nil {
			cond.ElementId = nil
		}
	}

	return nil
}

func clearTree(
	m proto.Message,
	clearTreeID bool,
	clearNodeIDs bool,
	clearElementIDs bool, 
) error {
	if bt, ok := m.(*behaviortreepb.BehaviorTree); ok && bt != nil {
		cleaner := &idCleaner{treeIDs: clearTreeID, nodeIDs: clearNodeIDs}

		cleaner.elementIDs = clearElementIDs

		if err := behaviortree.Walk(context.Background(), bt, cleaner); err != nil {
			return err
		}
	}
	return fieldbehavior.ClearOutputOnly(m)
}

func validateTargetFlags(address string, clusterName string, solutionName string) error {
	if address == "" && clusterName == "" && solutionName == "" {
		return fmt.Errorf("target workcell not specified: provide `--server` for a local/direct workcell (e.g. `--server=localhost:17080`), or `--org` with `--cluster` or `--solution` for a cloud workcell")
	}
	if clusterName != "" && solutionName != "" {
		return fmt.Errorf("both `--cluster=%q` and `--solution=%q` were provided, which could be ambiguous", clusterName, solutionName)
	}
	return nil
}

func connectToCluster(ctx context.Context, projectName string, orgName string, address string, solutionName string, clusterName string) (context.Context, *grpc.ClientConn, error) {
	if err := validateTargetFlags(address, clusterName, solutionName); err != nil {
		return nil, nil, err
	}
	if solutionName != "" {
		// Look up solution name via cloud portal.
		ctx, conn, err := dialerutil.DialConnectionCtx(ctx, dialerutil.DialInfoParams{
			CredName: projectName,
			CredOrg:  orgName,
		})
		if err != nil {
			return nil, nil, fmt.Errorf("failed to create client connection: %w", err)
		}

		clusterName, err = solutionutil.GetClusterNameFromSolution(ctx, conn, solutionName)
		if err != nil {
			return nil, nil, errors.Wrapf(err, "could not resolve solution to cluster")
		}
	}

	// Establish a gRPC connection to server, cluster, or cloud.
	ctx, conn, err := dialerutil.DialConnectionCtx(ctx, dialerutil.DialInfoParams{
		Address:  address,
		Cluster:  clusterName,
		CredName: projectName,
		CredOrg:  orgName,
	})
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create client connection: %w", err)
	}

	return ctx, conn, nil
}

func getActiveBT(ctx context.Context, exC executiveservicepb.ExecutiveServiceClient) (*behaviortreepb.BehaviorTree, error) {
	listOpResp, err := exC.ListOperations(ctx, &longrunningpb.ListOperationsRequest{})
	if err != nil {
		return nil, errors.Wrap(err, "unable to list executive operations")
	}

	if len(listOpResp.Operations) == 0 {
		return nil, fmt.Errorf("no operations found. Did you load a behavior tree into the executive?")
	}

	if len(listOpResp.Operations) > 1 {
		fmt.Fprintf(os.Stderr, "Found %d concurrent operations, getting first one", len(listOpResp.Operations))
	}
	operation := listOpResp.Operations[0]

	metadata := new(runmetadatapb.RunMetadata)
	if err := operation.GetMetadata().UnmarshalTo(metadata); err != nil {
		return nil, errors.Wrap(err, "unable to unmarshal RunMetadata proto")
	}

	return metadata.GetBehaviorTree(), nil
}

func setBT(ctx context.Context, exC executiveservicepb.ExecutiveServiceClient, bt *behaviortreepb.BehaviorTree) error {
	listOpResp, err := exC.ListOperations(ctx, &longrunningpb.ListOperationsRequest{})
	if err != nil {
		return errors.Wrap(err, "unable to list executive operations")
	}

	if len(listOpResp.Operations) > 1 {
		return errors.Errorf("More than one concurrently loaded BT/executive operation, please delete all but one")
	}

	if len(listOpResp.Operations) == 1 {
		operationToDelete := listOpResp.Operations[0]
		if _, err = exC.DeleteOperation(ctx, &longrunningpb.DeleteOperationRequest{
			Name: operationToDelete.Name,
		}); err != nil {
			return errors.Wrap(err, "unable to delete operation")
		}
	}

	req := &executiveservicepb.CreateOperationRequest{}
	req.RunnableType = &executiveservicepb.CreateOperationRequest_BehaviorTree{BehaviorTree: bt}

	if _, err = exC.CreateOperation(ctx, req); err != nil {
		return errors.Wrap(err, "unable to create executive operation")
	}

	return nil
}

// fileDescriptorSetCollector is a behavior tree visitor that collects all file descriptor sets in a
// behavior tree.
type fileDescriptorSetCollector struct {
	fileDescriptorSets []*descriptorpb.FileDescriptorSet
}

func (c *fileDescriptorSetCollector) Visit(ctx context.Context, element behaviortree.VisitElement) error {
	if node := element.Node(); node != nil {
		fileDescriptorSet := node.GetTask().GetExecuteCode().GetFileDescriptorSet()
		if fileDescriptorSet != nil {
			c.fileDescriptorSets = append(c.fileDescriptorSets, fileDescriptorSet)
		}
	}
	return nil
}

func addAllFilesToFiles(dst *protoregistry.Files, files *protoregistry.Files) error {
	var err error
	files.RangeFiles(func(file protoreflect.FileDescriptor) bool {
		// Add file if not already present. The error returned by RegisterFile() below cannot easily be
		// classified into "not found" vs "other error". So we check for the file's presence first using
		// FindFileByPath() which does return a specific error for "not found".
		fileExists := true
		_, err = dst.FindFileByPath(file.Path())
		if errors.Is(err, protoregistry.NotFound) {
			fileExists = false
		} else if err != nil {
			err = errors.Wrap(err, "failed finding file by path")
			return false
		}

		if !fileExists {
			if err = dst.RegisterFile(file); err != nil {
				err = errors.Wrap(err, "failed registering file")
				return false
			}
		}
		return true
	})
	return err
}

// Creates a merged pool of the file descriptor sets of all script nodes in
// the given behavior tree.
//
// This is required until the parameter Any protos of script nodes in behavior
// trees have Intrinsic type URLs and are fully supported by the proto registry.
func MergedTypesForAllScriptNodesInTree(ctx context.Context, bt *behaviortreepb.BehaviorTree) (*protoregistry.Types, error) {
	collector := fileDescriptorSetCollector{}
	if err := behaviortree.Walk(ctx, bt, &collector); err != nil {
		return nil, errors.Wrap(err, "failed walking behavior tree")
	}

	files := new(protoregistry.Files)
	for _, fileDescriptorSet := range collector.fileDescriptorSets {
		setFiles, err := protodesc.NewFiles(fileDescriptorSet)
		if err != nil {
			return nil, errors.Wrap(err, "failed creating files from file descriptor set")
		}
		if err := addAllFilesToFiles(files, setFiles); err != nil {
			return nil, errors.Wrap(err, "failed adding file descriptor set files to files")
		}
	}

	types := new(protoregistry.Types)
	if err := registryutil.PopulateTypesFromFiles(types, files); err != nil {
		return nil, errors.Wrapf(err, "failed to populate types from files")
	}

	return types, nil
}

func addCommonGetSetFlags(cmd *cobra.Command) {
	allowedFormats := []string{TextProtoFormat, BinaryProtoFormat}
	cmd.Flags().StringVar(
		&flagProcessFormat, "process_format", TextProtoFormat,
		fmt.Sprintf("(optional) input/output format. One of: (%s)", strings.Join(allowedFormats, ", ")))
	cmd.Flags().StringVar(&flagSolutionName, "solution", "", "Id of the solution to interact with. For example, use 'inctl solutions list --org my_org --output json [--filter running_in_sim]' to see the list of solutions.")
	cmd.Flags().StringVar(&flagClusterName, "cluster", "", "Name of the cluster to interact with.")
	cmd.Flags().StringVar(&flagServerAddress, "server", "", "Server address of the cluster. Format is {ADDRESS}:{PORT}, for example 'localhost:17080'")
	cmd.MarkFlagsMutuallyExclusive("cluster", "solution")
	cmd.Flags().BoolVar(&flagClearTreeID, "clear_tree_id", true, "Clear the tree_id fields of the tree and all of its sub-trees from the BT proto.")
	cmd.Flags().BoolVar(&flagClearNodeIDs, "clear_node_ids", true, "Clear the nodes' id fields from the BT proto.")

	cmd.Flags().BoolVar(&flagClearElementIDs, "clear_element_ids", true, "Clear the element_id fields of all trees, nodes and conditions from the BT proto.")

}

var processCmd = cobrautil.ParentOfNestedSubcommands(
	root.ProcessCmdName,
	"Interacts with processes (behavior trees)",
)

func init() {
	processCmd.AddCommand(processGetCmd)
	processCmd.AddCommand(processSetCmd)
	root.RootCmd.AddCommand(processCmd)
}
