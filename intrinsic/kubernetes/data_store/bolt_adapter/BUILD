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

load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library", "go_proto_library")

go_library(
    name = "boltadapter",
    srcs = ["bolt_adapter.go"],
    importpath = "intrinsic/kubernetes/data_store/bolt_adapter/boltadapter",
    visibility = [
        "//intrinsic/kubernetes/data_store:__subpackages__",
        "//intrinsic/storage/hot_shared_state:__pkg__",
    ],
    deps = [
        ":bolt_record_go_proto",
        "//intrinsic/kubernetes/data_store:revision",
        "//intrinsic/kubernetes/data_store/firestore_shim:firestoreshimtypes",
        "//intrinsic/kubernetes/data_store/utils:uriutils",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@io_etcd_go_bbolt//:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

proto_library(
    name = "bolt_record_proto",
    srcs = ["bolt_record.proto"],
)

go_proto_library(
    name = "bolt_record_go_proto",
    importpath = "intrinsic/kubernetes/data_store/bolt_adapter/bolt_record_go_proto",
    protos = [":bolt_record_proto"],
)
