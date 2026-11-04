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

// Package sequencelist implements tooling to generate a sequence list from log
// items of a Process execution.
package sequencelist

import (
	"context"

	log "github.com/golang/glog"
	"google.golang.org/protobuf/types/known/timestamppb"

	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
	executivelogitemspb "intrinsic/executive/proto/log_items_go_proto"
	runmetadatapb "intrinsic/executive/proto/run_metadata_go_proto"
	slpb "intrinsic/frontend/solution_service/proto/sequence_list_go_proto"
	"intrinsic/frontend/solution_service/sequencelist/find"
	logitempb "intrinsic/logging/proto/log_item_go_proto"
)

type itemState struct {
	item *slpb.SequenceListItem
	// lastTime is the acquisition time of the last log item seen for the
	// action.
	lastTime *timestamppb.Timestamp
	// resolved is true once the action's task node was found in a log item.
	resolved bool
}

// Builder collects sequence list items from executive operation log items.
// Each item is one action of a Process execution, i.e., one execution of a
// task node. Log items are added in batches in the order they were logged. The
// builder does not keep references to log items after [Builder.Add] returns.
type Builder struct {
	items map[uint64]*itemState
	order []uint64
	// numResolved is the number of resolved entries in items.
	numResolved int
}

// NewBuilder creates an empty [Builder].
func NewBuilder() *Builder {
	return &Builder{items: make(map[uint64]*itemState)}
}

// Add adds a batch of log items. Log items that are not associated with an
// action are ignored. The details of each action are taken from the last log
// item for the action in the batch.
func (b *Builder) Add(ctx context.Context, items []*logitempb.LogItem) {
	// The last log item per action in this batch. Only these are unpacked since
	// each log item contains the full Process state, which can be large.
	lastByActionID := make(map[uint64]*logitempb.LogItem)
	// Action IDs in the order they first appear in this batch.
	var touched []uint64
	for _, item := range items {
		id := item.GetContext().GetExecutivePlanActionId()
		if id == 0 {
			// For example log items about the operation as a whole.
			continue
		}
		state, ok := b.items[id]
		if !ok {
			state = &itemState{
				item: &slpb.SequenceListItem{
					Id:        id,
					StartTime: item.GetMetadata().GetAcquisitionTime(),
				},
			}
			b.items[id] = state
			b.order = append(b.order, id)
		}
		// The executive logs the action ID of the enclosing Process call as
		// parent_skill_id. Only non-zero values are taken so that a log item
		// without it cannot move the action to the top level.
		if parentID := item.GetContext().GetParentSkillId(); parentID != 0 {
			state.item.ParentId = parentID
		}
		state.lastTime = item.GetMetadata().GetAcquisitionTime()
		if _, ok := lastByActionID[id]; !ok {
			touched = append(touched, id)
		}
		lastByActionID[id] = item
	}

	for _, id := range touched {
		state := b.items[id]
		// Until its task node is found, an action has no name or state, so its
		// item is not returned.
		if updateFromOperation(ctx, state.item, lastByActionID[id].GetPayload().GetExecutiveOperation()) && !state.resolved {
			state.resolved = true
			b.numResolved++
		}
		// The executive logs every state change, so the last log item of a
		// finished action is the one with its final state.
		if isFinalState(state.item.GetState()) {
			state.item.EndTime = state.lastTime
		} else {
			state.item.EndTime = nil
		}
	}
}

// Len returns the number of items that [Builder.Items] returns.
func (b *Builder) Len() int {
	return b.numResolved
}

// Items returns the items of all resolved actions in the order they were first
// seen.
func (b *Builder) Items() []*slpb.SequenceListItem {
	items := make([]*slpb.SequenceListItem, 0, b.numResolved)
	for _, id := range b.order {
		if state := b.items[id]; state.resolved {
			items = append(items, state.item)
		}
	}
	return items
}

// updateFromOperation updates the item with details about its task node from
// the logged operation. Returns false if the task node could not be found.
func updateFromOperation(ctx context.Context, item *slpb.SequenceListItem, op *executivelogitemspb.LoggedOperation) bool {
	nodeID := op.GetBehaviorTreeContext().GetActionNodeIdentifier()
	if nodeID == nil {
		return false
	}
	metadata := op.GetOperation().GetMetadata()
	if metadata == nil {
		return false
	}
	var runMetadata runmetadatapb.RunMetadata
	if err := metadata.UnmarshalTo(&runMetadata); err != nil {
		log.ErrorContextf(ctx, "Failed to unmarshal run metadata for action %d: %v", item.GetId(), err)
		return false
	}
	// The run metadata holds the state of the whole Process at the time of the
	// log item, including the trees of Process calls. The task node in it has
	// the state of the action that the log item belongs to.
	bt := runMetadata.GetBehaviorTree()
	if bt == nil {
		return false
	}
	node, err := find.Node(nodeID.GetTreeId(), nodeID.GetNodeId()).InBehaviorTree(ctx, bt)
	if err != nil {
		return false
	}
	task := node.GetTask()
	if task == nil {
		return false
	}

	item.Name = node.GetName()
	item.State = task.GetState()
	item.Details = nil
	switch t := task.GetTaskType().(type) {
	case *btpb.BehaviorTree_TaskNode_CallBehavior:
		item.Details = &slpb.SequenceListItem_SkillDetails{
			SkillDetails: &slpb.SkillDetails{SkillId: t.CallBehavior.GetSkillId()},
		}
	case *btpb.BehaviorTree_TaskNode_ExecuteCode:
		item.Details = &slpb.SequenceListItem_CodeExecutionDetails{
			CodeExecutionDetails: &slpb.CodeExecutionDetails{Stdout: t.ExecuteCode.GetStdout()},
		}
	}
	return true
}

// isFinalState returns whether an action in the state has finished. States such
// as PROJECTED come before the action runs.
func isFinalState(state btpb.BehaviorTree_TaskNode_State) bool {
	switch state {
	case btpb.BehaviorTree_TaskNode_SUCCEEDED,
		btpb.BehaviorTree_TaskNode_FAILED,
		btpb.BehaviorTree_TaskNode_CANCELED:
		return true
	default:
		return false
	}
}
