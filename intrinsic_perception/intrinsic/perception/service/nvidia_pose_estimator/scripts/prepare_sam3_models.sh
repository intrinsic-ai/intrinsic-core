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
FORCE=0

SAM3_REPO_ID="facebook/sam3"
SAM3_FILENAME="sam3.pt"
SAM3_EXPECTED_SHA256="9999e2341ceef5e136daa386eecb55cb414446a00ac2b55eb2dfd2f7c3cf8c9e"
# LINT.IfChange(sam3_commit)
SAM3_GIT_COMMIT="96914d2425f90a64f45ca977c2b5165418099543"
# LINT.ThenChange(//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/BUILD:sam3_version)
PIPELINE_GIT_COMMIT="4d79cf040a981d9375f1e8c8b0d71306b968ed67"

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

# 2. Check for CUDA GPU (required by upstream SAM 3 position encoding cache during export)
if ! command -v nvidia-smi >/dev/null 2>&1 || ! nvidia-smi -L >/dev/null 2>&1; then
  echo "❌ Error: A CUDA-capable GPU is required to export SAM 3 models to ONNX." >&2
  exit 1
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

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
  if [[ -n "${UV_CMD}" ]]; then
    "${UV_CMD}" venv --seed "${VENV_DIR}"
  elif ! python3 -m venv "${VENV_DIR}" 2>/dev/null; then
    rm -rf "${VENV_DIR}"
    python3 -m venv --without-pip "${VENV_DIR}"
    curl -sSL https://bootstrap.pypa.io/get-pip.py | "${VENV_DIR}/bin/python"
  fi
fi

# Install torch, huggingface_hub, onnx, and pinned upstream SAM 3 + perception pipeline
echo "=== Installing export dependencies (torch, sam3@${SAM3_GIT_COMMIT:0:8}, onnx) ==="
DEPS=(
  "setuptools<81"
  "numpy<2"
  torch
  torchvision
  huggingface_hub
  onnx
  onnxscript
  einops
  hydra-core
  omegaconf
  opencv-python-headless
  pycocotools
  scipy
  pyyaml
  psutil
  "git+https://github.com/facebookresearch/sam3.git@${SAM3_GIT_COMMIT}"
)
if [[ -n "${UV_CMD}" ]]; then
  "${UV_CMD}" pip install --python "${VENV_DIR}/bin/python" --index-url https://pypi.org/simple --link-mode=copy --quiet "${DEPS[@]}"
else
  PIP_INDEX_URL=https://pypi.org/simple "${VENV_DIR}/bin/pip" install --upgrade --quiet pip wheel "setuptools<81"
  PIP_INDEX_URL=https://pypi.org/simple "${VENV_DIR}/bin/pip" install --quiet "${DEPS[@]}"
fi

# Clone or update the perception pipeline tools repo for export_sam3_to_onnx.py
PIPELINE_SRC_DIR="${VENV_DIR}/foundationpose_perception_pipeline"
if [[ ! -d "${PIPELINE_SRC_DIR}" ]]; then
  git clone --quiet https://github.com/intrinsic-opensource/foundationpose_perception_pipeline.git "${PIPELINE_SRC_DIR}"
fi
git -C "${PIPELINE_SRC_DIR}" fetch --quiet origin "${PIPELINE_GIT_COMMIT}"
git -C "${PIPELINE_SRC_DIR}" checkout --quiet "${PIPELINE_GIT_COMMIT}"

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

