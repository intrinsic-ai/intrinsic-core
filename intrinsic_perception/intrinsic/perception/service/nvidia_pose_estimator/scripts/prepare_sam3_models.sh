#!/usr/bin/env bash

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

# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Downloads the gated SAM 3 checkpoint (`sam3.pt`) from HuggingFace (`facebook/sam3`)
# using the active `hf auth login` session, exports the 4 SAM 3 ONNX graphs + BPE
# vocabulary via `foundationpose_perception_pipeline/tools/export_sam3_to_onnx.py`,
# and stages them where `bazel build :sam3_model_data_asset` will find them.
#
# Output goes to $NVIDIA_POSE_ESTIMATOR_MODELS_DIR (default
# `intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models`).
#
# Usage:
#   ./scripts/prepare_sam3_models.sh [--force] [--output-dir DIR] [--venv-dir DIR]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_OUTPUT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)/models"
OUTPUT_DIR="${NVIDIA_POSE_ESTIMATOR_MODELS_DIR:-${DEFAULT_OUTPUT_DIR}}"
VENV_DIR="${SAM3_EXPORT_VENV_DIR:-/tmp/sam3_onnx_export_venv}"
# Hash-pinned export dependencies (see sam3_export_requirements.in to regenerate).
REQUIREMENTS_FILE="${SCRIPT_DIR}/sam3_export_requirements.txt"
# Package index for the export dependencies: PyPI, unless UV_DEFAULT_INDEX or PIP_INDEX_URL
# points at a mirror. Every download is checked against the lockfile hashes either way.
INDEX_URL="${UV_DEFAULT_INDEX:-${PIP_INDEX_URL:-https://pypi.org/simple}}"
FORCE=0

SAM3_REPO_ID="facebook/sam3"
SAM3_FILENAME="sam3.pt"
SAM3_EXPECTED_SHA256="9999e2341ceef5e136daa386eecb55cb414446a00ac2b55eb2dfd2f7c3cf8c9e"
# When bumping SAM3_GIT_COMMIT, check that the runtime dependencies in sam3's
# pyproject.toml are still covered by sam3_export_requirements.in (sam3 is installed
# with --no-deps).
# LINT.IfChange(sam3_commit)
SAM3_GIT_COMMIT="608457d816009fc369a02c692f27efdd7256b442"
# LINT.ThenChange(//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/BUILD:sam3_version)
# The export runs foundationpose_perception_pipeline's tools/export_sam3_to_onnx.py. When
# bumping PIPELINE_GIT_COMMIT, check that its pyproject.toml still requires
# EXPORT_PYTHON_VERSION and that its torch/torchvision pins still match
# sam3_export_requirements.in.
PIPELINE_GIT_COMMIT="cd4965a1095fc1fd66da6e2a10b0488ebbf586eb"
# The lockfile is compiled for exactly this Python version.
# LINT.IfChange(export_python_version)
EXPORT_PYTHON_VERSION="3.12"
# LINT.ThenChange(//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/scripts/sam3_export_requirements.in:lockfile_command)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force)
      FORCE=1
      shift
      ;;
    --output-dir)
      OUTPUT_DIR="$2"
      shift 2
      ;;
    --venv-dir)
      VENV_DIR="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 [--force] [--output-dir <dir>] [--venv-dir <dir>]"
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

REQUIRED_FILES=(
  "sam3_vision_encoder.onnx"
  "sam3_text_encoder.onnx"
  "sam3_mask_decoder.onnx"
  "sam3_box_decoder.onnx"
  "bpe_simple_vocab_16e6.txt.gz"
)

# 1. Check if all files are already staged
if [[ "${FORCE}" -eq 0 ]]; then
  ALL_PRESENT=1
  for f in "${REQUIRED_FILES[@]}"; do
    if [[ ! -s "${OUTPUT_DIR}/${f}" ]]; then
      ALL_PRESENT=0
      break
    fi
  done
  if [[ "${ALL_PRESENT}" -eq 1 ]]; then
    echo "✓ All 5 SAM 3 files are already present in ${OUTPUT_DIR}:"
    for f in "${REQUIRED_FILES[@]}"; do
      ls -lh "${OUTPUT_DIR}/${f}"
    done
    echo "Pass --force to re-export."
    exit 0
  fi
fi

# 2. Check for CUDA GPU (optional; falls back to CPU export when no GPU is detected)
if ! command -v nvidia-smi >/dev/null 2>&1 || ! nvidia-smi -L >/dev/null 2>&1; then
  echo "ℹ No local CUDA GPU detected; SAM 3 ONNX export will run on CPU."
fi

