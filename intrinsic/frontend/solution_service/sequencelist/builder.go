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

package sequencelist

import (
	"context"

	log "github.com/golang/glog"
	"google.golang.org/protobuf/types/known/timestamppb"

	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
	executivelogitemspb "intrinsic/executive/proto/log_items_go_proto"
	runmetadatapb "intrinsic/executive/proto/run_metadata_go_proto"
	"intrinsic/frontend/solution_service/sequencelist/find"
	logitempb "intrinsic/logging/proto/log_item_go_proto"
)

// Action is the latest known state of a single action of a Process execution.
// An action is one execution of a task node.
type Action struct {
	// ID is the executive plan action ID of the action.
	ID uint64
	// ParentID is the ID of the action of the Process call that this action
	// runs in. It is 0 for actions that do not run inside a Process call.
	ParentID uint64
	// Name is the name of the task node.
	Name string
	// State is the latest known state of the task node.
	State btpb.BehaviorTree_TaskNode_State
	// SkillID is the ID of the called Skill or Process. Only set for task nodes
	// that call a Skill or Process.
	SkillID string
	// IsCodeExecution is true if the task node executes code.
	IsCodeExecution bool
	// Stdout is the output of the code execution. Only set for code executions.
	Stdout []string
	// StartTime is the acquisition time of the first log item seen for the
	// action.
	StartTime *timestamppb.Timestamp
	// EndTime is the acquisition time of the last log item seen for the action.
	// Only set once the action is in a final state.
	EndTime *timestamppb.Timestamp
}

type actionState struct {
	action *Action
	// lastTime is the acquisition time of the last log item seen for the
	// action.
	lastTime *timestamppb.Timestamp
	// resolved is true once the action's task node was found in a log item.
	resolved bool
}

// Builder collects actions from executive operation log items. Log items are
// added in batches in the order they were logged. The builder does not keep
// references to log items after [Builder.Add] returns.
type Builder struct {
	actions map[uint64]*actionState
	order   []uint64
	// numResolved is the number of actions in actions that are resolved.
	numResolved int
}

// NewBuilder creates an empty [Builder].
func NewBuilder() *Builder {
	return &Builder{actions: make(map[uint64]*actionState)}
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
		state, ok := b.actions[id]
		if !ok {
			state = &actionState{
				action: &Action{
					ID:        id,
					StartTime: item.GetMetadata().GetAcquisitionTime(),
				},
			}
			b.actions[id] = state
			b.order = append(b.order, id)
		}
		// The executive logs the action ID of the enclosing Process call as
		// parent_skill_id. Only non-zero values are taken so that a log item
		// without it cannot move the action to the top level.
		if parentID := item.GetContext().GetParentSkillId(); parentID != 0 {
			state.action.ParentID = parentID
		}
		state.lastTime = item.GetMetadata().GetAcquisitionTime()
		if _, ok := lastByActionID[id]; !ok {
			touched = append(touched, id)
		}
		lastByActionID[id] = item
	}

	for _, id := range touched {
		state := b.actions[id]
		// Until its task node is found, an action has no name or state, so it is
		// not returned.
		if updateFromOperation(ctx, state.action, lastByActionID[id].GetPayload().GetExecutiveOperation()) && !state.resolved {
			state.resolved = true
			b.numResolved++
		}
		// The executive logs every state change, so the last log item of a
		// finished action is the one with its final state.
		if isFinalState(state.action.State) {
			state.action.EndTime = state.lastTime
		} else {
			state.action.EndTime = nil
		}
	}
}

// Len returns the number of actions that [Builder.Actions] returns.
func (b *Builder) Len() int {
	return b.numResolved
}

// Actions returns all resolved actions in the order they were first seen.
func (b *Builder) Actions() []*Action {
	actions := make([]*Action, 0, len(b.order))
	for _, id := range b.order {
		if state := b.actions[id]; state.resolved {
			actions = append(actions, state.action)
		}
	}
	return actions
}

// updateFromOperation updates the action with details about its task node from
// the logged operation. Returns false if the task node could not be found.
func updateFromOperation(ctx context.Context, action *Action, op *executivelogitemspb.LoggedOperation) bool {
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
		log.ErrorContextf(ctx, "Failed to unmarshal run metadata for action %d: %v", action.ID, err)
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

	action.Name = node.GetName()
	action.State = task.GetState()
	action.SkillID = ""
	action.IsCodeExecution = false
	action.Stdout = nil
	switch t := task.GetTaskType().(type) {
	case *btpb.BehaviorTree_TaskNode_CallBehavior:
		action.SkillID = t.CallBehavior.GetSkillId()
	case *btpb.BehaviorTree_TaskNode_ExecuteCode:
		action.IsCodeExecution = true
		action.Stdout = t.ExecuteCode.GetStdout()
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
