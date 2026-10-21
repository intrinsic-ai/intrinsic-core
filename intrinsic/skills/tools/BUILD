# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary")

go_binary(
    name = "skillregistryservice_main",
    srcs = ["skillregistryservice_main.go"],
    importpath = "intrinsic/skills/tools/skillregistryservice_main",
    deps = [
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/skills/internal:skillcomposer",
        "//intrinsic/skills/internal:skillregistryservice",
        "//intrinsic/skills/internal:skillruntime",
        "//intrinsic/skills/internal/proto:behavior_tree_registry_internal_go_proto",
        "//intrinsic/skills/internal/proto:skill_registry_internal_go_proto",
        "//intrinsic/util/go:shutdown",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:behavior_tree_registry_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_registry_config_go_proto",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@io_k8s_client_go//informers:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "skill-registry-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        ":skillregistryservice_main",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
