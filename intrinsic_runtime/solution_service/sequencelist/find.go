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

// Package find provides utilities to find something in a Process.
package find

import (
	"context"
	"errors"

	"intrinsic/executive/go/behaviortree"
	behaviortreepb "intrinsic/executive/proto/behavior_tree_go_proto"
)

// ErrNodeNotFound is returned when the requested node was not found.
var ErrNodeNotFound = errors.New("node not found")

// NodeLocation describes the location of a node in a behavior tree.
type NodeLocation struct {
	treeID string
	nodeID uint32
}

// Node creates a [NodeLocation] that points to a node. This can be used to look
// up the node in a behavior tree using [NodeLocation.InBehaviorTree].
func Node(treeID string, nodeID uint32) NodeLocation {
	return NodeLocation{treeID, nodeID}
}

// InBehaviorTree looks for the node location in the provided behavior tree.
// Returns [ErrNodeNotFound] if the behavior tree does not contain a node with
// the location. Will traverse into `called_tree_state` on task nodes if the
// field is populated on the provided behavior tree.
func (l NodeLocation) InBehaviorTree(ctx context.Context, bt *behaviortreepb.BehaviorTree) (*behaviortreepb.BehaviorTree_Node, error) {
	visitor := &nodeFindingVisitor{
		treeID: l.treeID,
		nodeID: l.nodeID,
	}
	if err := behaviortree.Walk(ctx, bt, visitor, behaviortree.VisitCalledTreeState()); err != nil {
		return nil, err
	}
	if visitor.foundNode == nil {
		return nil, ErrNodeNotFound
	}
	return visitor.foundNode, nil
}

type nodeFindingVisitor struct {
	treeID    string
	nodeID    uint32
	foundNode *behaviortreepb.BehaviorTree_Node
}

func (v *nodeFindingVisitor) Visit(ctx context.Context, element behaviortree.VisitElement) error {
	if node := element.Node(); node != nil && node.GetId() == v.nodeID {
		for ancestor := range element.Ancestors() {
			if tree := ancestor.Tree(); tree != nil {
				if tree.GetTreeId() == v.treeID {
					v.foundNode = node
					return behaviortree.Stop
				}
				// Stop looking at more ancestors once we encounter the first tree.
				// Trees further up the ancestor chain are not relevant for matching the
				// tree ID.
				break
			}
		}
	}
	return nil
}
