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

#ifndef INTRINSIC_PERCEPTION_PUBLIC_CAMERA_H_
#define INTRINSIC_PERCEPTION_PUBLIC_CAMERA_H_

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/client_callback.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/perception/cameras/camera_config.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_config_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/util/grpc/connection_cache.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {

// Either returns the input string representing a server address or a nullopt if
// the input is empty.
inline std::optional<std::string> ServerAddressOrNulloptIfEmpty(
    absl::string_view server_address) {
  if (server_address.empty()) return std::nullopt;
  return std::string(server_address);
}

// Returns the gRPC interface URI for the CameraService.
absl::string_view CameraServiceInterfaceUri();

// Returns the gRPC interface URI for the CameraConfigService.
absl::string_view CameraConfigServiceInterfaceUri();

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
UnpackCameraConfig(const google::protobuf::Any& any);

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
GetCameraConfigFromHandle(
    const intrinsic_proto::resources::ResourceHandle& handle);

// A Camera class which provides direct access to a physical camera and its
// frames.
//
// The camera enables clients to use various image sources through a single
// interface (type erasure). Cameras cannot be copied and are move only.
// A single instance of a Camera is bound to exactly one concrete instance of
// a low-level camera driver.
class Camera {
 public:
  static absl::StatusOr<Camera> Create(
      const CameraIdentifier& camera_identifier,
      std::optional<std::string> camera_server_address = std::nullopt,
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout,
      std::optional<std::string> camera_server_instance = std::nullopt);
  static absl::StatusOr<Camera> CreateFromHandle(
      const intrinsic_proto::resources::ResourceHandle& handle,
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

  static absl::StatusOr<std::vector<CameraIdentifier>> ListAvailableCameras(
      std::optional<std::string> camera_server_address = std::nullopt,
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout,
      std::optional<std::string> camera_server_instance = std::nullopt);

  explicit Camera(std::unique_ptr<ImageSourceInterface> image_source_interface);

  // Cameras can be moved.
  Camera(Camera&&) = default;
  Camera& operator=(Camera&&) = default;

  // Cameras cannot be copied. Each camera instance is unique.
  Camera(const Camera&) = delete;
  Camera& operator=(const Camera&) = delete;

  // Describes the available sensors for this camera.
  absl::StatusOr<DescribeCameraResponse> DescribeCamera() const;

  // Captures from active sensors and returns the resulting sensor images.
  absl::StatusOr<CaptureResult> Capture(const CaptureArgs& args) const;

  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccess(
      absl::string_view name) const;

  absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      absl::string_view name) const;

  absl::StatusOr<CameraSetting> ReadCameraSetting(absl::string_view name) const;

  // Updates the user specified setting of the camera.
  // Important: The function may (depending on the driver) also stop active
  // streaming of the camera (e.g. if it is in free-running mode, or triggered
  // mode). This will require a restart of the streaming on the next call to
  // GetFrame() which maybe costly.
  absl::Status UpdateCameraSetting(const CameraSetting& camera_setting);

  // Add a callback that will receive new capture results
  absl::StatusOr<ImageSourceInterface::CallbackToken> AddCaptureCallback(
      absl::AnyInvocable<absl::Status(const CaptureResult&)> callback);

  // Remove a previously registered callback.
  absl::Status RemoveCaptureCallback(ImageSourceInterface::CallbackToken token);

  // Starts a stream with the given CaptureArgs and returns the images back into
  // any registered callbacks. Multiple streams may be supported by the
  // underlying driver, but that is not guaranteed, and may result in an error
  // for subsequent calls.
  absl::Status StartStream(const CaptureArgs& params);

  // Stops any currently running streams, no-op if there is no stream running.
  absl::Status StopStream();

  absl::Status GetFaultsStatus() const;

  absl::Status ClearFaults();

 private:
  std::unique_ptr<ImageSourceInterface> image_source_interface_;
};

// A small wrapper for a Camera which allows us to call its methods
// concurrently.
class ConcurrentCamera {
 public:
  static absl::StatusOr<std::shared_ptr<ConcurrentCamera>> Create(
      const CameraIdentifier& identifier);

  // Call any Camera member function through, e.g.
  //   concurrent_camera->Call(&Camera::Bar, param1, param2);
  // This prevents us from adding new functions here whenever we extend the
  // camera class' interface.
  template <bool error_on_disabled = true, typename M, typename T,
            typename... Args>
  ABSL_LOCKS_EXCLUDED(mutex_)
  auto Call(M T::* absl_nonnull pm, Args&&... args) ->
      typename std::invoke_result_t<decltype(pm), T&, Args&&...> {
    absl::MutexLock lock(mutex_);
    if constexpr (error_on_disabled) {
      if (is_disabled_) {
        return absl::FailedPreconditionError("Camera is disabled.");
      }
    }
    auto res = std::mem_fn(pm)(*camera_, std::forward<Args>(args)...);
    if (res.ok() || camera_->GetFaultsStatus().ok()) {
      return res;
    }
    LOG(INFO) << "Clearing faults and retrying.";
    INTR_RETURN_IF_ERROR(camera_->ClearFaults());
    return std::mem_fn(pm)(*camera_, std::forward<Args>(args)...);
  }

  // Extra functionality expected by the camera health service to disable/enable
  // a device.
  void SetDisabled(bool disabled) ABSL_LOCKS_EXCLUDED(mutex_);

  // Extra functionality expected by the camera health service to disable/enable
  // a device.
  bool IsDisabled() const ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  explicit ConcurrentCamera(std::unique_ptr<Camera> camera);

  mutable absl::Mutex mutex_;
  bool is_disabled_ ABSL_GUARDED_BY(mutex_) = false;
  const std::unique_ptr<Camera> camera_ ABSL_GUARDED_BY(mutex_);
};

struct ActiveCameraConfig {
  CameraIdentifier identifier;
  CameraConfig config;

  static absl::StatusOr<ActiveCameraConfig> FromProto(
      const intrinsic_proto::perception::v1::CameraConfig& camera_config_proto);

  intrinsic_proto::perception::v1::CameraConfig ToProto() const;
};

class CameraManager {
 public:
  CameraManager() = default;
  // The passed active camera will keep its connection open until the camera
  // manager is destroyed.
  explicit CameraManager(const ActiveCameraConfig& active_camera);

  absl::StatusOr<std::shared_ptr<ConcurrentCamera>> Get(
      const CameraIdentifier& identifier) ABSL_LOCKS_EXCLUDED(mutex_);
  absl::StatusOr<std::shared_ptr<ConcurrentCamera>> GetActiveCamera()
      ABSL_LOCKS_EXCLUDED(mutex_);
  absl::StatusOr<ActiveCameraConfig> GetActiveCameraConfig() const
      ABSL_LOCKS_EXCLUDED(mutex_);

  void SetActiveCamera(const ActiveCameraConfig& active_camera)
      ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  mutable absl::Mutex mutex_;
  std::optional<ActiveCameraConfig> active_camera_config_
      ABSL_GUARDED_BY(mutex_);
  std::shared_ptr<ConcurrentCamera> active_concurrent_camera_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<CameraIdentifier, std::weak_ptr<ConcurrentCamera>>
      camera_by_id_ ABSL_GUARDED_BY(mutex_);
};

class GrpcCamera {
 public:
  using CameraService = intrinsic_proto::perception::v1::CameraService;
  using CameraConfigService =
      intrinsic_proto::perception::v1::CameraConfigService;
  using CameraConnection = ConnectionCache<CameraService>::Connection;
  using CameraConfigConnection =
      ConnectionCache<CameraConfigService>::Connection;
  using Stub = CameraService::Stub;

  // Can be used to call any method of the CameraService, e.g.:
  // INTR_ASSIGN_OR_RETURN(
  //     intrinsic_proto::perception::v1::CaptureResponse response,
  //     grpc_camera.Call(&GrpcCamera::Stub::Capture, request,
  //                      connection_params));
  template <typename Request, typename Response>
  absl::StatusOr<Response> Call(
      grpc::Status (Stub::* absl_nonnull method)(
          grpc::ClientContext* absl_nonnull, const Request&,
          Response* absl_nonnull),
      const Request& request, const ConnectionParams& connection_params) const {
    INTR_ASSIGN_OR_RETURN(std::shared_ptr<CameraConnection> connection,
                          GetConnection(connection_params));
    std::unique_ptr<grpc::ClientContext> client_context =
        connection->channel->GetClientContextFactory()();
    Response response;
    INTR_RETURN_IF_ERROR(ToAbslStatus((connection->stub.get()->*method)(
        client_context.get(), request, &response)));
    return response;
  }

  // Returns a connection to the camera service.
  absl::StatusOr<std::shared_ptr<CameraConnection>> GetConnection(
      const ConnectionParams& connection_params) const;

  // Returns the camera config for the given connection params.
  // Note: The connection must implement
  // intrinsic_proto.perception.v1.CameraConfigService. Old versions of camera
  // Assets do not implement it.
  absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig> GetCameraConfig(
      const ConnectionParams& connection_params) const;

 private:
  mutable ConnectionCache<CameraService> connection_cache_;
  mutable ConnectionCache<CameraConfigService> config_connection_cache_;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PUBLIC_CAMERA_H_
