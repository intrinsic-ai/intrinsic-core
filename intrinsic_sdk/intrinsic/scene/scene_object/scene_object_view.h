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

#ifndef INTRINSIC_SCENE_SCENE_OBJECT_SCENE_OBJECT_VIEW_H_
#define INTRINSIC_SCENE_SCENE_OBJECT_SCENE_OBJECT_VIEW_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/absl_check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace scene_object {

// Read-only view of a validated SceneObject proto that provides structural
// queries on its entity tree.
//
// Children of an entity are always ordered by entity name, which makes all
// orderings returned by this class independent of the order of the entities
// in the SceneObject proto.
//
// The view takes ownership of the (immutable) SceneObject proto. All returned
// pointers, references, and spans remain valid as long as the view exists,
// including after the view has been moved.
//
// Example:
//
//   INTR_ASSIGN_OR_RETURN(const SceneObjectView view,
//                         SceneObjectView::Create(std::move(scene_object)));
//   for (const Entity* entity : view.GetEntitiesInTreeOrder()) {
//     ...
//   }
class SceneObjectView {
 public:
  using Entity = ::intrinsic_proto::scene_object::v1::Entity;
  using SceneObject = ::intrinsic_proto::scene_object::v1::SceneObject;

  // Validates `scene_object` and creates a view of it. Returns the error of
  // `ValidateSceneObject()` if `scene_object` is invalid.
  static absl::StatusOr<SceneObjectView> Create(SceneObject scene_object);

  SceneObjectView(const SceneObjectView&) = delete;
  SceneObjectView& operator=(const SceneObjectView&) = delete;
  SceneObjectView(SceneObjectView&&) = default;
  SceneObjectView& operator=(SceneObjectView&&) = default;

  // Returns the owned SceneObject proto.
  const SceneObject& scene_object() const {
    ABSL_DCHECK(scene_object_ != nullptr);
    return *scene_object_;
  }

  // Returns the root entity, which is the only entity without a parent.
  const Entity& GetRootEntity() const {
    ABSL_DCHECK(scene_object_ != nullptr);
    ABSL_DCHECK(root_ != nullptr);
    return *root_;
  }

  // Returns the number of entities.
  int GetNumEntities() const { return static_cast<int>(nodes_.size()); }

  // Returns true if an entity named `name` exists.
  bool HasEntity(absl::string_view name) const {
    return index_by_name_.contains(name);
  }

  // Returns the entity named `name`, or nullptr if there is no such entity.
  const Entity* FindEntity(absl::string_view name) const;

  // Returns the parent of the entity named `name`. Returns nullptr for the
  // root entity or if there is no entity named `name`.
  const Entity* GetParent(absl::string_view name) const;

  // Returns the children of the entity named `name` ordered by entity name.
  // Returns an empty span for leaf entities or if there is no entity named
  // `name`.
  absl::Span<const Entity* const> GetChildren(absl::string_view name) const;

  // Returns true if the entity named `name` has no children. Returns false if
  // the entity has children or if there is no entity named `name`.
  bool IsLeaf(absl::string_view name) const {
    return HasEntity(name) && GetChildren(name).empty();
  }

  // Returns all entities in depth-first pre-order of the entity tree, starting
  // at the root entity with children visited in order of entity name. Each
  // entity precedes its descendants, and the subtree of an entity is
  // contiguous.
  absl::Span<const Entity* const> GetEntitiesInTreeOrder() const {
    return tree_order_;
  }

  // Returns the entity named `name` followed by all its descendants, in the
  // same order as `GetEntitiesInTreeOrder()`. Returns an empty span if there is
  // no entity named `name`.
  absl::Span<const Entity* const> GetSubtreeInTreeOrder(
      absl::string_view name) const;

  // Returns true if the entity named `name` belongs to the subtree rooted at
  // `subtree_root` (including when `name == subtree_root`). Returns false if
  // either entity does not exist.
  bool IsInSubtree(absl::string_view name,
                   absl::string_view subtree_root) const;

  // Returns the chain of entities from the root entity to the entity named
  // `name` (both inclusive). Returns an empty vector if there is no entity
  // named `name`.
  std::vector<const Entity*> GetPathFromRoot(absl::string_view name) const;

  // Returns the pose of every entity relative to the root entity keyed
  // by entity name. Returns an error if the pose of an entity cannot be
  // computed.
  absl::StatusOr<absl::flat_hash_map<absl::string_view, Pose3d>> GetRootPoses()
      const;

 private:
  struct Node {
    const Entity* entity = nullptr;
    int parent_index = -1;
    int depth = 0;
    // Half-open range [children_begin, children_end) of `entity`'s children in
    // `children_`, ordered by entity name.
    int children_begin = 0;
    int children_end = 0;
    // Half-open range [subtree_begin, subtree_end) of the subtree of `entity`
    // in `tree_order_`.
    int subtree_begin = 0;
    int subtree_end = 0;
  };

  explicit SceneObjectView(std::unique_ptr<const SceneObject> scene_object)
      : scene_object_(std::move(scene_object)) {}

  // Heap-allocated so that the pointee never moves, which keeps the entity
  // pointers and name keys below valid when the view is moved.
  std::unique_ptr<const SceneObject> scene_object_;
  const Entity* root_ = nullptr;
  // Parallel to `scene_object_->entities()`.
  std::vector<Node> nodes_;
  // Keys reference the entity names in `*scene_object_`; values index into
  // `nodes_`.
  absl::flat_hash_map<absl::string_view, int> index_by_name_;

  // `children_` and `tree_order_` both hold entity pointers but are laid out
  // for different queries. Both are illustrated for this example tree, whose
  // entities appear in the proto in the order root, b, a, d, c:
  //
  //     root
  //     |-- a
  //     |   `-- c
  //     `-- b
  //         `-- d

  // Children of every entity, grouped by parent. Each entity's children form
  // one contiguous slice [Node::children_begin, Node::children_end), sorted by
  // entity name. Slices are laid out in `nodes_` order (proto order of the
  // parents), so the layout as a whole carries no tree meaning. Contains every
  // entity except the root exactly once, i.e., `GetNumEntities() - 1` entries.
  // Backs `GetChildren()`.
  //
  //     index:      0  1   2   3
  //     children_: [a, b | d | c]
  //                 \__/   |   |
  //                 root   b   a     (leaves d and c have empty slices)
  std::vector<const Entity*> children_;

  // All entities in depth-first pre-order, starting at the root and visiting
  // children in order of entity name. Every entity precedes its descendants,
  // and the subtree of each entity is one contiguous range
  // [Node::subtree_begin, Node::subtree_end). Backs `GetEntitiesInTreeOrder()`,
  // `GetSubtreeInTreeOrder()`, and `IsInSubtree()`.
  //
  //     index:        0     1  2  3  4
  //     tree_order_: [root, a, c, b, d]
  //                   \______________/  root: [0, 5)
  //                         \__/        a:    [1, 3)
  //                               \__/  b:    [3, 5)
  //
  // An entity's children are generally not adjacent here (root's children are
  // at indices 1 and 3), which is why `children_` is stored separately.
  std::vector<const Entity*> tree_order_;
};

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SCENE_OBJECT_SCENE_OBJECT_VIEW_H_
