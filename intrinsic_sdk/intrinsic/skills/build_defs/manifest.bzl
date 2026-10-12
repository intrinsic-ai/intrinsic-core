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

"""Build rule for creating a Skill Manifest."""

load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("//intrinsic/assets/build_defs:allowlists.bzl", "in_allowlist")  
load("//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl", "ProtoSourceCodeInfo", "gen_source_code_info_descriptor_set")

SkillManifestInfo = provider(
    "Info about a binary skill manifest",
    fields = {
        "file_descriptor_set": "The file descriptor set with source information",
        "manifest_binary_file": "The binary manifest File.",
    },
)

def _skill_manifest_impl(ctx):
    outputfile = ctx.actions.declare_file(ctx.label.name + ".pbbin")
    file_descriptor_set_out = ctx.actions.declare_file(ctx.label.name + "_filedescriptor.pbbin")
    pbtxt = ctx.file.src

    transitive_descriptor_sets = depset(
        transitive = [
            dep[ProtoSourceCodeInfo].transitive_descriptor_sets
            for dep in ctx.attr.deps
        ],
    )


    if not ctx.attr.incompatible_disallow_manifest_dependencies and not in_allowlist(ctx, "skill_manifest_deps"):
        fail("Manifest dependencies are no longer allowed and this manifest is not in the allowlist.")


    args = ctx.actions.args().add(
        "--manifest",
        pbtxt,
    ).add(
        "--output",
        outputfile,
    ).add(
        "--file_descriptor_set_out",
        file_descriptor_set_out,
    ).add_joined(
        "--file_descriptor_sets",
        transitive_descriptor_sets,
        join_with = ",",
    ).add(
        ctx.attr.incompatible_disallow_manifest_dependencies,
        format = "--incompatible_disallow_manifest_dependencies=%s",
    )

    outputs = [outputfile, file_descriptor_set_out]
    ctx.actions.run(
        outputs = outputs,
        inputs = depset([pbtxt], transitive = [transitive_descriptor_sets]),
        executable = ctx.executable._skillmanifestgen,
        arguments = [args],
        mnemonic = "SkillManifest",
    )

    legacy_symlinks = {}  


    if ctx.label.package.startswith("intrinsic_sdk/intrinsic/"):
        legacy_pkg = "google3/intrinsic/" + ctx.label.package[len("intrinsic_sdk/intrinsic/"):]
        legacy_symlinks[legacy_pkg + "/" + ctx.label.name + ".pbbin"] = outputfile
        legacy_symlinks[legacy_pkg + "/" + ctx.label.name + "_filedescriptor.pbbin"] = file_descriptor_set_out



    return [
        DefaultInfo(
            files = depset(outputs),
            runfiles = ctx.runfiles(files = outputs, symlinks = legacy_symlinks),  
        ),
        SkillManifestInfo(
            manifest_binary_file = outputfile,
            file_descriptor_set = file_descriptor_set_out,
        ),
    ]

skill_manifest = rule(
    doc = """Compiles a binary proto message for the given intrinsic_proto.skills.SkillManifest textproto
           and writes it to file.

           Example:
            skill_manifest(
              name = "foo_manifest",
              src = ["foo_manifest.textproto"],
              deps = [":foo_proto"],
            )

            creates the file foo_manifest.pbbin.

           Provides SkillManifestInfo.
           """,
    implementation = _skill_manifest_impl,
    attrs = {
        "deps": attr.label_list(
            doc = "proto deps of the manifest textproto for this skill. " +
                  "This is normally the proto message declaring the skill's return type and parameter " +
                  "type messages.",
            providers = [ProtoInfo],
            aspects = [gen_source_code_info_descriptor_set],
        ),
        "incompatible_disallow_manifest_dependencies": attr.bool(
            doc = "whether this manifest is prevented from using the old dependency model. " +
                  "This is a temporary attribute to allow a safe migration to the new model.",
            default = True,  

        ),
        "src": attr.label(
            allow_single_file = True,
            doc = "textproto specifying an intrinsic_proto.skills.SkillManifest",
        ),
        "_skillmanifestgen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/skills/build_defs:skillmanifestgen"),
            executable = True,
            cfg = "exec",
        ),
    },
)
