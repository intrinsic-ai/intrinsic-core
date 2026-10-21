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

"""Macros for testing text proto files."""

load("//bazel:cc_macros.bzl", "cc_test")
load("//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl", "proto_source_code_info_transitive_descriptor_set")

def _cc_test_for_srcs(name, srcs, message, tags, transitive_descriptor_set_name, **kwargs):
    srcs_locations = ["$(location %s)" % src for src in srcs]
    cc_test(
        name = name,
        data = srcs + [transitive_descriptor_set_name],
        deps = ["//intrinsic/util/proto/build_defs/testing:text_proto_test"],
        args = [
            "--message_full_name=" + message,
            "--text_proto_files=" + ",".join(srcs_locations),
            "--transitive_descriptor_set_file=$(location %s)" % transitive_descriptor_set_name,
        ],
        tags = tags,
        **kwargs
    )

def text_proto_test(
        name,
        src,
        deps,
        message,
        tags = None,
        **kwargs):
    """Creates a test for a single text proto file which tests whether it is well-formed.

    Args:
      name: The name of the generated test.
      src: The file (label expanding to a single text proto file) whose content shall be tested.
      deps: A list of proto_library targets where 'message' is defined. Typically only a single
        proto_library target is needed - transitive dependencies are pulled in automatically.
      message: The full message name of the message type represented by 'src', e.g.,
        'intrinsic_proto.Pose3d'.
      tags: (optional) Target tags.
      **kwargs: arguments to pass to underlying targets.
    """
    transitive_descriptor_set_name = "_%s_tds" % name
    proto_source_code_info_transitive_descriptor_set(
        name = transitive_descriptor_set_name,
        deps = deps,
        testonly = True,
        **kwargs
    )

    _cc_test_for_srcs(name, [src], message, tags, transitive_descriptor_set_name, **kwargs)

def text_proto_test_suite(
        name,
        srcs,
        deps,
        message,
        tags = None,
        shard_size = 10,
        **kwargs):
    """Creates a test suite for a set of text proto files which tests whether they are well-formed.

    Splits the set of text proto files into n shards of size 'shard_size' and creates a test for
    each shard. Then creates a test suite which contains all the generated tests.

    Args:
      name: The name of the generated test suite.
      srcs: The files (labels expanding to single text proto files) whose content shall be tested.
      deps: A list of proto_library targets where 'message' is defined. Typically only a single
        proto_library target is needed - transitive dependencies are pulled in automatically.
      message: The full message name of the message type represented by each file in 'srcs', e.g.,
        'intrinsic_proto.Pose3d'.
      tags: (optional) Target tags.
      shard_size: (optional) The number of text proto files to test in each shard. The last shard
        will contain less than 'shard_size' files if the number of files is not divisible by
        'shard_size'.
      **kwargs: arguments to pass to underlying targets.
    """
    if not srcs:
        fail("'srcs' must not be empty")

    transitive_descriptor_set_name = "_%s_tds" % name
    proto_source_code_info_transitive_descriptor_set(
        name = transitive_descriptor_set_name,
        deps = deps,
        testonly = True,
        **kwargs
    )

    # Create n cc_tests, each covering 'shard_size' text proto files.
    test_names = []
    for i in range(0, len(srcs), shard_size):
        test_name = "%s_shard_%d" % (name, i)
        test_names.append(test_name)
        _cc_test_for_srcs(
            name = test_name,
            srcs = srcs[i:i + shard_size],
            message = message,
            tags = tags,
            transitive_descriptor_set_name = transitive_descriptor_set_name,
            **kwargs
        )

    native.test_suite(name = name, tests = test_names, tags = tags)
