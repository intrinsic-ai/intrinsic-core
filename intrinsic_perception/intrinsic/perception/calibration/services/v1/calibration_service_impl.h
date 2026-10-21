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

#ifndef INTRINSIC_PERCEPTION_CALIBRATION_SERVICES_V1_CALIBRATION_SERVICE_IMPL_H_
#define INTRINSIC_PERCEPTION_CALIBRATION_SERVICES_V1_CALIBRATION_SERVICE_IMPL_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/channel.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/v1/asset_instances.grpc.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/calibration/pattern_detector.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/image.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/core/single_thread_executor.h"
#include "intrinsic/perception/proto/v1/calibration_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/calibration_service.pb.h"
#include "intrinsic/perception/proto/v1/camera_setup.pb.h"
#include "intrinsic/perception/proto/v1/capture_data.pb.h"
#include "intrinsic/perception/proto/v1/pattern_detection_config.pb.h"
#include "intrinsic/perception/proto/v1/pattern_detection_result.pb.h"
#include "intrinsic/perception/storage/capture_result_storage.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic::perception {

// Implementation of the CalibrationService.
class CalibrationServiceImpl final
    : public intrinsic_proto::perception::v1::CalibrationService::Service {
 public:
  // The pointer arguments of the constructor can be null, in which case
  // the corresponding stubs will be created internally.
  explicit CalibrationServiceImpl(
      std::unique_ptr<::google::longrunning::Operations::StubInterface>
          operations_stub,
      std::unique_ptr<
          intrinsic_proto::assets::v1::AssetInstances::StubInterface>
          asset_instances_stub,
      std::unique_ptr<
          intrinsic_proto::assets::AssetDeploymentService::StubInterface>
          asset_deployment_service_stub,
      std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
          object_world_service_stub = nullptr,
      KeyValueStoreFactory kvstore_factory = CreateKeyValueStoreFactory());

  grpc::Status Initialize(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::InitializeRequest* request,
      intrinsic_proto::perception::v1::InitializeResponse* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status CaptureData(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::CaptureDataRequest* request,
      intrinsic_proto::perception::v1::CaptureDataResponse* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status Calibrate(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::CalibrationRequest* request,
      intrinsic_proto::perception::v1::CalibrationResult* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status Validate(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::ValidateRequest* request,
      intrinsic_proto::perception::v1::ValidationResult* response) override
      ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status Save(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::CalibrationResult* request,
      google::protobuf::Empty* response) override ABSL_LOCKS_EXCLUDED(mutex_);

  grpc::Status DeleteCapture(
      grpc::ServerContext* context,
      const intrinsic_proto::perception::v1::DeleteCaptureRequest* request,
      google::protobuf::Empty* response) override ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  perception::GrpcCamera grpc_camera_;
  PubSub pubsub_;
  const KeyValueStoreFactory kvstore_factory_;

  absl::Mutex mutex_;
  int64_t next_capture_id_ ABSL_GUARDED_BY(mutex_) = 0;
  std::string session_id_ ABSL_GUARDED_BY(mutex_);

  // Data point for calibration.
  struct CalibrationDataPoint {
    // One pattern detection per camera. Follows the same order as
    // `camera_resource_handles` in the InitializeRequest.
    std::vector<intrinsic_proto::perception::v1::PatternDetection>
        pattern_detections;
    std::vector<std::optional<CameraParams>> camera_params_from_capture;
    std::vector<std::optional<Pose3d>> camera_ts_sensor_from_capture;
    // Optional field for robot pose, in case camera-to-robot calibration should
    // be performed.
    std::optional<intrinsic_proto::Pose> base_t_flange;
    std::string capture_id;
  };
  std::vector<CalibrationDataPoint> calibration_data_ ABSL_GUARDED_BY(mutex_);
  intrinsic_proto::perception::v1::PatternDetectionConfig
      pattern_detection_config_ ABSL_GUARDED_BY(mutex_);
  std::optional<PatternDetector> pattern_detector_ ABSL_GUARDED_BY(mutex_);
  std::vector<Publisher> sharpness_publishers_ ABSL_GUARDED_BY(mutex_);
  std::vector<CameraParams> cached_intrinsic_calibration_results_
      ABSL_GUARDED_BY(mutex_);

  struct CameraInfo {
    intrinsic_proto::resources::ResourceHandle camera_resource_handle;
    bool is_multi_sensor;
    ConnectionParams connection_params;
    Dimensions image_dimensions;
    absl::flat_hash_map<int64_t, PostProcessing> post_processing_by_sensor_id;
    int64_t default_sensor_id;
  };
  std::vector<CameraInfo> camera_info_ ABSL_GUARDED_BY(mutex_);
  std::optional<world::KinematicObject> robot_object_ ABSL_GUARDED_BY(mutex_);
  std::optional<world::Frame> robot_flange_ ABSL_GUARDED_BY(mutex_);
  std::optional<world::WorldObject> calibration_object_ ABSL_GUARDED_BY(mutex_);
  std::string execute_world_id_ ABSL_GUARDED_BY(mutex_);
  std::string initial_world_id_ ABSL_GUARDED_BY(mutex_);

  absl::Mutex coverage_mutex_;
  std::vector<Image<Gray8u>> coverage_maps_ ABSL_GUARDED_BY(coverage_mutex_);
  std::vector<Publisher> coverage_map_publishers_
      ABSL_GUARDED_BY(coverage_mutex_);

  absl::Mutex stub_mutex_;
  std::shared_ptr<grpc::Channel> ingress_channel_ ABSL_GUARDED_BY(stub_mutex_);
  std::unique_ptr<::google::longrunning::Operations::StubInterface>
      operations_stub_ ABSL_GUARDED_BY(stub_mutex_);
  std::unique_ptr<intrinsic_proto::assets::v1::AssetInstances::StubInterface>
      asset_instances_stub_ ABSL_GUARDED_BY(stub_mutex_);
  std::unique_ptr<
      intrinsic_proto::assets::AssetDeploymentService::StubInterface>
      asset_deployment_service_stub_ ABSL_GUARDED_BY(stub_mutex_);
  std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub_ ABSL_GUARDED_BY(stub_mutex_);

  // Helper functions to get or create stubs.
  std::shared_ptr<grpc::Channel> GetOrCreateIngressChannel()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(stub_mutex_);
  intrinsic_proto::assets::AssetDeploymentService::StubInterface*
  GetOrCreateAssetDeploymentServiceStub() ABSL_LOCKS_EXCLUDED(stub_mutex_);
  intrinsic_proto::assets::v1::AssetInstances::StubInterface*
  GetOrCreateAssetInstancesStub() ABSL_LOCKS_EXCLUDED(stub_mutex_);
  google::longrunning::Operations::StubInterface* GetOrCreateOperationsStub()
      ABSL_LOCKS_EXCLUDED(stub_mutex_);
  std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
  GetOrCreateObjectWorldServiceStub() ABSL_LOCKS_EXCLUDED(stub_mutex_);

  // Helper function to retrieve camera config.
  absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
  GetCameraConfigFromAssetInstances(
      absl::string_view camera_resource_handle_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Helper functions that contain the actual calibration logic.
  absl::StatusOr<std::vector<
      intrinsic_proto::perception::v1::CameraToRobotCalibrationRequest>>
  CreateCameraToRobotCalibrationRequests(
      intrinsic_proto::perception::v1::CameraSetup type,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::StatusOr<std::vector<CameraParams>>
  GetCameraParamsFromCachedResultsOrData(const CalibrationDataPoint& data_point)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  std::vector<Pose3d> GetCameraTsSensor(const CalibrationDataPoint& data_point)
      const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status CalibrateIntrinsics(
      const intrinsic_proto::perception::v1::CalibrationRequest* request,
      intrinsic_proto::perception::v1::CalibrationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status CalibrateCameraToRobot(
      const intrinsic_proto::perception::v1::CalibrationRequest* request,
      intrinsic_proto::perception::v1::CalibrationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status CalibrateCameraToCamera(
      const intrinsic_proto::perception::v1::CalibrationRequest* request,
      intrinsic_proto::perception::v1::CalibrationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  struct ProvidedCaptureDetection {
    std::optional<CameraParams> camera_params;
    std::optional<intrinsic_proto::perception::v1::PatternDetection>
        pattern_detection;
  };
  absl::StatusOr<ProvidedCaptureDetection> PatternDetectionFromProvidedCapture(
      const intrinsic_proto::perception::v1::CaptureData& capture_data,
      absl::string_view capture_name, absl::string_view camera_name,
      grpc::ServerContext* context) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Converts provided captures into calibration data points, one per capture.
  absl::StatusOr<std::vector<CalibrationDataPoint>>
  CalibrationDataFromProvidedCaptures(
      const google::protobuf::RepeatedPtrField<
          intrinsic_proto::perception::v1::CaptureDataList>& captures,
      grpc::ServerContext* context) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status ValidateIntrinsics(
      const intrinsic_proto::perception::v1::ValidateRequest* request,
      intrinsic_proto::perception::v1::ValidationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status ValidateCameraToRobot(
      const intrinsic_proto::perception::v1::ValidateRequest* request,
      intrinsic_proto::perception::v1::ValidationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status ValidateCameraToCamera(
      const intrinsic_proto::perception::v1::ValidateRequest* request,
      intrinsic_proto::perception::v1::ValidationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status CalibrateCaptureToCapture(
      const intrinsic_proto::perception::v1::CalibrationRequest* request,
      intrinsic_proto::perception::v1::CalibrationResult* response,
      absl::Span<const CalibrationDataPoint> calibration_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  grpc::Status SaveCameraPosesToInitialWorld(
      const intrinsic_proto::perception::v1::CalibrationResult* request)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  grpc::Status SaveIntrinsicCalibrationResults(
      const intrinsic_proto::perception::v1::CalibrationResult* request)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Status ScheduleCoverageMapsUpdate(
      std::vector<std::vector<Vector2f>>&& image_points_per_camera,
      std::function<absl::StatusOr<Image<Gray8u>>(const std::vector<Vector2f>&,
                                                  const Image<Gray8u>&)>
          update_function);

  // For work on time-consuming operations.
  // Must be declared last so that upon destruction, its destructor is invoked
  // first and blocks until all pending background tasks finish before any other
  // member variables are destroyed.
  SingleThreadExecutor executor_;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CALIBRATION_SERVICES_V1_CALIBRATION_SERVICE_IMPL_H_
