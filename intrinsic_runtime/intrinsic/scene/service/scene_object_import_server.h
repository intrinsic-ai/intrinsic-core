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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_SERVER_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_SERVER_H_

#include <string>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace intrinsic {

struct SceneObjectImportServerOptions {
  int port;
  std::string geometry_service_address;
  std::string asset_deployment_service_address;
  std::string workcell_cluster_service_address;
  absl::Duration shutdown_grace_period;

  // This notification will be notified when the signal handlers are
  // registered and the server is ready to handle requests.
  absl::Notification& handlers_registered;
};

// Runs the SceneObjectImport gRPC server.
// This function will block until the server is shut down.
absl::Status RunSceneObjectImportServer(
    const SceneObjectImportServerOptions& options);

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_IMPORT_SERVER_H_
