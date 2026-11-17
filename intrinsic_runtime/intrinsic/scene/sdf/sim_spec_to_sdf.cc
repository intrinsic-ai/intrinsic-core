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

#include "intrinsic/scene/sdf/sim_spec_to_sdf.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "intrinsic/scene/sdf/xml_utils.h"
#include "tinyxml2.h"

namespace intrinsic {
namespace sdf {

namespace {
using ::intrinsic_proto::scene_object::v1::SimulationSpec;
}  // namespace

absl::StatusOr<std::string> SimSpecToSdf(const SimulationSpec& sim_spec) {
  std::string sdf;
  if (sim_spec.has_multi_camera_plugin()) {
    const auto& mc_spec = sim_spec.multi_camera_plugin();
    absl::StrAppend(
        &sdf,
        "<plugin name=\"MultiCameraPlugin0\" "
        "filename=\"static://intrinsic::simulation::MultiCameraPlugin\">");
    for (const auto& sensor : mc_spec.sensors()) {
      absl::StrAppend(&sdf, "<sensor>");
      absl::SubstituteAndAppend(&sdf, "<id>$0</id>", sensor.id());
      absl::SubstituteAndAppend(&sdf, "<name>$0</name>",
                                EscapeXml(sensor.name()));
      absl::StrAppend(&sdf, "</sensor>");
    }
    absl::StrAppend(&sdf, "</plugin>");
  }

  for (const auto& extra_plugin : sim_spec.extra_inlined_plugins()) {
    absl::StrAppend(&sdf, extra_plugin);
  }

  return sdf;
}

}  // namespace sdf
}  // namespace intrinsic
