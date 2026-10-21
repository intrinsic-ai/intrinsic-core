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

"""Linter definitions for C/C++ using aspect_rules_lint.

Provides the `clang_tidy` aspect configured to run static analysis on C and C++
targets using the repository's hermetic LLVM toolchain and root `.clang-tidy` configuration.
"""

load("@aspect_rules_lint//lint:clang_tidy.bzl", "lint_clang_tidy_aspect")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

clang_tidy = lint_clang_tidy_aspect(
    # Use the hermetic LLVM Clang-Tidy binary provided by @llvm
    binary = Label("@llvm-project//clang-tools-extra/clang-tidy:clang-tidy"),

    # Mount and pass the repository root .clang-tidy configuration file
    global_config = [Label("//:.clang-tidy")],

    # Enable linting on libraries, binaries, and tests
    rule_kinds = ["cc_binary", "cc_library", "cc_test"],
)

# Output groups defined by aspect_rules_lint (not LLVM toolchain internals):
# - "rules_lint_human": Text diagnostics for developers. Exposing this in DefaultInfo
#   ensures `bazel build` evaluates the aspect and fails if Clang-Tidy finds errors.
# - "rules_lint_patch": Unified patch files containing Clang-Tidy auto-fixes,
#   consumed by `bazel run //:clang_tidy_fix` (apply_fixes.sh).
def _cc_tidy_rule_impl(ctx):
    output_groups = {}
    files = []
    if OutputGroupInfo in ctx.attr.target:
        og = ctx.attr.target[OutputGroupInfo]
        if "rules_lint_human" in og:
            output_groups["rules_lint_human"] = og["rules_lint_human"]
            files = og["rules_lint_human"]
        if "rules_lint_patch" in og:
            output_groups["rules_lint_patch"] = og["rules_lint_patch"]
    return [
        DefaultInfo(files = files),
        OutputGroupInfo(**output_groups),
    ]

cc_tidy_rule = rule(
    implementation = _cc_tidy_rule_impl,
    attrs = {
        "target": attr.label(
            providers = [CcInfo],
            aspects = [clang_tidy],
        ),
    },
)
