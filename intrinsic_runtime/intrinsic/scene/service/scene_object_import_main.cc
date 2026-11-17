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

// Startup code for WorldImportService server.

#include <cstdint>
#include <string>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/time/time.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/scene/service/scene_object_import_server.h"

ABSL_FLAG(int32_t, port, 10001, "port to listen on for world import service");
ABSL_FLAG(std::string, geometry_service_address, "",
          "Address to connect to for serializing and deserializing geometry");
ABSL_FLAG(std::string, asset_deployment_service_address, "",
          "Address to connect to for asset deployment service.");
ABSL_FLAG(std::string, workcell_cluster_service_address, "",
          "Address to connect to for workcell cluster service.");
ABSL_FLAG(absl::Duration, shutdown_grace_period, absl::Seconds(10),
          "grace period to wait for the server to shutdown");

namespace intrinsic {

absl::Status MainImpl() {
  absl::Notification handlers_registered;
  SceneObjectImportServerOptions options = {
      .port = absl::GetFlag(FLAGS_port),
      .geometry_service_address = absl::GetFlag(FLAGS_geometry_service_address),
      .asset_deployment_service_address =
          absl::GetFlag(FLAGS_asset_deployment_service_address),
      .workcell_cluster_service_address =
          absl::GetFlag(FLAGS_workcell_cluster_service_address),
      .shutdown_grace_period = absl::GetFlag(FLAGS_shutdown_grace_period),
      .handlers_registered = handlers_registered,
  };

  return RunSceneObjectImportServer(options);
}

}  // namespace intrinsic

int main(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);
  QCHECK_OK(intrinsic::MainImpl());
  return 0;
}
