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

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "create",
    srcs = ["create.go"],
    embedsrcs = [
        "templates/BUILD_cc_fragment.template",  #keep
        "templates/BUILD_py_fragment.template",  #keep
        "templates/skill_cc.template",  #keep
        "templates/skill_cc_manifest.template",  #keep
        "templates/skill_cc_test.template",  #keep
        "templates/skill_h.template",  #keep
        "templates/skill_integration_test.template",  #keep
        "templates/skill_params_proto.template",  #keep
        "templates/skill_py.template",  #keep
        "templates/skill_py_manifest.template",  #keep
        "templates/skill_py_test.template",  #keep
    ],
    importpath = "intrinsic/skills/tools/skill/cmd/create/create",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/bazel",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/version",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:buildozer",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:templateutil",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_stoewer_go_strcase//:go_default_library",
    ],
)
