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

// Package behaviortree provides utilities to operate on Behavior Trees.
//
// Features are:
// - Enables to walk and execute code for each node and condition in the tree.
package behaviortree

import (
	"context"
	"errors"
	"iter"

	"google.golang.org/protobuf/proto"

	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
)

type visitElement struct {
	msg      proto.Message
	ancestor *VisitElement
}

// VisitElement is a single element visited by the [Visitor] when using [Walk].
//
// One of:
// - [btpb.BehaviorTree]
// - [btpb.BehaviorTree_Node]
// - [btpb.BehaviorTree_Condition]
type VisitElement visitElement

// VisitElementFromTree wraps a tree into a [VisitElement].
func VisitElementFromTree(tree *btpb.BehaviorTree) VisitElement {
	return VisitElement{tree, nil}
}

// VisitElementFromNode wraps a node into a [VisitElement].
func VisitElementFromNode(node *btpb.BehaviorTree_Node) VisitElement {
	return VisitElement{node, nil}
}

// VisitElementFromCondition wraps a condition into a [VisitElement].
func VisitElementFromCondition(cond *btpb.BehaviorTree_Condition) VisitElement {
	return VisitElement{cond, nil}
}

// AsAncestorForTree returns a new visit element for the given tree with this
// element as its ancestor. Panics if the visit element is not valid (i.e.
// [VisitElement.IsValid] would return `false`).
func (v VisitElement) AsAncestorForTree(tree *btpb.BehaviorTree) VisitElement {
	if !v.IsValid() {
		panic("cannot use invalid visit element as ancestor for tree")
	}
	return VisitElement{tree, &v}
}

// AsAncestorForNode returns a new visit element for the given node with this
// element as its ancestor. Panics if the visit element is not valid (i.e.
// [VisitElement.IsValid] would return `false`).
func (v VisitElement) AsAncestorForNode(node *btpb.BehaviorTree_Node) VisitElement {
	if !v.IsValid() {
		panic("cannot use invalid visit element as ancestor for node")
	}
	return VisitElement{node, &v}
}

// AsAncestorForCondition returns a new visit element for the given condition
// with this element as its ancestor. Panics if the visit element is not valid
// (i.e. [VisitElement.IsValid] would return `false`).
func (v VisitElement) AsAncestorForCondition(cond *btpb.BehaviorTree_Condition) VisitElement {
	if !v.IsValid() {
		panic("cannot use invalid visit element as ancestor for condition")
	}
	return VisitElement{cond, &v}
}

// IsValid returns whether the element is valid. The element is considered valid
// if it holds a tree, node or condition.
func (v VisitElement) IsValid() bool {
	return v.Tree() != nil || v.Node() != nil || v.Condition() != nil
}

// Tree returns the tree if the element represents one or nil otherwise.
func (v VisitElement) Tree() *btpb.BehaviorTree {
	if tree, ok := v.msg.(*btpb.BehaviorTree); ok {
		return tree
	}
	return nil
}

// Node returns the node if the element represents one or nil otherwise.
func (v VisitElement) Node() *btpb.BehaviorTree_Node {
	if node, ok := v.msg.(*btpb.BehaviorTree_Node); ok {
		return node
	}
	return nil
}

// Condition returns the condition if the element represents one or nil
// otherwise.
func (v VisitElement) Condition() *btpb.BehaviorTree_Condition {
	if cond, ok := v.msg.(*btpb.BehaviorTree_Condition); ok {
		return cond
	}
	return nil
}

// Ancestor returns the direct ancestor of the element. Returns `nil` if the
// element does not have any ancestor.
func (v VisitElement) Ancestor() *VisitElement {
	return v.ancestor
}

// Ancestors returns an iterator over the ancestors of the element. Yields items
// in order from the direct anecestor of the element up to the root element.
// Does not yield elements that are not valid (i.e. [VisitElement.IsValid]
// returns `false`).
func (v VisitElement) Ancestors() iter.Seq[VisitElement] {
	return func(yield func(VisitElement) bool) {
		ancestor := v.ancestor
		for ancestor != nil {
			if !ancestor.IsValid() {
				return
			}
			if !yield(*ancestor) {
				return
			}
			ancestor = ancestor.ancestor
		}
	}
}

// Ancestors returns an iterator over the ancestors of the element. Yields items
// in order from the direct anecestor of the element up to the root element.
// Does not yield elements that are not valid (i.e. [VisitElement.IsValid]
// returns `false`).
func (v VisitElement) AncestorsFromRoot() iter.Seq[VisitElement] {
	return func(yield func(VisitElement) bool) {
		ancestor := v.ancestor
		for ancestor != nil {
			if !ancestor.IsValid() {
				return
			}
			if !yield(*ancestor) {
				return
			}
			ancestor = ancestor.ancestor
		}
	}
}

// Stop causes visiting to stop immediately. [Walk] will not return this error.
var Stop = errors.New("stop visiting")

// Visitor defines requirements for visitor implementations for the BehaviorTree
// proto walker. Use it with [Walk] to visit all nodes in a behavior tree.
type Visitor interface {
	// Visit each element in the tree. If this returns any non-nil error (other
	// than [Stop]), it stops visiting and the error is returned by [Walk].
	Visit(ctx context.Context, element VisitElement) error
}

type options struct {
	visitCalledTreeState bool
}

// Option enables customizing the behavior of [Walk].
type Option func(*options)

