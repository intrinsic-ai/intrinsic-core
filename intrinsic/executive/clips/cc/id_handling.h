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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_ID_HANDLING_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_ID_HANDLING_H_

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"

namespace intrinsic::executive {

// Generate and manage tree and node ids in loaded operations.
//
// Loading a new operation must proceed as follows to guarantee valid and unique
// ids.
// 1. When loading a process tree proto use EnsureValidIds(tree,
// conflicting_tree_ids), where conflicting_tree_ids are all tree ids already
// loaded in the executive in other operations.
// 2. When loading a PBT instance at a node use EnsureValidIds(tree,
// tree_id_prefix) and use GenerateTreeIdPrefixForNode given the calling node's
// tree id and node id.
//
// A side-effect of generating unique tree ids is that tree ids in nested PBTs
// will be prefixed with the calling node internally. This is shown in all
// internal tree ids, but also reported as such in the called_tree_state in the
// state proto.
// This ensures that even the same PBT used multiple times from the same process
// tree will result in unique ids for within each PBT instance.

// Checks and ensures valid tree and node ids for 'tree'. Tree must have valid
// tree and node ids according to ValidateAndGetTreeIds and
// ValidateAndGetNodeIds, i.e., most importantly no duplicates.
// In addition no tree ids in 'tree' must already be present in
// conflicting_tree_ids.
//
// If successful, makes sure that all tree and node ids are set, either from the
// already set values in tree or randomly generated.
//
// Exposed in CLIPS as ensure-valid-ids
// Args:
//   ?tree_proto_id: proto id of the behavior tree proto (modified in place)
//   ?conflicting_tree_ids: Multifield to tree ids representing
//     conflicting tree ids.
// Returns:
//   Multifield pair of bool and value.
//   On success: TRUE and an empty string
//   On failure: FALSE and a proto id of an ExtendedStatus. This is owned by the
//     caller.
absl::Status EnsureValidIds(
    intrinsic_proto::executive::BehaviorTree& tree,
    const absl::flat_hash_set<std::string>& conflicting_tree_ids);

// Checks and ensures valid tree and node ids for 'tree'. Tree must have valid
// tree and node ids according to ValidateAndGetTreeIds and
// ValidateAndGetNodeIds, i.e., most importantly no duplicates.
//
// If successful, makes sure that all tree and node ids are set, either from the
// already set values in 'tree' or randomly generated.
// In addition all tree ids are prefixed with tree_id_prefix. Thus if
// tree_id_prefix is not a prefix of any global tree id, then afterwards the
// tree ids in 'tree' are globally unique.
//
// Exposed in CLIPS as ensure-valid-ids-with-prefix
// Args:
//   ?tree_proto_id: proto id of the behavior tree proto (modified in place)
//   ?prefix: Prefix to use for tree ids, usually from
//     generate-tree-id-prefix-for-node
// Returns:
//   Multifield pair of bool and value.
//   On success: TRUE and an empty string
//   On failure: FALSE and a proto id of an ExtendedStatus. This is owned by the
//     caller.
absl::Status EnsureValidIds(intrinsic_proto::executive::BehaviorTree& tree,
                            absl::string_view tree_id_prefix);

// Generate a canonical prefix representing a node.
//
// This is typically used to prefix a BehaviorTree proto for EnsureValidIds
// prior to loading this proto when it could cause conflicts for the same proto
// being loaded multiple times. This is the case for loading a PBT instance from
// a task node. In that case the tree_id and node_id of that task node calling
// the PBT instance would be passed.
//
// Exposed in CLIPS as generate-tree-id-prefix-for-node
// Args:
//   ?tree_id: Tree id that the node is in
//   ?node_id: Node id for this node
// Returns:
//   String with the prefix
std::string GenerateTreeIdPrefixForNode(absl::string_view tree_id,
                                        uint32_t node_id);

// Generates a NodeIdentifier proto for the given tree_id and node_id.
// tree_id can be the tree id of a process tree or a nested tree id within a
// reusable process.
//
// Example:
// tree_id: main_tree:43/reusable_tree node: 42
// -> NodeIdentifier:
//    tree_id: main_tree node_id: 43
//    node_within_task_node { tree_id: reusable_tree node_id: 42 }
absl::StatusOr<intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
GenerateNodeIdentifier(absl::string_view tree_id, uint32_t node_id);

// Sets the generated NodeIdentifier for tree_id and node_id on the field
// specified by proto_path in the proto identified by proto_id.
// If proto_path is empty, proto_id itself must identify a NodeIdentifier proto.
//
// Exposed in CLIPS as set-node-identifier-proto.
// Args:
//   ?proto_id: Proto ID of the message to modify
//   ?proto_path: Field path pointing to a NodeIdentifier field
//   ?tree_id: Tree ID (possibly prefixed)
//   ?node_id: Node ID
// Returns:
//   TRUE on success, FALSE on failure.
absl::Status SetNodeIdentifier(clips::ProtobufManager* proto_mgr,
                               clips::ProtoMessageId proto_id,
                               absl::string_view proto_path,
                               absl::string_view tree_id, uint32_t node_id);

// Adds the main id handling functions to the CLIPS environment.
// The ProtobufManager must be available as long as the functions are registered
// in the environment.
absl::Status AddClipsIdHandlingFunctions(clips::Environment* env,
                                         clips::ProtobufManager* mgr);

// Checks if a given id is a valid identifier, e.g., to use as a tree id.
absl::Status IsValidIdentifier(absl::string_view id);

// Sanitizes an identifier by removing all invalid characters. This is possibly
// typically done to generate an identifier from a user given name. The result
// will be either a valid identifier or an empty string (if there are not valid
// characters in str).
void SanitizeIdentifier(std::string& str);

// Checks whether the given key is a valid blackboard key and can be used.
//
// It checks whether the key is not a reserved key and (optionally) if it's
// empty.
//
// Exposed in CLIPS as is-valid-blackboard-key.
// Args:
//   ?key: blackboard key to check
//   ?where: description where the key is used, added to error message for
//     invalid keys.
//  ?empty_option: Pass NON-EMPTY if ?key may not be the empty string,
//  EMPTY-OK otherwise.
//
// Returns:
//   Pair of bool and string. If the key is a valid key returns (TRUE
//   ""), otherwise returns a pair of FALSE and an error message.
absl::Status IsValidBlackboardKey(absl::string_view key, bool allow_empty,
                                  absl::string_view context = "");

// Validates the tree_ids defined in 'tree'. Each tree id must be a valid
// identifier and there must not be any duplicates.
// Empty tree ids are considered valid in this function.
// If there are no errors returns the tree ids (excluding empty tree ids).
absl::StatusOr<absl::flat_hash_set<std::string>> ValidateAndGetTreeIds(
    const intrinsic_proto::executive::BehaviorTree& tree);

// Checks if all tree ids in 'tree' are valid according to
// ValidateAndGetTreeIds. Also checks that none of the tree ids specified in
// 'tree' is already present in conflicting_tree_ids. If successful ensures that
// there are no empty tree ids in tree, i.e., every tree id is valid if this
// call returns an OK status.
//
// After this call every tree id, including already set ones, will be prefixed
// with 'prefix'. If a tree id is not set, a random one will be generated. The
// tree's name might be partially integrated into that id to make identifying
// this easier.
absl::Status EnsureValidTreeIds(
    intrinsic_proto::executive::BehaviorTree& tree,
    const absl::flat_hash_set<std::string>& conflicting_tree_ids,
    absl::string_view prefix);

// Validates the node ids defined in 'tree'. Each node id must be unique within
// its tree, i.e., it is OK if there are duplicate node ids within tree as long
// as they are in different subtrees or behavior tree conditions.
// Unset node ids (0) are considered valid in this function.
// For this function to work correctly tree ids in 'tree' must all be set and
// valid.
// If there are no errors returns a map from tree id to its node ids (excluding
// unset (0) node ids).
absl::StatusOr<absl::flat_hash_map<std::string, absl::flat_hash_set<uint32_t>>>
ValidateAndGetNodeIds(const intrinsic_proto::executive::BehaviorTree& tree);

// Checks if all node ids in 'tree' are valid according to
// ValidateAndGetNodeIds. In addition ensures that there are no empty (0) node
// ids in tree, i.e., every node id is set (!= 0) if this call returns an OK
// status.
// If a node id is not set, a random one will be generated.
//
// For this function to work correctly tree ids in 'tree' must all be set and
// valid.
absl::Status EnsureValidNodeIds(intrinsic_proto::executive::BehaviorTree& tree,
                                absl::BitGenRef id_random_generator);

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_ID_HANDLING_H_