# 3. Check HuggingFace authentication
if [[ -z "${HF_TOKEN:-}" && -z "${HUGGING_FACE_HUB_TOKEN:-}" && ! -f "${HOME}/.cache/huggingface/token" ]]; then
  echo "❌ Error: No HuggingFace token found." >&2
  echo "Please accept the SAM 3 license at https://huggingface.co/facebook/sam3 and run:" >&2
  echo "  huggingface-cli login   (or: hf auth login)" >&2
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

# 4. Prepare isolated Python environment for PyTorch -> ONNX export
echo "=== Preparing SAM 3 ONNX export environment in ${VENV_DIR} ==="
UV_CMD=""
if command -v uv >/dev/null 2>&1; then
  UV_CMD="uv"
elif [[ -x "${HOME}/.local/bin/uv" ]]; then
  UV_CMD="${HOME}/.local/bin/uv"
fi

# The venv is reused across runs only while it has EXPORT_PYTHON_VERSION and was set up from
# the current lockfile. Otherwise it is rebuilt from scratch, because installing on top of it
# would leave behind packages that the lockfile no longer lists.
INSTALLED_REQUIREMENTS_FILE="${VENV_DIR}/installed_sam3_export_requirements.txt"
VENV_PYTHON_VERSION="$("${VENV_DIR}/bin/python" -c 'import sys; print("%d.%d" % sys.version_info[:2])' 2>/dev/null || true)"
if [[ "${VENV_PYTHON_VERSION}" != "${EXPORT_PYTHON_VERSION}" ]] ||
  ! cmp -s "${REQUIREMENTS_FILE}" "${INSTALLED_REQUIREMENTS_FILE}"; then
  # Only ever delete a directory that is a venv.
  if [[ -f "${VENV_DIR}/pyvenv.cfg" ]]; then
    rm -rf "${VENV_DIR}"
  fi
  if [[ -n "${UV_CMD}" ]]; then
    "${UV_CMD}" venv --python "${EXPORT_PYTHON_VERSION}" "${VENV_DIR}"
  elif ! "python${EXPORT_PYTHON_VERSION}" -m venv "${VENV_DIR}"; then
    echo "❌ Error: Could not create a Python ${EXPORT_PYTHON_VERSION} venv. Install uv (https://docs.astral.sh/uv/)" >&2
    echo "   or python${EXPORT_PYTHON_VERSION} with its venv module, then re-run this script." >&2
    exit 1
  fi
fi

# Clones (or updates) a git repository, checks out a pinned commit, and deletes untracked and
# ignored files (such as the build/ and *.egg-info outputs of an earlier install), so files
# from a previously checked-out commit cannot end up in the next build.
checkout_pinned_repo() {
  local url="$1" dir="$2" commit="$3"
  if [[ ! -d "${dir}/.git" ]]; then
    # Start over if an earlier clone was interrupted.
    rm -rf "${dir}"
    git clone --quiet "${url}" "${dir}"
  fi
  git -C "${dir}" fetch --quiet origin "${commit}"
  git -C "${dir}" checkout --quiet --force "${commit}"
  git -C "${dir}" clean --quiet -fdx
}

# Install torch, huggingface_hub, onnx, etc. from the hash-pinned lockfile, then the
# pinned upstream SAM 3 checkout without resolving any further dependencies (its
# runtime dependencies are part of the lockfile). Some lockfile entries (iopath) are
# only published as sdists, so building them must also use the hash-pinned setuptools
# and wheel from the lockfile rather than whatever is latest on the index.
echo "=== Installing export dependencies (torch, sam3@${SAM3_GIT_COMMIT:0:8}, onnx) ==="
SAM3_SRC_DIR="${VENV_DIR}/sam3"
checkout_pinned_repo https://github.com/facebookresearch/sam3.git "${SAM3_SRC_DIR}" "${SAM3_GIT_COMMIT}"
if [[ -n "${UV_CMD}" ]]; then
  # --build-constraints keeps build isolation but pins (and hash-checks) the build backend.
  "${UV_CMD}" pip install --python "${VENV_DIR}/bin/python" --index-url "${INDEX_URL}" --link-mode=copy --quiet --require-hashes --build-constraints "${REQUIREMENTS_FILE}" -r "${REQUIREMENTS_FILE}"
  "${UV_CMD}" pip install --python "${VENV_DIR}/bin/python" --link-mode=copy --quiet --no-build-isolation --no-deps --no-index "${SAM3_SRC_DIR}"
  "${UV_CMD}" pip check --python "${VENV_DIR}/bin/python"
