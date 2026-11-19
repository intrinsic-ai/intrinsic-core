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

#include "intrinsic_runtime/intrinsic/proto_tools/registry/resolver_asset.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "grpcpp/client_context.h"
#include "intrinsic_runtime/intrinsic/proto_tools/util/descriptor_pool_loader.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/assets/processes/proto/process_asset.pb.h"
#include "intrinsic/assets/proto/asset_type.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.pb.h"
#include "intrinsic/assets/proto/metadata.pb.h"
#include "intrinsic/assets/proto/view.pb.h"
#include "intrinsic/executive/cc/behavior_tree_visitor.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/proto/code_execution.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto/parsed_type_url.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

namespace intrinsic::proto_registry {

namespace {

class AssetResolverImpl : public AssetResolver {
 public:
  absl::StatusOr<google::protobuf::FileDescriptorSet> Resolve(
      const ParsedUrl& parsed_url) override;

  explicit AssetResolverImpl(
      std::shared_ptr<
          intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
          installed_assets_stub)
      : installed_assets_stub_(std::move(installed_assets_stub)) {}

 private:
  std::shared_ptr<
      intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
      installed_assets_stub_;
};

absl::StatusOr<intrinsic_proto::assets::v1::InstalledAsset>
CallGetInstalledAsset(
    std::string_view asset_id,
    intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface&
        installed_assets_stub,
    intrinsic_proto::catalog::AssetViewType view,
    std::string_view retrieval_context) {
  INTR_ASSIGN_OR_RETURN(
      std::string asset_name, assets::NameFrom(asset_id),
      std::move(_).With(AttachExtendedStatus(
          12001,
          absl::StrFormat("Failed to get asset name from '%s'", asset_id))));
  INTR_ASSIGN_OR_RETURN(
      std::string asset_package, assets::PackageFrom(asset_id),
      std::move(_).With(AttachExtendedStatus(
          12001,
          absl::StrFormat("Failed to get asset package from '%s'", asset_id))));

  grpc::ClientContext context;
  ConfigureClientContext(&context);
  intrinsic_proto::assets::v1::GetInstalledAssetRequest request;
  request.mutable_id()->set_package(asset_package);
  request.mutable_id()->set_name(asset_name);
  request.set_view(view);
  intrinsic_proto::assets::v1::InstalledAsset asset;

  INTR_RETURN_IF_ERROR(ToAbslStatus(installed_assets_stub.GetInstalledAsset(
                           &context, request, &asset)))
      .With(AttachExtendedStatus(
          12100, absl::StrFormat("Failed to get installed asset '%s' %s",
                                 asset_id, retrieval_context)));

  return asset;
}

bool FileDescriptorSetContainsType(
    const google::protobuf::FileDescriptorSet& fds,
    std::string_view full_name) {
  absl::StatusOr<std::unique_ptr<executive::LoadedDescriptorPool>> loaded_pool =
      executive::LoadedDescriptorPool::CreateUnowned(fds);
  if (!loaded_pool.ok()) {
    return false;
  }

  return (*loaded_pool)->descriptor_pool()->FindMessageTypeByName(full_name) !=
         nullptr;
}

// Collects the file descriptor sets of all code nodes of the given process
// behavior tree in the order in which they are visited.
absl::StatusOr<std::vector<const google::protobuf::FileDescriptorSet*>>
CollectFileDescriptorSetsFromBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& bt) {
  std::vector<const google::protobuf::FileDescriptorSet*> fds_list;

  auto visit_node =
      [&fds_list](const intrinsic_proto::executive::BehaviorTree&,
                  const intrinsic_proto::executive::BehaviorTree::Node& node) {
        if (node.task().execute_code().has_file_descriptor_set()) {
          fds_list.push_back(&node.task().execute_code().file_descriptor_set());
        }
        return absl::OkStatus();
      };
  INTR_RETURN_IF_ERROR(
      executive::VisitBehaviorTree(bt, {.visit_node = visit_node}));

  return fds_list;
}

// Returns the file descriptor set of the given process behavior tree which
// defines 'message_type'.
absl::StatusOr<google::protobuf::FileDescriptorSet>
FindFileDescriptorSetInBehaviorTree(
    const intrinsic_proto::executive::BehaviorTree& bt,
    std::string_view message_type) {
  // TODO(b/510731384): Use file descriptor set from asset's metadata instead of
  // merging parameter and return value sets.
  const intrinsic_proto::skills::Skill& description = bt.description();

  const google::protobuf::FileDescriptorSet& params_fds =
      description.parameter_description().parameter_descriptor_fileset();
  const google::protobuf::FileDescriptorSet& return_fds =
      description.return_value_description().descriptor_fileset();

  if (message_type.empty() ||
      FileDescriptorSetContainsType(params_fds, message_type) ||
      FileDescriptorSetContainsType(return_fds, message_type)) {
    google::protobuf::FileDescriptorSet tree_fds = params_fds;
    INTR_RETURN_IF_ERROR(MergeFileDescriptorSet(return_fds, tree_fds));
    return tree_fds;
  }

  INTR_ASSIGN_OR_RETURN(
      std::vector<const google::protobuf::FileDescriptorSet*> fds_list,
      CollectFileDescriptorSetsFromBehaviorTree(bt));
  for (const google::protobuf::FileDescriptorSet* fds : fds_list) {
    if (FileDescriptorSetContainsType(*fds, message_type)) {
      return *fds;
    }
  }

  return CreateStatus(
      12102,
      absl::StrFormat("Message '%s' is not defined by the parameters, return "
                      "value or any code node of the process",
                      message_type),
      absl::StatusCode::kNotFound);
}

}  // namespace