// VisitCalledTreeState is an option that makes [Walk] traverse into tree in the
// `called_tree_state` field of task nodes when present.
func VisitCalledTreeState() Option {
	return func(o *options) {
		o.visitCalledTreeState = true
	}
}

func walkCondition(ctx context.Context, ancestor VisitElement, cond *btpb.BehaviorTree_Condition, visitor Visitor, opts *options) error {
	if cond == nil {
		return nil
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	element := ancestor.AsAncestorForCondition(cond)
	if err := visitor.Visit(ctx, element); err != nil {
		return err
	}
	switch cond.ConditionType.(type) {
	case *btpb.BehaviorTree_Condition_BehaviorTree:
		err := walkTree(ctx, element, cond.GetBehaviorTree(), visitor, opts)
		if err != nil {
			return err
		}

	case *btpb.BehaviorTree_Condition_AllOf:
		for _, c := range cond.GetAllOf().GetConditions() {
			if err := walkCondition(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Condition_AnyOf:
		for _, c := range cond.GetAnyOf().GetConditions() {
			if err := walkCondition(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Condition_Not:
		if err := walkCondition(ctx, element, cond.GetNot(), visitor, opts); err != nil {
			return err
		}
	}

	return nil
}

func walkNode(ctx context.Context, ancestor VisitElement, node *btpb.BehaviorTree_Node, visitor Visitor, opts *options) error {
	if node == nil {
		return nil
	}
	if err := ctx.Err(); err != nil {
		return err
	}
	element := ancestor.AsAncestorForNode(node)
	if err := visitor.Visit(ctx, element); err != nil {
		return err
	}

	if node.GetDecorators().GetCondition() != nil {
		if err := walkCondition(ctx, element, node.GetDecorators().GetCondition(), visitor, opts); err != nil {
			return err
		}
	}

	switch node.NodeType.(type) {
	case *btpb.BehaviorTree_Node_Sequence:
		for _, c := range node.GetSequence().GetChildren() {
			if err := walkNode(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Node_Parallel:
		for _, c := range node.GetParallel().GetChildren() {
			if err := walkNode(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Node_Selector:
		for _, c := range node.GetSelector().GetChildren() {
			if err := walkNode(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}
		for _, c := range node.GetSelector().GetBranches() {
			if err := walkCondition(ctx, element, c.GetCondition(), visitor, opts); err != nil {
				return err
			}
			if err := walkNode(ctx, element, c.GetNode(), visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Node_Fallback:
		for _, c := range node.GetFallback().GetChildren() {
			if err := walkNode(ctx, element, c, visitor, opts); err != nil {
				return err
			}
		}
		for _, c := range node.GetFallback().GetTries() {
			if err := walkCondition(ctx, element, c.GetCondition(), visitor, opts); err != nil {
				return err
			}
			if err := walkNode(ctx, element, c.GetNode(), visitor, opts); err != nil {
				return err
			}
		}

	case *btpb.BehaviorTree_Node_Branch:
		if err := walkCondition(ctx, element, node.GetBranch().GetIf(), visitor, opts); err != nil {
			return err
		}
		if err := walkNode(ctx, element, node.GetBranch().GetThen(), visitor, opts); err != nil {
			return err
		}
		if err := walkNode(ctx, element, node.GetBranch().GetElse(), visitor, opts); err != nil {
			return err
		}

	case *btpb.BehaviorTree_Node_Loop:
		if err := walkCondition(ctx, element, node.GetLoop().GetWhile(), visitor, opts); err != nil {
			return err
		}
		if err := walkNode(ctx, element, node.GetLoop().GetDo(), visitor, opts); err != nil {
			return err
		}

	case *btpb.BehaviorTree_Node_Retry:
		if err := walkNode(ctx, element, node.GetRetry().GetChild(), visitor, opts); err != nil {
			return err
		}
		if err := walkNode(ctx, element, node.GetRetry().GetRecovery(), visitor, opts); err != nil {
			return err
		}

	case *btpb.BehaviorTree_Node_SubTree:
		if err := walkTree(ctx, element, node.GetSubTree().GetTree(), visitor, opts); err != nil {
			return err
		}

	case *btpb.BehaviorTree_Node_Task:
		if opts.visitCalledTreeState {
			if called := node.GetTask().GetCalledTreeState(); called != nil {
				// Trees in called tree state are relevant for finding a node by tree ID.
				// They are therefore considered a new enclosing tree.
				if err := walkTree(ctx, element, called, visitor, opts); err != nil {
					return err
				}
			}
		}
	}

	return nil
}

func walkTree(ctx context.Context, ancestor VisitElement, tree *btpb.BehaviorTree, visitor Visitor, opts *options) error {
	var element VisitElement
	if ancestor.IsValid() {
		element = ancestor.AsAncestorForTree(tree)
	} else {
		element = VisitElementFromTree(tree)
	}
	if err := visitor.Visit(ctx, element); err != nil {
		return err
	}
	return walkNode(ctx, element, tree.Root, visitor, opts)
}

// Walk walks the given Behavior Tree and invokes the given visitor for nodes
// and conditions of the tree.
func Walk(ctx context.Context, tree *btpb.BehaviorTree, visitor Visitor, opts ...Option) error {
	options := &options{}
	for _, opt := range opts {
		opt(options)
	}
	err := walkTree(ctx, VisitElement{}, tree, visitor, options)
	if err == Stop {
		err = nil
	}
	return err
}
