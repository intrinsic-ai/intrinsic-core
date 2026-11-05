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

#include "intrinsic/scene/scene_object/scene_object_view.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace scene_object {

absl::StatusOr<SceneObjectView> SceneObjectView::Create(
    SceneObject scene_object_value) {
  INTR_RETURN_IF_ERROR(ValidateSceneObject(scene_object_value));

  SceneObjectView view(
      std::make_unique<const SceneObject>(std::move(scene_object_value)));
  const SceneObject& scene_object = *view.scene_object_;
  const int num_entities = scene_object.entities_size();

  view.nodes_.resize(num_entities);
  view.index_by_name_.reserve(num_entities);
  for (int i = 0; i < num_entities; ++i) {
    const Entity& entity = scene_object.entities(i);
    view.nodes_[i].entity = &entity;
    view.index_by_name_.emplace(entity.name(), i);
  }

  // Validation guarantees a single root and that all parents exist.
  //
  // `children_end` temporarily holds the number of children of each node; it
  // is turned into a proper slice bound below.
  int root_index = -1;
  for (int i = 0; i < num_entities; ++i) {
    const absl::string_view parent_name = view.nodes_[i].entity->parent_name();
    if (parent_name.empty()) {
      root_index = i;
      continue;
    }
    const int parent_index = view.index_by_name_.at(parent_name);
    view.nodes_[i].parent_index = parent_index;
    ++view.nodes_[parent_index].children_end;
  }
  view.root_ = view.nodes_[root_index].entity;

  // Computes contiguous child slices in `children_` (counting sort by parent).
  //
  // The exclusive prefix sum over the child counts gives the start of each
  // node's slice. `children_end` is reset to the slice start so that it serves
  // as the write cursor of the slice in the next loop.
  int offset = 0;
  for (Node& node : view.nodes_) {
    const int num_children = node.children_end;
    node.children_begin = offset;
    node.children_end = offset;
    offset += num_children;
  }
  // Places each node index into its parent's slice, in proto order. Advancing
  // the write cursor of every slice once per child leaves `children_end` at
  // `children_begin + num_children`, i.e., the final half-open slice bound.
  // Every entity except the root is the child of exactly one node, so the
  // slices exactly fill the `num_entities - 1` entries (validation guarantees
  // `num_entities >= 1`).
  std::vector<int> child_indices(num_entities - 1);
  for (int i = 0; i < num_entities; ++i) {
    const int parent_index = view.nodes_[i].parent_index;
    if (parent_index >= 0) {
      child_indices[view.nodes_[parent_index].children_end++] = i;
    }
  }
  // Depth-first pre-order traversal. Uses an explicit stack to support deep
  // entity trees. The stack holds node indices still to be visited in reverse
  // visiting order, interleaved with exit markers `~idx` (negative) that are
  // popped once the whole subtree of `idx` has been emitted. On visiting a
  // node, its children are sorted by entity name and written to `children_`.
  view.children_.resize(num_entities - 1);
  view.tree_order_.reserve(num_entities);
  const absl::Span<int> child_indices_span = absl::MakeSpan(child_indices);
  std::vector<int> stack = {root_index};
  while (!stack.empty()) {
    const int top = stack.back();
    stack.pop_back();
    // Bitwise complement maps every index `idx >= 0` to `~idx = -idx - 1 < 0`
    // (in particular `~0 == -1`), so exit markers never collide with node
    // indices and `~top` recovers the index.
    if (top < 0) {
      // All descendants of `~top` were emitted after it, so the current size
      // of `tree_order_` is the exclusive end of its subtree.
      view.nodes_[~top].subtree_end = static_cast<int>(view.tree_order_.size());
      continue;
    }
    Node& node = view.nodes_[top];
    node.subtree_begin = static_cast<int>(view.tree_order_.size());
    // The parent was visited before its children (pre-order), so its depth is
    // already final.
    if (node.parent_index >= 0) {
      node.depth = view.nodes_[node.parent_index].depth + 1;
    }
    view.tree_order_.push_back(node.entity);
    // Each node is popped exactly once, so each child slice is sorted exactly
    // once. Entity names are unique, so the resulting order is deterministic
    // and independent of the proto order.
    std::ranges::sort(
        child_indices_span.subspan(node.children_begin,
                                   node.children_end - node.children_begin),
        std::less<>(), [&](int idx) -> absl::string_view {
          return view.nodes_[idx].entity->name();
        });
    // The exit marker is pushed below the children so that it is popped only
    // after the entire subtree of `top` has been emitted.
    stack.push_back(~top);
    // Children are pushed in reverse name order so that the child with the
    // smallest name is popped (visited) first. `children_` mirrors the sorted
    // slice of `child_indices` at the same positions.
    for (int c = node.children_end - 1; c >= node.children_begin; --c) {
      view.children_[c] = view.nodes_[child_indices[c]].entity;
      stack.push_back(child_indices[c]);
    }
  }

  return view;
}

const SceneObjectView::Entity* SceneObjectView::FindEntity(
    absl::string_view name) const {
  const auto it = index_by_name_.find(name);
  return it == index_by_name_.end() ? nullptr : nodes_[it->second].entity;
}

const SceneObjectView::Entity* SceneObjectView::GetParent(
    absl::string_view name) const {
  const auto it = index_by_name_.find(name);
  if (it == index_by_name_.end()) {
    return nullptr;
  }
  const int parent_index = nodes_[it->second].parent_index;
  return parent_index < 0 ? nullptr : nodes_[parent_index].entity;
}

absl::Span<const SceneObjectView::Entity* const> SceneObjectView::GetChildren(
    absl::string_view name) const {
  const auto it = index_by_name_.find(name);
  if (it == index_by_name_.end()) {
    return {};
  }
  const Node& node = nodes_[it->second];
  return absl::MakeConstSpan(children_).subspan(
      node.children_begin, node.children_end - node.children_begin);
}

absl::Span<const SceneObjectView::Entity* const>
SceneObjectView::GetSubtreeInTreeOrder(absl::string_view name) const {
  const auto it = index_by_name_.find(name);
  if (it == index_by_name_.end()) {
    return {};
  }
  const Node& node = nodes_[it->second];
  return absl::MakeConstSpan(tree_order_)
      .subspan(node.subtree_begin, node.subtree_end - node.subtree_begin);
}

bool SceneObjectView::IsInSubtree(absl::string_view name,
                                  absl::string_view subtree_root) const {
  const auto entity_it = index_by_name_.find(name);
  if (entity_it == index_by_name_.end()) {
    return false;
  }
  const auto root_it = index_by_name_.find(subtree_root);
  if (root_it == index_by_name_.end()) {
    return false;
  }
  const int pos = nodes_[entity_it->second].subtree_begin;
  const Node& root_node = nodes_[root_it->second];
  return root_node.subtree_begin <= pos && pos < root_node.subtree_end;
}

std::vector<const SceneObjectView::Entity*> SceneObjectView::GetPathFromRoot(
    absl::string_view name) const {
  const auto it = index_by_name_.find(name);
  if (it == index_by_name_.end()) {
    return {};
  }
  const Node* node = &nodes_[it->second];
  std::vector<const Entity*> path(node->depth + 1);
  for (int i = node->depth; i >= 0; --i) {
    path[i] = node->entity;
    if (node->parent_index >= 0) {
      node = &nodes_[node->parent_index];
    }
  }
  return path;
}

}  // namespace scene_object
}  // namespace intrinsic
