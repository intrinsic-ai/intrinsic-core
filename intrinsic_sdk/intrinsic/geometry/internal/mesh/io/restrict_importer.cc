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

#include "intrinsic/geometry/internal/mesh/io/restrict_importer.h"

#include <assimp/BaseImporter.h>

#include <algorithm>
#include <assimp/Importer.hpp>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"

namespace intrinsic::geo {

void RestrictImporterToExtension(Assimp::Importer& importer,
                                 absl::string_view extension) {
  const std::string lower_ext =
      absl::AsciiStrToLower(absl::StripPrefix(extension, "."));

  auto to_remove_view =
      std::views::iota(size_t{0}, importer.GetImporterCount()) |
      std::views::transform(
          [&importer](size_t i) { return importer.GetImporter(i); }) |
      std::views::filter(
          [](Assimp::BaseImporter* imp) { return imp != nullptr; }) |
      std::views::filter([&lower_ext](Assimp::BaseImporter* imp) {
        std::set<std::string> supported_extensions;
        imp->GetExtensionList(supported_extensions);
        return !supported_extensions.contains(lower_ext);
      });

  std::vector<Assimp::BaseImporter*> to_remove(to_remove_view.begin(),
                                               to_remove_view.end());

  std::ranges::for_each(to_remove, [&importer](Assimp::BaseImporter* imp) {
    importer.UnregisterLoader(imp);
    // Manually unregistered loaders are not freed by Assimp's destructor.
    // Wrapping the unregistered pointer in unique_ptr ensures safe RAII
    // cleanup.
    std::unique_ptr<Assimp::BaseImporter> owned_imp(imp);
  });
}

}  // namespace intrinsic::geo