std::unique_ptr<AssetResolver> AssetResolver::Create(
    std::shared_ptr<
        intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
        installed_assets_stub) {
  return std::make_unique<AssetResolverImpl>(std::move(installed_assets_stub));
}

absl::StatusOr<google::protobuf::FileDescriptorSet> AssetResolverImpl::Resolve(
    const ParsedUrl& parsed_url) {
  if (parsed_url.path.empty()) {
    return CreateStatus(12001, "Asset type URL is missing asset ID (path).",
                        absl::StatusCode::kInvalidArgument);
  }

  std::vector<std::string> path_parts =
      absl::StrSplit(parsed_url.path, kTypeUrlSeparator);

  // Ignore version in type URL if present to support legacy type URLs which
  // used to have a version.
  if (path_parts.size() != 1 && path_parts.size() != 2) {
    return CreateStatus(
        12001,
        absl::StrFormat("Asset type URL '%s' has %zu parts, 1 or 2 parts "
                        "expected (<id> or <id>/<versions>)",
                        parsed_url.type_url, path_parts.size()),
        absl::StatusCode::kInvalidArgument);
  }
  const std::string asset_id = std::move(path_parts[0]);
  const std::string retrieval_context =
      absl::StrFormat("for type URL %s", parsed_url.type_url);

  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::assets::v1::InstalledAsset asset,
      CallGetInstalledAsset(
          asset_id, *installed_assets_stub_,
          intrinsic_proto::catalog::ASSET_VIEW_TYPE_ALL_METADATA,
          retrieval_context));

  if (asset.metadata().asset_type() !=
      intrinsic_proto::assets::ASSET_TYPE_PROCESS) {
    // TODO(b/530915170): Return only the relevant part of the descriptor set
    // when a specific message was requested.
    return asset.metadata().file_descriptor_set();
  }

  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::assets::v1::InstalledAsset full_asset,
      CallGetInstalledAsset(asset_id, *installed_assets_stub_,
                            intrinsic_proto::catalog::ASSET_VIEW_TYPE_FULL,
                            retrieval_context));
  if (!full_asset.deployment_data().has_process()) {
    return CreateStatus(
        12103,
        absl::StrFormat("Process asset '%s' has no process deployment data %s",
                        asset_id, retrieval_context),
        absl::StatusCode::kFailedPrecondition);
  }

  return FindFileDescriptorSetInBehaviorTree(
      full_asset.deployment_data().process().process().behavior_tree(),
      parsed_url.message_type);
}

}  // namespace intrinsic::proto_registry
