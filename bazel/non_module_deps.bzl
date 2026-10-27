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

"""
Module extension for non-module dependencies
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive", "http_file", "http_jar")

# TODO(b/572956581): Remove NVIDIA Pose Estimator out of insrc
load("//bazel:nvidia_pose_estimator_models.bzl", "nvidia_pose_estimator_models")  

# Module extension function signatures include a ctx variable, which should not be removed.
def _non_module_deps_impl(
        ctx):  # @unused



    # Download and extract GLVND EGL/GL stubs from the sysroot.
    # Needed for tests that require OpenGL/EGL
    http_archive(
        name = "sysroot_glvnd_stubs",
        build_file = Label("//intrinsic/production/external:sysroot_glvnd_stubs.BUILD.bazel"),
        urls = ["https://storage.googleapis.com/chrome-linux-sysroot/toolchain/4f611ec025be98214164d4bf9fbe8843f58533f7/debian_bullseye_amd64_sysroot.tar.xz"],
        sha256 = "5df5be9357b425cdd70d92d4697d07e7d55d7a923f037c22dc80a78e85842d2c",
        type = "tar.xz",
    )

    http_jar(
        name = "firestore_emulator",
        sha256 = "9d43599ed6151199e8d604dc87fac51218e49e5f3a48519b1ae560bbe5e3382d",
        urls = ["https://storage.googleapis.com/firebase-preview-drop/emulator/cloud-firestore-emulator-v1.19.8.jar"],
    )

    http_archive(
        name = "coal",
        build_file = Label("//intrinsic/production/external:coal.BUILD.bazel"),
        sha256 = "136d7f809fccd0027265dead58abb57798fc7631f1a3331f139a74bed92066cc",
        strip_prefix = "coal-1.0.1",
        urls = ["https://github.com/torresl-google/coal/archive/refs/tags/v1.0.1.tar.gz"],
    )

    http_archive(
        name = "cgal",
        build_file = Label("//intrinsic/production/external:cgal.BUILD.bazel"),
        sha256 = "4a0713351882989dda8b6bbfa942d5013b994f297fbebe1096ce664a92aff1a0",
        strip_prefix = "cgal-6.2.1",
        url = "https://github.com/CGAL/cgal/archive/refs/tags/v6.2.1.tar.gz",
    )

    http_archive(
        name = "foundation_pose_inference_library",
        build_file = Label("//intrinsic/production/external:foundation_pose_inference_library.BUILD.bazel"),
        sha256 = "de217d2c4e1f2526f3d156c69c20bcc11afaca961a11b844703a86f4f79b21ae",
        strip_prefix = "foundation-pose-inference-library-32d1122bf819b4db5b6d823aaa4092e27a562091",
        urls = ["https://github.com/intrinsic-opensource/foundation-pose-inference-library/archive/32d1122bf819b4db5b6d823aaa4092e27a562091.tar.gz"],
    )

    http_archive(
        name = "foundationpose_perception_pipeline",
        build_file = Label("//intrinsic/production/external:foundationpose_perception_pipeline.BUILD.bazel"),
        sha256 = "403987508aacc773e3ef81cfdb3aa8b79b00e741fe92fcff8c32c46d29c76f3e",
        strip_prefix = "foundationpose_perception_pipeline-cd4965a1095fc1fd66da6e2a10b0488ebbf586eb",
        urls = ["https://github.com/intrinsic-opensource/foundationpose_perception_pipeline/archive/cd4965a1095fc1fd66da6e2a10b0488ebbf586eb.tar.gz"],
    )

    # foundationpose_refine_onnx, foundationpose_score_onnx, and
    # foundationstereo_onnx are gated behind
    # --//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:download_weights=True
    # so CI does not fetch them unnecessarily.

    http_file(
        name = "foundationpose_refine_onnx",
        downloaded_file_path = "refiner_net.onnx",
        sha256 = "4a4445d2506b1bfc0a8d62fa80f8780a7e37bb0df13bb09aa5dd04a03bf0c954",
        urls = [
            "https://api.ngc.nvidia.com/v2/models/nvidia/tao/foundationpose/versions/deployable_v1.0/files/refiner_net.onnx",
            "https://huggingface.co/nvidia/foundationpose/resolve/18d8309afc9790cddc03a1d50bc69954dc058693/refiner_net.onnx",
        ],
    )

    http_file(
        name = "foundationpose_score_onnx",
        downloaded_file_path = "score_net.onnx",
        sha256 = "bb7208919557560ecb0e10a30612821f0025a2578190b17677bdf3653514a48d",
        urls = [
            "https://api.ngc.nvidia.com/v2/models/nvidia/tao/foundationpose/versions/deployable_v1.0/files/score_net.onnx",
            "https://huggingface.co/nvidia/foundationpose/resolve/18d8309afc9790cddc03a1d50bc69954dc058693/score_net.onnx",
        ],
    )

    http_file(
        name = "foundationstereo_onnx",
        downloaded_file_path = "deployable_foundation_stereo_s_dynamic_v2.0.onnx",
        sha256 = "a001a7bc0512a0bc3b3218194e924784e58b20656c6f1ea2c151024e555cfd64",
        urls = ["https://api.ngc.nvidia.com/v2/models/nvidia/tao/foundationstereo/versions/deployable_foundation_stereo_s_dynamic_v2.0/files/deployable_foundation_stereo_s_dynamic_v2.0.onnx"],
    )

    # Locally staged model files. These files need to be fetched manually to the
    # $NVIDIA_POSE_ESTIMATOR_MODELS_DIR directory. Defaults to:
    # intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models/
    #
    #   - SAM 3 ONNX graphs and BPE vocabulary, exported from the gated SAM 3
    #     checkpoint via:
    #     intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/scripts/prepare_sam3_models.sh
    #   - [Optional] Precompiled TensorRT plans.

    # Tracked for removal from insrc in http://b/572956581
    nvidia_pose_estimator_models(
        name = "nvidia_pose_estimator_models",
    )

    # For ur_calibration.[cc/h]
    http_archive(
        name = "ur_calibration",
        build_file = Label("//intrinsic/production/external:ur_calibration.BUILD.bazel"),
        patch_args = ["-p1"],
        patches = [
            # TODO(b/382000146): Remove patch once Logger can be selected.
            Label("//intrinsic/production/external/patches:ur-calibration.patch"),
        ],
        sha256 = "e3b5a7b4c1d9ea2e59497b1c5f029562d8352828d3213d9bac46a555fbdf6786",
        strip_prefix = "Universal_Robots_ROS2_Driver-4.1.0/ur_calibration",
        urls = ["https://github.com/UniversalRobots/Universal_Robots_ROS2_Driver/archive/refs/tags/4.1.0.tar.gz"],
    )

    # TODO build kubectl from source (go package "k8s.io/kubernetes/cmd/kubectl")
    # - https://github.com/bazel-contrib/bazel-gazelle/issues/1392#issuecomment-2642983316
    http_archive(
        name = "kubebuilder_test_tools",
        build_file_content = """
