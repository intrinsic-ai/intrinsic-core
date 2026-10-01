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

load("//intrinsic/kubernetes:helm_chart.bzl", "helm_chart")

# TODO(b/326921974): DO NOT ADD NEW SERVICES HERE.
#
# This chart is deprecated and should not be used anymore.
# Instead, make your service an asset or use intrinsic-base.
#
# See go/intrinsic-sunsetting-app-chart-design for more details.
helm_chart(
    name = "intrinsic-app-chart",
    images = {
        # keep-sorted start
        "code_execution_service": "//intrinsic/executive/code:code-execution-service-image.tar",  # b/527851591
        "executive": "//intrinsic/executive:executive-image.tar",  # b/527852616
        "proto_registry": "//intrinsic_runtime/intrinsic/proto_tools/registry:proto-registry-image.tar",
        # keep-sorted end
    } | {
        "gzserver_insrc": "//intrinsic/simulation/docker:gzserver-proxy-oci-image.tar",
    } | {},
    templates = [
        # keep-sorted start
        "//intrinsic/executive/templates:code-execution-template",
        "//intrinsic/executive/templates:executive-template",
        "//intrinsic_runtime/intrinsic/proto_tools/templates:proto-registry-template",
        # keep-sorted end
    ] + [
        "//intrinsic/simulation/templates:gzserver-core-template",
    ] + [],
    values = ":app_values.yaml",
)

helm_chart(
    name = "intrinsic-base",
    # TODO: b/326921974 - Remove once services have been migrated to use this chart directly.
    embed = [
        ":intrinsic-app-chart",
    ],
    images = {
        # keep-sorted start
        "artifacts_service": "//intrinsic/storage/artifacts:artifacts-service-image.tar",
        "geometry": "//intrinsic/geometry/service:geometry-image.tar",
        "hot_shared_state": "//intrinsic/storage/hot_shared_state:hot-shared-state-image.tar",
        "http_gateway_service": "//intrinsic_runtime/httpjson:http-gateway-image.tar",
        "kvstore_service": "//intrinsic/platform/pubsub/kvstore_grpc:kvservice-image.tar",
        "operations": "//intrinsic/longrunning/service:operations-image.tar",
        "proto_builder": "//intrinsic_runtime/intrinsic/proto_tools/builder:proto-builder-image.tar",
        "resource_registry": "//intrinsic/resources/service:resource-registry-image.tar",
        "runtime_db": "//intrinsic/resources/service:runtime-db-image.tar",
        "scene_object_import": "//intrinsic/scene/service:scene-object-import-image.tar",
        "simulation_service": "//intrinsic/simulation/service:simulation-service-image.tar",
        "skill_registry": "//intrinsic/skills/tools:skill-registry-image.tar",
        "solution_service": "//intrinsic/frontend/solution_service:solution-service-image.tar",
        "workcell_spec_service": "//intrinsic/kubernetes/workcell_spec:workcell-spec-service-image.tar",
        "world": "//intrinsic/world/binary:world-image.tar",
        "world_updater": "//intrinsic/world/service/updater:world-updater-image.tar",
        "zenoh_introspection": "//intrinsic/platform/pubsub/introspection:zenoh-introspection-image.tar",
        # keep-sorted end
    } | {
        # keep-sorted start
        "local_cas": "//intrinsic/storage/onprem_cas:local-cas-image.tar",
        # keep-sorted end
    },
    # TODO(b/542107774): Fix linter for this helm chart.
    lint_test = False,
    templates = [
        # keep-sorted start
        "//intrinsic/frontend/onprem:solution-service-template",
        "//intrinsic/geometry/service:geomservice_template",
        "//intrinsic/kubernetes/workcell_spec:workcell-cluster-service-template",
        "//intrinsic/longrunning/service:operations-template",
        "//intrinsic/platform/pubsub/kvstore_grpc:kvservice-template",
        "//intrinsic/platform/pubsub:fds-template",
        "//intrinsic/resources/service:runtime_db_template",
        "//intrinsic/resources:resource-registry-template",
        "//intrinsic/scene/service:scene_object_import_template",  # needed for scene_object_import
        "//intrinsic/skills/templates:skill-registry-template",
        "//intrinsic/storage/artifacts:artifacts-service-template",
        "//intrinsic/world/binary:world-template",
        "//intrinsic/world/service/updater:world_updater_template",
        "//intrinsic_runtime/httpjson:http-gateway-template",
        "//intrinsic_runtime/intrinsic/proto_tools/templates:proto-builder-template",
        # keep-sorted end
    ] + [
        "//intrinsic/storage/data_store:core-template",
    ],
    values = ":core_base_values.yaml",
    visibility = ["//visibility:public"],
)
