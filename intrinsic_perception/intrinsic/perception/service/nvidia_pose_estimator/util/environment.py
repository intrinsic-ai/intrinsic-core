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

"""Environment setup and shared-library preloading for NVIDIA Pose Estimator.

Configures `sys.path`, preloads CUDA and TensorRT shared libraries with
`RTLD_GLOBAL` so Python bindings and C++ extensions resolve symbols at import
time, and locates `libfoundation_pose_nvidia.so` (from `$FP_LIBRARY`,
`$FP_INFERENCE_LIB_PATH`, `/usr/lib/x86_64-linux-gnu` in the container, or Bazel
runfiles in local builds/tests).
"""

from __future__ import annotations

import ctypes
import logging
import os
from pathlib import Path
import re
import sys

from incode.util.runfiles import runfiles

_SYSTEM_LIB_DIR = Path("/usr/lib/x86_64-linux-gnu")
_FP_LIBRARY_DIRNAME = "foundation-pose-inference-library"
# cc_binary emits the versioned .so.0.1.0 in runfiles, whereas pkg_tar creates
# the unversioned .so symlink inside the container image.
_FP_LIBRARY_SONAME = "libfoundation_pose_nvidia.so"
_FP_LIBRARY_VERSIONED = "libfoundation_pose_nvidia.so.0.1.0"
_RUNFILES_FP_LIBRARY_SO = (
    f"foundation_pose_inference_library/{_FP_LIBRARY_VERSIONED}"
)
_RUNFILES_BINDINGS = "intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/third_party/bindings"
_LIB_PATH_ENV = "FP_INFERENCE_LIB_PATH"
# Set to "1" to tolerate a missing FoundationPose library (unit tests only).
_ALLOW_MISSING_LIBRARY_ENV = "FP_ALLOW_MISSING_LIBRARY"
_ELF_MAGIC = b"\x7fELF"
_SHARED_OBJECT_NAME_RE = re.compile(r"^lib[\w.-]+\.so(\.\d+)*$")
_INITIALIZED: bool = False


def _rlocation(runfiles_path: str) -> Path | None:
  """Resolves a repository-relative runfiles path, or None if unavailable."""
  try:
    resolved = runfiles.RlocationCurrentRepository(runfiles_path)
  except (AssertionError, OSError):
    return None
  return Path(resolved) if resolved else None


def _rlocation_external(runfiles_path: str) -> Path | None:
  """Resolves a runfiles path rooted at an external repository, or None."""
  r = runfiles.Create()
  if r is None:
    return None
  try:
    resolved = r.Rlocation(runfiles_path)
  except (AssertionError, OSError):
    return None
  return Path(resolved) if resolved else None


def add_bindings_to_syspath() -> None:
  """Adds TensorRT / CUDA Python binding candidate directories to sys.path."""
  pkg_dir = Path(__file__).resolve().parent.parent
  binding_candidates = [
      Path("/bindings"),
      Path("/tmp/trt_bindings"),
      pkg_dir / "third_party" / "bindings",
  ]
  if rf := _rlocation(_RUNFILES_BINDINGS):
    binding_candidates.insert(0, rf)

  for c in binding_candidates:
    if c.is_dir() and str(c) not in sys.path:
      sys.path.insert(0, str(c))


def _get_sdk_lib_dirs() -> list[Path]:
  """Returns candidate directories containing compiled SDK and TensorRT libraries."""
  pkg_dir = Path(__file__).resolve().parent.parent
  dirs = [
      _SYSTEM_LIB_DIR,
      pkg_dir / _FP_LIBRARY_DIRNAME / "build",
      pkg_dir / _FP_LIBRARY_DIRNAME / "lib64",
      pkg_dir / "third_party" / _FP_LIBRARY_DIRNAME / "build",
      pkg_dir / "third_party" / _FP_LIBRARY_DIRNAME / "lib64",
      Path("/opt/foundation-pose-inference-library/lib64"),
  ]

  env_raw = os.environ.get(_LIB_PATH_ENV, "")
  for entry in reversed(env_raw.split(os.pathsep)):
    if entry:
      dirs.insert(0, Path(entry).expanduser())

  if rf := _rlocation_external(_RUNFILES_FP_LIBRARY_SO):
    dirs.append(rf.parent)

  return [d for d in dirs if d.is_dir()]


def _is_elf_shared_object(path: Path) -> bool:
  """Returns True if `path` is an ELF shared object with a valid .so name."""
  if not _SHARED_OBJECT_NAME_RE.match(path.name) or not path.is_file():
    return False
  try:
    with path.open("rb") as f:
      return f.read(4) == _ELF_MAGIC
  except OSError:
    return False


def preload_tensorrt_libs() -> None:
  """Preloads TensorRT & CUDA C++ shared libraries so Python bindings link dynamically."""
  libs_to_load = (
      "libpng16.so",
      "libcudart.so",
      "libnvrtc.so",
      "libnvrtc-builtins.so",
      "libnvinfer.so",
      "libnvonnxparser.so",
      "libnvinfer_plugin.so",
      "libnvinfer_vc_plugin.so",
  )
  for d in _get_sdk_lib_dirs():
    for res_lib in sorted(d.glob("libnvinfer_builder_resource*.so*")):
      if not _is_elf_shared_object(res_lib):
        continue
      try:
        ctypes.CDLL(str(res_lib.resolve()), mode=ctypes.RTLD_GLOBAL)
      except OSError:
        pass
    for lib_name in libs_to_load:
      candidates = sorted(
          (p for p in d.glob(f"{lib_name}*") if _is_elf_shared_object(p)),
          key=lambda p, target=lib_name: (
              p.name != target,
              len(p.name),
              p.name,
          ),
      )
      for cand in candidates:
        try:
          ctypes.CDLL(str(cand.resolve()), mode=ctypes.RTLD_GLOBAL)
          break
        except OSError:
          pass


def resolve_foundation_pose_library() -> Path | None:
  """Resolves the libfoundation_pose_nvidia.so shared library path."""
  if (env_path := os.environ.get("FP_LIBRARY")) and os.path.isfile(env_path):
    return Path(env_path).resolve()

  for d in _get_sdk_lib_dirs():
    for name in (_FP_LIBRARY_SONAME, _FP_LIBRARY_VERSIONED):
      cand = d / name
      if cand.is_file():
        return cand.resolve()
  return None


def init_environment() -> None:
  """Initializes sys.path, preloads CUDA/TRT libraries, and sets FP_LIBRARY.

  Raises:
    RuntimeError: If the FoundationPose shared library cannot be found and
      `FP_ALLOW_MISSING_LIBRARY=1` is not set.
  """
  global _INITIALIZED
  if _INITIALIZED:
    return

  add_bindings_to_syspath()
  preload_tensorrt_libs()
  fp_lib = resolve_foundation_pose_library()
  if fp_lib:
    os.environ["FP_LIBRARY"] = str(fp_lib)
    logging.info("Auto-resolved FP_LIBRARY=%s", fp_lib)
    _INITIALIZED = True
    return
  message = (
      f"Could not resolve {_FP_LIBRARY_SONAME}: searched"
      f" {[str(d) for d in _get_sdk_lib_dirs()]} and $FP_LIBRARY. If this is"
      " the service image, it was built without"
      " --//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator:build_full_image=True"
      " and cannot run FoundationPose."
  )
  if os.environ.get(_ALLOW_MISSING_LIBRARY_ENV) == "1":
    logging.warning(
        "%s (%s=1, continuing)", message, _ALLOW_MISSING_LIBRARY_ENV
    )
    _INITIALIZED = True
    return
  raise RuntimeError(message)