exports_files(
    [
        "etcd",
        "kube-apiserver",
        "kubectl",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "tools",
    srcs = glob(["*"]),
    visibility = ["//visibility:public"],
)
""",
        sha256 = "5716719def14a3fec3ed285e5e8c4280e6268854039b5073a96e8c0adafb1c02",
        type = "tar.gz",
        urls = ["https://github.com/kubernetes-sigs/controller-tools/releases/download/envtest-v1.35.0/envtest-v1.35.0-linux-amd64.tar.gz"],
        strip_prefix = "controller-tools/envtest",
    )

    # Schema JSON files for helm chart validation.
    # Generated by:
    #
    # git clone https://github.com/yannh/kubernetes-json-schema
    # cd kubernetes-json-schema
    # tar zcf schemas.tar.gz master-standalone-strict
    # gcloud storage mv schemas.tar.gz gs://intrinsic-mirror/bazel/helmlint/schemas-$(git rev-parse HEAD).tar.gz
    #
    # We do not depend on the full git repository as a http_arcive, because the archive is huge
    # (~1GB) and this file we need is only a few MB.
    http_file(
        name = "kubernetes_json_schema",
        sha256 = "7df8fe4a1739b56b03bb3794f64a77bcf978d9de11548ed920526e090864b9ed",
        downloaded_file_path = "schemas.tar.gz",
        urls = ["https://storage.googleapis.com/intrinsic-mirror/bazel/helmlint/schemas-b3d60bb6a5c10815dcfb1ee592b4e5b6f10cef29.tar.gz"],
    )

    GLOG_COMMIT = "7b134a5c82c0c0b5698bb6bf7a835b230c5638e4"  # v0.7.1
    http_file(
        name = "com_google_glog_googleinit_h",
        downloaded_file_path = "googleinit.h",
        urls = ["https://raw.githubusercontent.com/google/glog/%s/src/base/googleinit.h" % GLOG_COMMIT],
        sha256 = "ee770ce4721fe37dd3ef6a80e4f646082d835bf4923d235c29484641b0129782",
    )

    OPENXLA_COMMIT = "35fbc9a3be7d43ffaedecb96e3fa1b0e7cf5a077"  # 2026-10-12
    http_file(
        name = "com_github_openxla_xla_tsl_net_h",
        downloaded_file_path = "net.h",
        urls = ["https://raw.githubusercontent.com/openxla/xla/%s/third_party/tsl/tsl/platform/net.h" % OPENXLA_COMMIT],
        sha256 = "67042c2c2d2014d1e4d446eae02a436d8993162d2c92e75fcd729784c26c1854",
    )
    http_file(
        name = "com_github_openxla_xla_tsl_net_cc",
        downloaded_file_path = "net.cc",
        urls = ["https://raw.githubusercontent.com/openxla/xla/%s/xla/tsl/platform/default/net.cc" % OPENXLA_COMMIT],
        sha256 = "a3c6a4f331255dedbb77e349bbdf34ac4971fa60115bb33e7d72c33ce8ef4e87",
    )

    http_file(
        name = "oss_licenses",
        downloaded_file_path = "oss_licenses.json",
        urls = ["https://storage.googleapis.com/intrinsic-mirror/oss_licenses/intrinsic.platform.20251110.RC08.json"],
        # This is a load bearing comment.
        sha256 = "7022570bc17af774bdf42c5214e3b270c9250ace8f3df1d6377583c5efb785df",  # OSS_LICENSES_SHA256
    )

    http_file(
        name = "libkmsp11",
        downloaded_file_path = "libkmsp11-1.1-linux-amd64.tar.gz",
        urls = ["https://github.com/GoogleCloudPlatform/kms-integrations/releases/download/v1.1/libkmsp11-1.1-linux-amd64.tar.gz"],
        sha256 = "d75c991cbf3fa391b1b95b95358feaa41894a608c8ded21028cdfa6fc5f0c378",
    )



    # The pass below ensures that if copybara strips everything out of this function
    # it still produces a valid function (functions without content are invalid).
    pass

non_module_deps_ext = module_extension(implementation = _non_module_deps_impl)