else
  # The pip bundled with Python 3.12 has no --build-constraint, so first install the
  # hash-pinned setuptools and wheel wheels from the lockfile, then build the sdists
  # against them without build isolation.
  BUILD_REQUIREMENTS_FILE="${VENV_DIR}/sam3_export_build_requirements.txt"
  awk '/^(setuptools|wheel)==/ { keep = 1 } keep { print } keep && !/\\$/ { keep = 0 }' \
    "${REQUIREMENTS_FILE}" >"${BUILD_REQUIREMENTS_FILE}"
  if [[ "$(grep -cE '^(setuptools|wheel)==' "${BUILD_REQUIREMENTS_FILE}")" -ne 2 ]]; then
    echo "❌ Error: ${REQUIREMENTS_FILE} must pin setuptools and wheel." >&2
    exit 1
  fi
  PIP_INDEX_URL="${INDEX_URL}" "${VENV_DIR}/bin/pip" install --quiet --require-hashes --only-binary :all: --no-deps -r "${BUILD_REQUIREMENTS_FILE}"
  PIP_INDEX_URL="${INDEX_URL}" "${VENV_DIR}/bin/pip" install --quiet --require-hashes --no-build-isolation -r "${REQUIREMENTS_FILE}"
  "${VENV_DIR}/bin/pip" install --quiet --no-build-isolation --no-deps --no-index "${SAM3_SRC_DIR}"
  "${VENV_DIR}/bin/pip" check
fi
cp "${REQUIREMENTS_FILE}" "${INSTALLED_REQUIREMENTS_FILE}"

# Clone or update the perception pipeline tools repo for export_sam3_to_onnx.py
PIPELINE_SRC_DIR="${VENV_DIR}/foundationpose_perception_pipeline"
checkout_pinned_repo https://github.com/intrinsic-opensource/foundationpose_perception_pipeline.git "${PIPELINE_SRC_DIR}" "${PIPELINE_GIT_COMMIT}"

# 5. Download sam3.pt from HuggingFace and run ONNX export
echo "=== Downloading ${SAM3_FILENAME} from HuggingFace (${SAM3_REPO_ID}) ==="
CHECKPOINT_PATH="$("${VENV_DIR}/bin/python" -c "
from huggingface_hub import hf_hub_download
path = hf_hub_download(repo_id='${SAM3_REPO_ID}', filename='${SAM3_FILENAME}')
print(path)
")"

echo "✓ Downloaded checkpoint: ${CHECKPOINT_PATH}"
ACTUAL_SHA256="$(sha256sum "${CHECKPOINT_PATH}" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" == "${SAM3_EXPECTED_SHA256}" ]]; then
  echo "✓ Verified ${SAM3_FILENAME} SHA-256: ${ACTUAL_SHA256}"
else
  echo "❌ Error: ${SAM3_FILENAME} SHA-256 mismatch (expected ${SAM3_EXPECTED_SHA256}, got ${ACTUAL_SHA256})." >&2
  exit 1
fi
echo "=== Exporting SAM 3 ONNX graphs and BPE vocabulary to ${OUTPUT_DIR} ==="
SKIP_FLAG=""
if [[ "${FORCE}" -eq 0 ]]; then
  SKIP_FLAG="--skip-existing"
fi

PYTHONPATH="${PIPELINE_SRC_DIR}/src:${PYTHONPATH:-}" \
  "${VENV_DIR}/bin/python" "${PIPELINE_SRC_DIR}/tools/export_sam3_to_onnx.py" \
  --checkpoint "${CHECKPOINT_PATH}" \
  --output-dir "${OUTPUT_DIR}" \
  ${SKIP_FLAG}

# 6. Verify all 5 files
echo ""
echo "=== Verifying exported SAM 3 files in ${OUTPUT_DIR} ==="
for f in "${REQUIRED_FILES[@]}"; do
  if [[ ! -s "${OUTPUT_DIR}/${f}" ]]; then
    echo "❌ Error: Expected output file ${OUTPUT_DIR}/${f} is missing or empty!" >&2
    exit 1
  fi
  ls -lh "${OUTPUT_DIR}/${f}"
done

echo ""
echo "✓ Success! SAM 3 models are staged in ${OUTPUT_DIR}."
echo "You can now build and install the SAM 3 data asset with:"
if [[ "${OUTPUT_DIR}" == "${DEFAULT_OUTPUT_DIR}" ]]; then
  echo "  bazel build --config=intrinsic //intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:sam3_model_data_asset"
else
  echo "  bazel build --config=intrinsic --repo_env=NVIDIA_POSE_ESTIMATOR_MODELS_DIR=${OUTPUT_DIR} //intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:sam3_model_data_asset"
fi
echo "  inctl asset install bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/sam3_model_data_asset.bundle.tar"

