// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/geometry/gltf/gltf_io.h"

#include <sstream>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "tiny_gltf.h"

namespace intrinsic::geo {
namespace {

void ConfigurePassthroughImageLoader(tinygltf::TinyGLTF& loader) {
  loader.SetImageLoader(
      [](tinygltf::Image*, const int, std::string*, std::string*, int, int,
         const unsigned char*, int, void*) { return true; },
      nullptr);
}

// The first bytes of every GLB file.
constexpr absl::string_view kGlbMagic = "glTF";

// Loads `bytes` as binary GLB. Stores tinygltf messages in `diagnostics` if
// non-null, or logs warnings otherwise.
absl::StatusOr<tinygltf::Model> LoadGlb(absl::string_view bytes,
                                        GltfParseDiagnostics* diagnostics) {
  tinygltf::TinyGLTF loader;
  ConfigurePassthroughImageLoader(loader);
  tinygltf::Model model;
  std::string err;
  std::string warn;
  const bool ok = loader.LoadBinaryFromMemory(
      &model, &err, &warn, reinterpret_cast<const unsigned char*>(bytes.data()),
      bytes.size());
  absl::Status status = ok ? absl::OkStatus()
                           : absl::InvalidArgumentError(
                                 absl::StrCat("Failed to parse GLB: ", err));
  if (diagnostics != nullptr) {
    diagnostics->errors = std::move(err);
    diagnostics->warnings = std::move(warn);
  } else if (!warn.empty()) {
    LOG(WARNING) << "tinygltf warnings while parsing glTF: " << warn;
  }
  if (!status.ok()) {
    return status;
  }
  return model;
}
}  // namespace

absl::StatusOr<tinygltf::Model> ParseGlb(absl::string_view glb_bytes,
                                         GltfParseDiagnostics* diagnostics) {
  if (diagnostics != nullptr) {
    *diagnostics = {};
  }
  if (!absl::StartsWith(glb_bytes, kGlbMagic)) {
    return absl::InvalidArgumentError(
        "Failed to parse GLB: Invalid GLB magic bytes");
  }
  return LoadGlb(glb_bytes, diagnostics);
}

absl::StatusOr<std::string> SerializeGltfToGlb(const tinygltf::Model& model) {
  std::ostringstream ss;
  tinygltf::TinyGLTF writer;
  if (!writer.WriteGltfSceneToStream(&model, ss,
                                     /*prettyPrint=*/false,
                                     /*writeBinary=*/true)) {
    return absl::InternalError("Failed to write glTF binary");
  }
  return ss.str();
}

}  // namespace intrinsic::geo
