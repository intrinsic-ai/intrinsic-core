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

#include "intrinsic/perception/calibration/services/v1/calibration_service_impl.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/asset_deployment.pb.h"
#include "intrinsic/assets/proto/v1/asset_instances.grpc.pb.h"
#include "intrinsic/assets/proto/v1/asset_instances.pb.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/perception/calibration/camera_to_robot_calibration.h"
#include "intrinsic/perception/calibration/charuco_pattern_utils.h"
#include "intrinsic/perception/calibration/image_quality_check.h"
#include "intrinsic/perception/calibration/intrinsic_calibration.h"
#include "intrinsic/perception/calibration/intrinsic_calibration_utils.h"
#include "intrinsic/perception/calibration/metrics.h"
#include "intrinsic/perception/calibration/pattern_detector.h"
#include "intrinsic/perception/calibration/stereo_calibration.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/capture_result_helper.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/config_name_utils.h"
#include "intrinsic/perception/core/drawing.h"
#include "intrinsic/perception/core/eigen_types.h"
#include "intrinsic/perception/core/image_traits.h"
#include "intrinsic/perception/core/opencv_wrapper.h"
#include "intrinsic/perception/core/operators.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/perception/logging/sensor_image.h"
#include "intrinsic/perception/proto/v1/camera_params.pb.h"
#include "intrinsic/perception/proto/v1/camera_setup.pb.h"
#include "intrinsic/perception/proto/v1/camera_to_robot_calibration.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_params.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/dimensions.h"
#include "intrinsic/perception/proto_conversion/v1/distortion_params.h"
#include "intrinsic/perception/proto_conversion/v1/image_buffer.h"
#include "intrinsic/perception/proto_conversion/v1/intrinsic_params.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "opencv2/calib3d.hpp"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"

namespace intrinsic::perception {
namespace {
constexpr absl::Duration kDefaultImageGrabbingTimeout = absl::Seconds(5);
constexpr int kMinimumCommonMarkerPointsForCameraToCameraCalibration = 10;
constexpr int kMinimumCameraDetectionsForCameraToCameraCalibration = 3;
constexpr int kMinimumCameraDetectionsForCaptureToCaptureCalibration = 1;
constexpr absl::string_view kIngressAddress =
    "istio-ingressgateway.app-ingress.svc.cluster.local:80";
constexpr int kDefaultReferenceCameraId = 0;
constexpr int kDefaultReferenceCaptureId = 0;
constexpr absl::string_view kDefaultBeliefWorldId = "world";
constexpr absl::string_view kDefaultInitialWorldId = "init_world";
constexpr absl::string_view kCalibrationKVStoragePrefix =
    "calibration/sessions/";
constexpr int kMaxCalibrationDataPoints = 100;
constexpr double kCoverageMapScaleFactor = 0.05;
constexpr int kMinNumberPatternImagePoints = 10;
constexpr double kHorizontalPixelWiseDifferenceFactorForRedundantDetections =
    0.01;
constexpr int32_t kBlurDetectionFilterSize = 60;
constexpr double kBlurDetectionFilterThreshold = 1.5;

absl::StatusOr<StereoCalibrationResult> ComputeStereoCalibrationResult(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        reference_pattern_detections,
    const CameraParams& reference_camera_params,
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        camera_i_pattern_detections,
    const CameraParams& camera_i_params, size_t camera_index,
    size_t min_common_detections) {
  std::vector<std::vector<Vector3f>> pair_object_points;
  std::vector<std::vector<Vector2f>> pair_image_points0;
  std::vector<std::vector<Vector2f>> pair_image_points_i;

  if (reference_pattern_detections.size() !=
      camera_i_pattern_detections.size()) {
    return absl::FailedPreconditionError(
        "The number of reference pattern detections must match the number of "
        "pattern detections for the i-th camera when computing the stereo "
        "calibration.");
  }

  for (size_t k = 0; k < reference_pattern_detections.size(); ++k) {
    const auto& detection0 = reference_pattern_detections[k];
    const auto& detection_i = camera_i_pattern_detections[k];

    if (detection0.image_points().empty() ||
        detection_i.image_points().empty()) {
      continue;
    }

    if (detection0.marker_ids().empty() || detection_i.marker_ids().empty()) {
      LOG(INFO) << "Skipping capture " << k << " for camera pair ("
                << kDefaultReferenceCameraId << ", " << camera_index
                << ") due to empty marker IDs.";
      continue;
    }
    if (detection0.marker_ids_size() != detection0.marker_points_size() ||
        detection0.marker_ids_size() != detection0.image_points_size()) {
      LOG(INFO) << "Skipping capture " << k << " for camera pair ("
                << kDefaultReferenceCameraId << ", " << camera_index
                << ") due to mismatched marker IDs, marker points, and image "
                   "points sizes for reference camera.";
      continue;
    }
    if (detection_i.marker_ids_size() != detection_i.marker_points_size() ||
        detection_i.marker_ids_size() != detection_i.image_points_size()) {
      LOG(INFO) << "Skipping capture " << k << " for camera pair ("
                << kDefaultReferenceCameraId << ", " << camera_index
                << ") due to mismatched marker IDs, marker points, and image "
                   "points sizes for camera "
                << camera_index << ".";
      continue;
    }

    std::vector<Vector3f> current_object_points;
    std::vector<Vector2f> current_image_points0;
    std::vector<Vector2f> current_image_points_i;

    for (int m0 = 0; m0 < detection0.marker_ids_size(); ++m0) {
      int32_t id0 = detection0.marker_ids(m0);
      for (int mi = 0; mi < detection_i.marker_ids_size(); ++mi) {
        if (detection_i.marker_ids(mi) == id0) {
          current_object_points.push_back(
              Vector3f(detection0.marker_points(m0).x(),
                       detection0.marker_points(m0).y(),
                       detection0.marker_points(m0).z()));
          current_image_points0.push_back(
              Vector2f(detection0.image_points(m0).x(),
                       detection0.image_points(m0).y()));
          current_image_points_i.push_back(
              Vector2f(detection_i.image_points(mi).x(),
                       detection_i.image_points(mi).y()));
          break;
        }
      }
    }

    if (current_object_points.size() >=
        kMinimumCommonMarkerPointsForCameraToCameraCalibration) {
      pair_object_points.push_back(std::move(current_object_points));
      pair_image_points0.push_back(std::move(current_image_points0));
      pair_image_points_i.push_back(std::move(current_image_points_i));
    } else {
      LOG(INFO) << "Skipping capture " << k << " for camera pair ("
                << kDefaultReferenceCameraId << ", " << camera_index
                << ") due to insufficient common marker points: "
                << current_object_points.size() << " < "
                << kMinimumCommonMarkerPointsForCameraToCameraCalibration;
    }
  }

  if (pair_object_points.size() < min_common_detections) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Camera-to-camera calibration failed for camera pair (",
        kDefaultReferenceCameraId, ", ", camera_index, "): requires at least ",
        min_common_detections, " detections with at least ",
        kMinimumCommonMarkerPointsForCameraToCameraCalibration,
        " common marker points. Found ", pair_object_points.size()));
  }

  return CalibrateStereoExtrinsic(pair_object_points, pair_image_points0,
                                  pair_image_points_i, reference_camera_params,
                                  camera_i_params);
}

struct WorldUpdateParameters {
  world::WorldObject camera;
  world::Frame camera_frame;
  world::KinematicObject robot;
  world::Frame flange;
  std::optional<world::WorldObject> calibration_object;
};

// Update camera pose wrt robot base or flange in Execute world.
absl::Status UpdateCameraPoseInWorld(
    const intrinsic_proto::perception::v1::CameraToRobotCalibrationResult&
        calibration_result,
    const WorldUpdateParameters& world_update_params,
    world::ObjectWorldClient& world) {
  if (calibration_result.has_moving_camera_result_poses()) {
    // For moving camera, the result gives flange_t_camera. We need to update
    // the transform between the robot's flange and the camera object.
    const Pose3d flange_t_camera = *FromProto(
        calibration_result.moving_camera_result_poses().flange_t_camera());
    INTR_RETURN_IF_ERROR(world.UpdateTransform(
        world_update_params.flange, world_update_params.camera_frame,
        /*node_to_update=*/world_update_params.camera, flange_t_camera));
  } else if (calibration_result.has_stationary_camera_result_poses()) {
    // For stationary camera, the result gives base_t_camera. We need to update
    // the transform between the world root and the camera object.
    Pose3d base_t_camera = *FromProto(
        calibration_result.stationary_camera_result_poses().base_t_camera());
    INTR_RETURN_IF_ERROR(world.UpdateTransform(
        world_update_params.robot, world_update_params.camera_frame,
        /*node_to_update=*/world_update_params.camera, base_t_camera));
  } else {
    return absl::InvalidArgumentError(
        "Camera-to-robot calibration result poses are missing.");
  }
  return absl::OkStatus();
}

// Update camera pose wrt a reference camera in Execute world.
absl::Status UpdateCameraPoseInWorld(
    const intrinsic_proto::perception::v1::StereoCalibrationResult&
        calibration_result,
    const world::WorldObject& reference_camera,
    const world::WorldObject& camera, world::ObjectWorldClient& world) {
  if (!calibration_result.has_cam_t_cam0()) {
    return absl::InternalError(
        "Camera-to-camera calibration result pose missing.");
  }
  Pose3d cam_t_cam0 = *FromProto(calibration_result.cam_t_cam0());
  INTR_ASSIGN_OR_RETURN(const auto camera_frame,
                        camera.GetFrame(SensorFrameName()));
  INTR_ASSIGN_OR_RETURN(const auto reference_camera_frame,
                        reference_camera.GetFrame(SensorFrameName()));
  INTR_RETURN_IF_ERROR(
      world.UpdateTransform(camera_frame, reference_camera_frame,
                            /*node_to_update=*/camera, cam_t_cam0));
  return absl::OkStatus();
}

absl::Status UpdateCalibrationObjectInWorld(
    const intrinsic_proto::perception::v1::CameraToRobotCalibrationResult&
        calibration_result,
    const WorldUpdateParameters& world_update_params,
    world::ObjectWorldClient& world) {
  if (!world_update_params.calibration_object.has_value()) {
    return absl::OkStatus();
  }
  if (calibration_result.has_moving_camera_result_poses()) {
    Pose3d base_t_object = *FromProto(
        calibration_result.moving_camera_result_poses().base_t_object());
    INTR_RETURN_IF_ERROR(world.UpdateTransform(
        world_update_params.robot,
        world_update_params.calibration_object.value(),
        /*node_to_update=*/world_update_params.calibration_object.value(),
        base_t_object));
  } else if (calibration_result.has_stationary_camera_result_poses()) {
    Pose3d flange_t_object = *FromProto(
        calibration_result.stationary_camera_result_poses().flange_t_object());
    INTR_RETURN_IF_ERROR(world.UpdateTransform(
        world_update_params.flange,
        world_update_params.calibration_object.value(),
        /*node_to_update=*/world_update_params.calibration_object.value(),
        flange_t_object));
  } else {
    return absl::InvalidArgumentError(
        "Camera-to-robot calibration result poses are missing.");
  }
  return absl::OkStatus();
}

std::vector<std::vector<Vector2f>> ImagePointsPerCamera(
    const std::vector<intrinsic_proto::perception::v1::PatternDetection>&
        pattern_detections) {
  return Transformed(
      pattern_detections,
      [](const intrinsic_proto::perception::v1::PatternDetection&
             pattern_detection) {
        return Transformed(
            pattern_detection.image_points(),
            [](const intrinsic_proto::perception::Vector2f& image_point) {
              return FromProto(image_point);
            });
      });
}

template <typename ImageTraits>
absl::StatusOr<double> SharpnessAtPatternDetection(
    const Image<ImageTraits>& image,
    const intrinsic_proto::perception::v1::PatternDetection&
        pattern_detection) {
  const std::vector<Vector2f> image_points =
      Transformed(pattern_detection.image_points(),
                  [](const intrinsic_proto::perception::Vector2f& image_point) {
                    return FromProto(image_point);
                  });
  INTR_ASSIGN_OR_RETURN(const Rectangle roi, ComputeRoi(image_points));
  INTR_ASSIGN_OR_RETURN(const Image<ImageTraits> extracted_roi,
                        ExtractRoi(image, roi));
  cv::Mat gray_mat;
  if constexpr (ImageTraits::kNumChannels == 3) {
    cv::cvtColor(UnsafeConstCastCvMat(extracted_roi), gray_mat,
                 cv::COLOR_RGB2GRAY);
  } else {
    gray_mat = UnsafeConstCastCvMat(extracted_roi).clone();
  }

  cv::Mat normalized_mat;
  cv::normalize(gray_mat, normalized_mat, 0, 255, cv::NORM_MINMAX, CV_8U);

  Image<Gray8u> normalized_roi = MoveToImage<Gray8u>(std::move(normalized_mat));
  return ComputeSharpnessScore(extracted_roi, kBlurDetectionFilterSize);
}

absl::StatusOr<double> SensorImageSharpness(
    const SensorImage* absl_nonnull sensor_image,
    const intrinsic_proto::perception::v1::PatternDetection&
        pattern_detection) {
  if (sensor_image->rgb8u().has_value()) {
    return SharpnessAtPatternDetection(sensor_image->rgb8u().value(),
                                       pattern_detection);
  } else if (sensor_image->gray8u().has_value()) {
    return SharpnessAtPatternDetection(sensor_image->gray8u().value(),
                                       pattern_detection);
  } else if (sensor_image->gray32f().has_value()) {
    return SharpnessAtPatternDetection(sensor_image->gray32f().value(),
                                       pattern_detection);
  } else {
    return absl::InvalidArgumentError("Unsupported image type.");
  }
}

// Returns the blob name prefix under which the raw image of a capture is
// logged. The pattern detector logs the matching annotated image under
// `<config_prefix>annotated/`, so raw images end up in a sibling `raw/` folder.
std::string RawImageLogPrefix(absl::string_view config_prefix) {
  return absl::StrCat(config_prefix, "raw/");
}

// Returns the logging context to attach to a logged calibration image.
// The calibration-specific labels are added so that logged frames can be
// filtered by calibration session, capture, and camera.
intrinsic_proto::data_logger::Context ImageLogContext(
    absl::string_view session_id, absl::string_view capture_id,
    absl::string_view camera_name) {
  intrinsic_proto::data_logger::Context context;
  (*context.mutable_labels())["calibration_session_id"] = session_id;
  (*context.mutable_labels())["calibration_capture_id"] = capture_id;
  (*context.mutable_labels())["camera"] = camera_name;
  return context;
}
}  // namespace

CalibrationServiceImpl::CalibrationServiceImpl(
    std::unique_ptr<::google::longrunning::Operations::StubInterface>
        operations_stub,
    std::unique_ptr<intrinsic_proto::assets::v1::AssetInstances::StubInterface>
        asset_instances_stub,
    std::unique_ptr<
        intrinsic_proto::assets::AssetDeploymentService::StubInterface>
        asset_deployment_service_stub,
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service_stub,
    std::unique_ptr<KeyValueStore> kvstore)
    : kvstore_(std::move(kvstore)),
      operations_stub_(std::move(operations_stub)),
      asset_instances_stub_(std::move(asset_instances_stub)),
      asset_deployment_service_stub_(std::move(asset_deployment_service_stub)),
      object_world_service_stub_(std::move(object_world_service_stub)) {}

grpc::Status CalibrationServiceImpl::Initialize(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::InitializeRequest* request,
    intrinsic_proto::perception::v1::InitializeResponse* response) {
  if (!request->has_pattern_detection_config()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "No `pattern_detection_config` was provided.");
  }
  if (request->camera_resource_handles().empty()) {
    return grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT,
        "At least one `camera_resource_handle` needs to be provided.");
  }
  response->Clear();
  absl::MutexLock lock(mutex_);

  execute_world_id_ = request->belief_world_id().empty()
                          ? std::string(kDefaultBeliefWorldId)
                          : request->belief_world_id();
  initial_world_id_ = request->edit_world_id().empty()
                          ? std::string(kDefaultInitialWorldId)
                          : request->edit_world_id();

  if (kvstore_ == nullptr) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        KeyValueStore kvstore,
        pubsub_.KeyValueStore(std::string(kDefaultKeyPrefix)));
    kvstore_ = std::make_unique<KeyValueStore>(kvstore);
  }
  absl::Notification notification;
  INTR_ASSIGN_OR_RETURN_GRPC(
      absl::StatusOr<intrinsic::KVQuery> query,
      kvstore_->GetAll(
          absl::StrCat(kCalibrationKVStoragePrefix, "**"),
          [this](std::string_view key, std::unique_ptr<google::protobuf::Any>) {
            mutex_.AssertHeld();
            kvstore_->Delete(key).IgnoreError();
          },
          [&notification](std::string_view) { notification.Notify(); }));
  const absl::Cleanup on_return = [&notification] {
    notification.WaitForNotification();
  };

  session_id_ =
      absl::FormatTime("%Y%m%d_%H%M%S", absl::Now(), absl::UTCTimeZone());
  calibration_data_.clear();
  cached_intrinsic_calibration_results_.clear();
  next_capture_id_ = 0;
  camera_info_.clear();
  camera_info_.reserve(request->camera_resource_handles_size());

  pattern_detection_config_ = request->pattern_detection_config();
  INTR_ASSIGN_OR_RETURN_GRPC(
      pattern_detector_, PatternDetector::Create(pattern_detection_config_));

  for (const auto& camera_resource_handle :
       request->camera_resource_handles()) {
    LOG(INFO) << "Creating camera " << camera_resource_handle.name();
    INTR_ASSIGN_OR_RETURN_GRPC(
        ConnectionParams connection_params,
        skills::GetConnectionParamsFromHandle(camera_resource_handle));

    INTR_ASSIGN_OR_RETURN_GRPC(
        const intrinsic_proto::perception::v1::CameraConfig camera_config,
        GetCameraConfigFromHandle(camera_resource_handle));
    LOG(INFO) << "Created camera: " << camera_resource_handle.name();

    intrinsic_proto::perception::v1::DescribeCameraRequest describe_request;
    *describe_request.mutable_camera_identifier() = camera_config.identifier();
    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::perception::v1::DescribeCameraResponse
            describe_response,
        grpc_camera_.Call(&GrpcCamera::Stub::DescribeCamera, describe_request,
                          connection_params));

    absl::flat_hash_map<int64_t, PostProcessing> post_processing_by_sensor_id;
    post_processing_by_sensor_id.reserve(describe_response.sensors().size());
    // Find first camera sensor with pixel type Intensity (grayscale or rgb).
    // This is considered the default (reference) camera sensor.
    const auto it = absl::c_find_if(
        describe_response.sensors(),
        [](const intrinsic_proto::perception::v1::SensorInformation& sensor) {
          return absl::c_linear_search(
              sensor.supported_pixel_types(),
              intrinsic_proto::perception::v1::PixelType::PIXEL_INTENSITY);
        });
    if (it == describe_response.sensors().end()) {
      return grpc::Status(
          grpc::StatusCode::FAILED_PRECONDITION,
          absl::StrCat("No sensor with PIXEL_INTENSITY found for camera: ",
                       camera_resource_handle.name()));
    }
    const Dimensions dimensions = FromProto(it->dimensions());
    for (const intrinsic_proto::perception::v1::SensorInformation& sensor_info :
         describe_response.sensors()) {
      post_processing_by_sensor_id.emplace(
          sensor_info.id(), PostProcessing{.crop_region = std::nullopt,
                                           .cols = std::nullopt,
                                           .rows = std::nullopt,
                                           .skip_undistortion = true});
    }
    camera_info_.push_back({
        .camera_resource_handle = camera_resource_handle,
        .is_multi_sensor = describe_response.sensors().size() > 1,
        .connection_params = std::move(connection_params),
        .image_dimensions = dimensions,
        .post_processing_by_sensor_id = std::move(post_processing_by_sensor_id),
        .default_sensor_id = it->id(),
    });

    robot_object_ = std::nullopt;
    robot_flange_ = std::nullopt;
    calibration_object_ = std::nullopt;

    if (request->has_arm_part() || request->has_calibration_object()) {
      auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
      world::ObjectWorldClient object_world_client(execute_world_id_,
                                                   object_world_service_stub);
      if (request->has_arm_part()) {
        auto robot =
            object_world_client.GetKinematicObject(request->arm_part());
        if (robot.ok()) {
          robot_object_ = *robot;
          INTR_ASSIGN_OR_RETURN_GRPC(robot_flange_,
                                     robot->GetSingleIsoFlangeFrame());
        } else {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string(robot.status().message()));
        }
      }
      if (request->has_calibration_object()) {
        auto object =
            object_world_client.GetObject(request->calibration_object());
        if (object.ok()) {
          calibration_object_ = *object;
        } else {
          return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string(object.status().message()));
        }
      }
    }
  }

  // Finish all scheduled coverage map processing and logging tasks, before we
  // reinitialize the coverage maps and their publishers, so we won't work on
  // different ones.
  INTR_RETURN_IF_ERROR_GRPC(executor_.WaitForOutstandingTasks());
  absl::MutexLock coverage_lock(coverage_mutex_);
  coverage_maps_.clear();
  coverage_maps_.reserve(camera_info_.size());
  coverage_map_publishers_.clear();
  coverage_map_publishers_.reserve(camera_info_.size());
  for (int i = 0; i < camera_info_.size(); ++i) {
    coverage_maps_.push_back(
        Image<Gray8u>(camera_info_[i].image_dimensions, Gray8u::PixelType(0)));
    std::string topic = absl::StrCat(kCalibrationKVStoragePrefix, session_id_,
                                     "/cameras/", i, "/coverage_map");
    INTR_ASSIGN_OR_RETURN_GRPC(Publisher publisher,
                               pubsub_.CreatePublisher(topic, TopicConfig()));
    coverage_map_publishers_.push_back(std::move(publisher));
    *response->add_coverage_map_topic_names() = std::move(topic);
  }
  INTR_RETURN_IF_ERROR_GRPC(ScheduleCoverageMapsUpdate(
      std::vector<std::vector<Vector2f>>(camera_info_.size()),
      [](const std::vector<Vector2f>&, const Image<Gray8u>& image) {
        return image;
      }));

  sharpness_publishers_.clear();
  sharpness_publishers_.reserve(camera_info_.size());
  for (int i = 0; i < camera_info_.size(); ++i) {
    std::string topic = absl::StrCat(kCalibrationKVStoragePrefix, session_id_,
                                     "/cameras/", i, "/sharpness");
    INTR_ASSIGN_OR_RETURN_GRPC(Publisher publisher,
                               pubsub_.CreatePublisher(topic, TopicConfig()));
    sharpness_publishers_.push_back(std::move(publisher));
  }

  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::CaptureData(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::CaptureDataRequest* request,
    intrinsic_proto::perception::v1::CaptureDataResponse* response) {
  response->Clear();
  absl::MutexLock lock(mutex_);

  std::vector<std::optional<double>> sharpness_scores(camera_info_.size(),
                                                      std::nullopt);

  if (pattern_detector_ == std::nullopt) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Pattern detector is not initialized.");
  }
  if (camera_info_.empty()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "No cameras initialized.");
  }
  if (calibration_data_.size() >= kMaxCalibrationDataPoints) {
    return grpc::Status(
        grpc::StatusCode::FAILED_PRECONDITION,
        absl::StrCat("No more than ", kMaxCalibrationDataPoints,
                     " capture data points can be stored. You can use "
                     "`DeleteCapture` to remove data points."));
  }

  // Compare cached base_t_flange to the one from the CaptureData request.
  // If they are not identical, return an error. If only one of the
  // two is set, use that one.
  std::optional<intrinsic_proto::Pose> base_t_flange;
  std::optional<Pose3d> base_t_flange_cached;
  if (robot_object_.has_value() && robot_flange_.has_value()) {
    auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
    world::ObjectWorldClient object_world_client(execute_world_id_,
                                                 object_world_service_stub);

    INTR_ASSIGN_OR_RETURN_GRPC(
        base_t_flange_cached,
        object_world_client.GetTransform(*robot_object_, *robot_flange_));
  }
  std::optional<Pose3d> base_t_flange_from_request;
  if (request->has_base_t_flange()) {
    auto parsed = FromProto(request->base_t_flange());
    if (parsed.ok()) {
      base_t_flange_from_request = *parsed;
    }
  }
  if (base_t_flange_cached.has_value() &&
      base_t_flange_from_request.has_value()) {
    if (!base_t_flange_cached->isApprox(*base_t_flange_from_request, 1e-4)) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "base_t_flange as extracted during calibration "
                          "service initialization and base_t_flange from "
                          "CaptureDataRequest are not identical.");
    }
    base_t_flange = request->base_t_flange();
  } else if (base_t_flange_cached.has_value()) {
    base_t_flange = ToProto(*base_t_flange_cached);
  } else if (base_t_flange_from_request.has_value()) {
    base_t_flange = request->base_t_flange();
  }

  CalibrationDataPoint calibration_data_point;
  if (base_t_flange.has_value()) {
    calibration_data_point.base_t_flange = base_t_flange;
  }

  calibration_data_point.capture_id = std::format("{:04}", next_capture_id_++);

  std::vector<intrinsic_proto::perception::v1::ImageBuffer>
      annotated_image_buffers;
  annotated_image_buffers.reserve(camera_info_.size());
  // Consider parallelizing this loop.
  for (int i = 0; i < camera_info_.size(); ++i) {
    // We push back an empty pattern detection in case we don't detect a
    // pattern in the image. This is to ensure that the number of pattern
    // detections is the same as the number of cameras.
    calibration_data_point.pattern_detections.push_back(
        intrinsic_proto::perception::v1::PatternDetection());
    const std::string& camera_name =
        camera_info_[i].camera_resource_handle.name();
    const absl::flat_hash_map<int64_t, PostProcessing>&
        post_processing_by_sensor_id =
            camera_info_[i].post_processing_by_sensor_id;

    LOG(INFO) << "Acquiring image from camera " << camera_name;
    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::perception::v1::CameraConfig camera_config,
        GetCameraConfigFromAssetInstances(
            camera_info_[i].camera_resource_handle.name()));

    intrinsic_proto::perception::v1::CaptureRequest capture_request;
    *capture_request.mutable_camera_config() = camera_config;
    INTR_ASSIGN_OR_RETURN_GRPC(*capture_request.mutable_timeout(),
                               FromAbslDuration(kDefaultImageGrabbingTimeout));
    *capture_request.mutable_post_processing_by_sensor_id() =
        ToProto(post_processing_by_sensor_id, {});
    INTR_ASSIGN_OR_RETURN_GRPC(
        const intrinsic_proto::perception::v1::CaptureResponse capture_response,
        grpc_camera_.Call(&GrpcCamera::Stub::Capture, capture_request,
                          camera_info_[i].connection_params));

    INTR_ASSIGN_OR_RETURN_GRPC(const CaptureResult capture_result,
                               FromProto(capture_response.capture_result()));
    if (capture_result.sensor_images.empty()) {
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "No sensor images returned from Capture.");
    }
    INTR_ASSIGN_OR_RETURN_GRPC(
        const SensorImage* absl_nonnull sensor_image,
        (GetFirstSensorImageOfType<Rgb8u, Gray8u, Gray32f>(capture_result)));
    calibration_data_point.camera_params_from_capture.push_back(
        sensor_image->camera_params());

    // Log the raw (unannotated) image. The corresponding annotated image is
    // logged by the pattern detector. Logging failures are not fatal for the
    // data collection.
    if (const absl::StatusOr<std::string> filename = LogSensorImage(
            executor_, *sensor_image,
            RawImageLogPrefix(
                ExtractPrefixFromConfigName(pattern_detection_config_.name())),
            ImageLogContext(session_id_, calibration_data_point.capture_id,
                            camera_name));
        filename.ok()) {
      LOG(INFO) << "Logging raw image of camera " << camera_name << " as "
                << *filename;
    } else {
      LOG(WARNING) << "Failed to log raw image of camera " << camera_name
                   << ": " << filename.status();
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::perception::v1::PatternDetectionResult
            pattern_detection_result,
        pattern_detector_->Run(capture_result, context));

    auto Annotate = [&](auto image) -> absl::Status {
      mutex_.AssertHeld();
      for (const auto& detection :
           pattern_detection_result.pattern_detections()) {
        std::vector<Vector2f> points;
        points.reserve(detection.image_points_size());
        for (const auto& p : detection.image_points()) {
          points.emplace_back(p.x(), p.y());
        }
        image = DrawCrosshairs(image, points);
      }
      intrinsic_proto::perception::v1::ImageBuffer annotated_image_buffer;
      INTR_ASSIGN_OR_RETURN(annotated_image_buffer,
                            ToProto(image, Encoding::kJpeg));
      annotated_image_buffers.push_back(std::move(annotated_image_buffer));
      return absl::OkStatus();
    };

    if (sensor_image->rgb8u().has_value()) {
      INTR_RETURN_IF_ERROR_GRPC(Annotate(sensor_image->rgb8u().value()));
    } else if (sensor_image->gray8u().has_value()) {
      INTR_RETURN_IF_ERROR_GRPC(Annotate(sensor_image->gray8u().value()));
    } else if (sensor_image->gray32f().has_value()) {
      INTR_RETURN_IF_ERROR_GRPC(Annotate(sensor_image->gray32f().value()));
    } else {
      return grpc::Status(
          grpc::StatusCode::FAILED_PRECONDITION,
          "Unsupported image type for annotation (only RGB/Gray supported).");
    }

    if (pattern_detection_result.pattern_detections().empty()) {
      LOG(WARNING) << "Detected no calibration board from camera "
                   << camera_name;
      response->add_capture_data_status(
          intrinsic_proto::perception::v1::CaptureDataResponse::
              CAPTURE_DATA_STATUS_NO_DETECTIONS);
      continue;
    }
    if (pattern_detection_result.pattern_detections_size() > 1) {
      LOG(WARNING) << "Detected multiple calibration boards from camera "
                   << camera_name;
      response->add_capture_data_status(
          intrinsic_proto::perception::v1::CaptureDataResponse::
              CAPTURE_DATA_STATUS_MULTIPLE_DETECTIONS);
      continue;
    }
    const intrinsic_proto::perception::v1::PatternDetection& pattern_detection =
        pattern_detection_result.pattern_detections(0);
    if (pattern_detection.image_points_size() < kMinNumberPatternImagePoints ||
        IsLowRankPatternDetection(pattern_detection)) {
      LOG(WARNING) << "Insufficient marker points from camera " << camera_name;
      response->add_capture_data_status(
          intrinsic_proto::perception::v1::CaptureDataResponse::
              CAPTURE_DATA_STATUS_INSUFFICIENT_MARKER_POINTS);
      continue;
    }

    // Compute sharpness score.
    LOG(INFO) << "Computing sharpness score for camera " << camera_name;
    auto sharpness = SensorImageSharpness(sensor_image, pattern_detection);
    if (sharpness.ok()) {
      sharpness_scores[i] = sharpness.value();
      if (request->detect_blurred_image()) {
        INTR_RETURN_IF_ERROR_GRPC(sharpness.status());
        const bool is_blurry =
            IsBlurry(sharpness.value(), kBlurDetectionFilterThreshold);
        if (is_blurry) {
          LOG(WARNING) << "Blurred image from camera " << camera_name;
          response->add_capture_data_status(
              intrinsic_proto::perception::v1::CaptureDataResponse::
                  CAPTURE_DATA_STATUS_BLURRED_IMAGE);
          continue;
        }
      }
    } else {
      LOG(WARNING) << "Failed to compute sharpness score: "
                   << sharpness.status().message();
    }
    if (request->detect_redundant_pattern()) {
      const int pixel_wise_difference_threshold =
          kHorizontalPixelWiseDifferenceFactorForRedundantDetections *
          sensor_image->Dimensions().cols;
      if (absl::c_any_of(calibration_data_, [&](const CalibrationDataPoint& d) {
            return IsRedundantPatternDetection(pattern_detection,
                                               d.pattern_detections[i],
                                               pixel_wise_difference_threshold);
          })) {
        LOG(WARNING) << "Redundant pattern detection from camera "
                     << camera_name;
        response->add_capture_data_status(
            intrinsic_proto::perception::v1::CaptureDataResponse::
                CAPTURE_DATA_STATUS_REDUNDANT_PATTERN_DETECTION);
        continue;
      };
    }

    calibration_data_point.pattern_detections[i] = pattern_detection;
    response->add_capture_data_status(
        intrinsic_proto::perception::v1::CaptureDataResponse::
            CAPTURE_DATA_STATUS_NO_ERRORS);
  }

  INTR_RETURN_IF_ERROR_GRPC(ScheduleCoverageMapsUpdate(
      ImagePointsPerCamera(calibration_data_point.pattern_detections),
      AddPolygonToCoverageMap));

  if (kvstore_ == nullptr) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "KVStore is not initialized.");
  }
  for (int i = 0; i < annotated_image_buffers.size(); ++i) {
    const intrinsic_proto::perception::v1::ImageBuffer& annotated_image_buffer =
        annotated_image_buffers[i];
    const std::string key = absl::StrCat(
        kCalibrationKVStoragePrefix, session_id_, "/captures/",
        calibration_data_point.capture_id, "/cameras/", i, "/annotated_image");
    INTR_RETURN_IF_ERROR_GRPC(
        kvstore_->Set(key, annotated_image_buffer, /*high_consistency=*/true));
    LOG(INFO) << "Stored annotated image in KV Store " << kDefaultKeyPrefix
              << " with key " << key;
    intrinsic_proto::kvstore::StorageLocation* annotated_image_location =
        response->add_annotated_image_locations();
    annotated_image_location->set_store(kDefaultKeyPrefix);
    annotated_image_location->set_key(key);

    if (sharpness_scores[i].has_value()) {
      google::protobuf::DoubleValue sharpness_val;
      sharpness_val.set_value(*sharpness_scores[i]);
      // if (i < sharpness_publishers_.size()) {
      //   INTR_RETURN_IF_ERROR_GRPC(
      //       sharpness_publishers_[i].Publish(sharpness_val));
      // }
      const std::string sharpness_key = absl::StrCat(
          kCalibrationKVStoragePrefix, session_id_, "/captures/",
          calibration_data_point.capture_id, "/cameras/", i, "/sharpness");
      INTR_RETURN_IF_ERROR_GRPC(kvstore_->Set(sharpness_key, sharpness_val,
                                              /*high_consistency=*/true));
      LOG(INFO) << "Stored sharpness score in KV Store " << kDefaultKeyPrefix
                << " with key " << sharpness_key;
    }
  }

  response->set_capture_id(calibration_data_point.capture_id);
  calibration_data_.push_back(std::move(calibration_data_point));
  LOG(INFO) << "Added calibration data point.";
  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::DeleteCapture(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::DeleteCaptureRequest* request,
    google::protobuf::Empty* response) {
  absl::MutexLock lock(mutex_);
  auto it = absl::c_find_if(
      calibration_data_, [&](const CalibrationDataPoint& data_point) {
        return data_point.capture_id == request->capture_id();
      });
  if (it == calibration_data_.end()) {
    return grpc::Status(
        grpc::StatusCode::NOT_FOUND,
        absl::StrCat("Capture ID ", request->capture_id(), " not found."));
  }

  INTR_RETURN_IF_ERROR_GRPC(
      ScheduleCoverageMapsUpdate(ImagePointsPerCamera(it->pattern_detections),
                                 RemovePolygonFromCoverageMap));

  calibration_data_.erase(it);
  LOG(INFO) << "Removed capture " << request->capture_id();
  return grpc::Status::OK;
}

absl::Status CalibrationServiceImpl::CalibrateIntrinsics(
    const intrinsic_proto::perception::v1::CalibrationRequest* request,
    intrinsic_proto::perception::v1::CalibrationResult* response,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  for (int i = 0; i < camera_info_.size(); ++i) {
    // Check if a camera is multi-sensor. In this case, we would need to
    // calibrate each sensor separately, which is not supported yet and also
    // not needed since the supported multi-sensor cameras are factory
    // calibrated. Until a proper solution is implemented, we throw an error
    // when a user tries to calibrate a multi-sensor camera.
    if (camera_info_[i].is_multi_sensor) {
      return absl::FailedPreconditionError(
          "Intrinsic calibration of multi-sensor cameras is "
          "not supported.");
    }
  }

  // Loop over each camera to calibrate them independently
  // (pattern_detections follow the same camera order as camera_info_).
  std::vector<CameraParams> calibration_results;
  calibration_results.reserve(camera_info_.size());
  for (int i = 0; i < camera_info_.size(); ++i) {
    std::vector<intrinsic_proto::perception::v1::PatternDetection>
        pattern_detections;
    pattern_detections.reserve(calibration_data.size());

    for (const auto& data : calibration_data) {
      if (data.pattern_detections.size() != camera_info_.size()) {
        return absl::FailedPreconditionError(
            "Each calibration data point must have one pattern "
            "detection per camera.");
      }

      const auto& detection = data.pattern_detections[i];
      if (!detection.image_points().empty()) {
        pattern_detections.push_back(detection);
      }
    }

    LOG(INFO) << "Calibrating intrinsics for camera "
              << camera_info_[i].camera_resource_handle.name();
    INTR_ASSIGN_OR_RETURN(
        const auto intrinsic_calibration_result,
        ::intrinsic::perception::CalibrateIntrinsics(
            camera_info_[i].image_dimensions, pattern_detections));

    // Update the camera parameters using the calibration result and use them
    // to compute the 3D error.
    const CameraParams camera_params =
        intrinsic_calibration_result.camera_params;
    calibration_results.push_back(camera_params);
    const intrinsic_proto::perception::v1::CameraParams camera_params_v1 =
        intrinsic_proto::perception::v1::ToProto(camera_params);
    intrinsic_proto::perception::v1::IntrinsicCalibrationResult result;
    *result.mutable_intrinsic_params() = camera_params_v1.intrinsic_params();
    if (camera_params_v1.has_distortion_params()) {
      *result.mutable_distortion_params() =
          camera_params_v1.distortion_params();
    }
    result.set_error(intrinsic_calibration_result.error);

    const auto error_3d = GetIntrinsics3DErrorFromPatternDetections(
        pattern_detections, camera_params);
    if (error_3d.ok()) {
      result.set_error3d(error_3d->mean_3d_error);
    } else {
      LOG(WARNING) << "Failed to compute 3D error: " << error_3d.status();
    }

    if (pattern_detections.size() !=
        intrinsic_calibration_result.errors_per_image.size()) {
      return absl::InternalError(
          absl::StrCat("Mismatch in errors_per_image count for camera ", i,
                       ". Expected ", pattern_detections.size(), ", got ",
                       intrinsic_calibration_result.errors_per_image.size()));
    }
    auto* reprojection_errors_map =
        result.mutable_reprojection_error_by_capture_id();
    auto error_it = intrinsic_calibration_result.errors_per_image.begin();

    for (const auto& calib_item : calibration_data) {
      if (!calib_item.pattern_detections[i].image_points().empty()) {
        (*reprojection_errors_map)[calib_item.capture_id] = *error_it++;
      }
    }

    LOG(INFO) << "Intrinsic calibration result for camera "
              << camera_info_[i].camera_resource_handle.name() << ": "
              << result.DebugString();
    *response->add_intrinsic_calibration_results() = result;
  }
  cached_intrinsic_calibration_results_.clear();
  cached_intrinsic_calibration_results_ = std::move(calibration_results);
  return absl::OkStatus();
}

absl::StatusOr<std::vector<CameraParams>>
CalibrationServiceImpl::GetCameraParamsFromCachedResultsOrData(
    const CalibrationDataPoint& data_point) {
  if (!cached_intrinsic_calibration_results_.empty()) {
    if (cached_intrinsic_calibration_results_.size() != camera_info_.size()) {
      return absl::InvalidArgumentError(
          "Number of intrinsic calibration results does not match number of "
          "cameras.");
    }
    LOG(INFO) << "Using cached intrinsic calibration results.";
    return cached_intrinsic_calibration_results_;
  }

  if (data_point.camera_params_from_capture.size() != camera_info_.size()) {
    return absl::InvalidArgumentError(
        "Number of camera params in capture does not match number of "
        "cameras.");
  }
  std::vector<CameraParams> camera_params;
  for (int i = 0; i < camera_info_.size(); ++i) {
    if (!data_point.camera_params_from_capture[i].has_value()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "No camera parameters found for camera ", i,
          ". Please run intrinsic calibration first or ensure the camera "
          "config provides intrinsic parameters."));
    }
    camera_params.push_back(data_point.camera_params_from_capture[i].value());
  }
  LOG(INFO) << "Using intrinsic calibration params from captured data.";
  return camera_params;
}

absl::StatusOr<std::vector<
    intrinsic_proto::perception::v1::CameraToRobotCalibrationRequest>>
CalibrationServiceImpl::CreateCameraToRobotCalibrationRequests(
    intrinsic_proto::perception::v1::CameraSetup type,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  std::vector<Pose3d> camera_t_sensors;
  camera_t_sensors.reserve(camera_info_.size());

  if (!robot_object_.has_value() || !robot_flange_.has_value()) {
    return absl::FailedPreconditionError(
        "No arm part was specified on calibration service initialization. "
        "Camera pose update is not possible without a reference robot arm "
        "part.");
  }

  std::vector<intrinsic_proto::perception::v1::CameraToRobotCalibrationRequest>
      requests;
  requests.reserve(camera_info_.size());

  INTR_ASSIGN_OR_RETURN(
      const auto camera_params,
      GetCameraParamsFromCachedResultsOrData(calibration_data.back()));

  for (int i = 0; i < camera_info_.size(); ++i) {
    intrinsic_proto::perception::v1::CameraToRobotCalibrationRequest
        camera_to_robot_calibration_request;
    camera_to_robot_calibration_request.set_type(type);
    requests.push_back(camera_to_robot_calibration_request);

    // Extract possible sensor pose offset from default sensor config.
    const int64_t default_sensor_id = camera_info_[i].default_sensor_id;
    intrinsic_proto::perception::v1::SensorConfig default_sensor_config;
    default_sensor_config.set_id(default_sensor_id);

    if (i < camera_params.size()) {
      *default_sensor_config.mutable_camera_params() =
          intrinsic_proto::perception::v1::ToProto(camera_params[i]);
    }

    if (!default_sensor_config.has_camera_params()) {
      return absl::FailedPreconditionError(
          absl::StrCat("Default sensor config for ",
                       camera_info_[i].camera_resource_handle.name(),
                       " does not have camera params."));
    }

    if (default_sensor_config.has_camera_t_sensor()) {
      INTR_ASSIGN_OR_RETURN(const Pose3d camera_t_sensor,
                            FromProto(default_sensor_config.camera_t_sensor()));
      camera_t_sensors.push_back(camera_t_sensor);
    } else {
      camera_t_sensors.push_back(Pose3d());
    }
  }

  for (const auto& data : calibration_data) {
    if (data.pattern_detections.size() != camera_info_.size()) {
      return absl::FailedPreconditionError(
          "Each calibration data point must have one pattern "
          "detection per camera.");
    }
    if (!data.base_t_flange.has_value()) {
      // This code should never be reached because if we are parsing the
      // calibration data, robot_object_ and robot_flange_ are guaranteed to be
      // set, hence data collection was performed with a defined base_t_flange
      // (even if the CaptureDataRequest did not have a base_t_flange).
      return absl::FailedPreconditionError(
          "No base_t_flange was provided for camera-to-robot "
          "calibration.");
    }
    // Loop over each camera and add to the respective calibration request
    // (pattern_detections are ordered by camera).
    for (int i = 0; i < data.pattern_detections.size(); ++i) {
      if (!data.pattern_detections[i].image_points().empty()) {
        INTR_ASSIGN_OR_RETURN(
            const auto pose, ComputePoseFromPatternDetection(
                                 data.pattern_detections[i], camera_params[i]));
        INTR_ASSIGN_OR_RETURN(
            const auto charuco_board,
            CreateCharucoBoard(
                pattern_detection_config_.charuco_pattern_detection_config()
                    .charuco_pattern()));
        const auto size = charuco_board->getChessboardSize();
        const eigenmath::Vector3d move =
            eigenmath::Vector3d(size.width, size.height, 0) / 2.0 *
            charuco_board->getSquareLength();
        const eigenmath::AngleAxisd rotate_x(M_PI,
                                             eigenmath::Vector3d(1, 0, 0));
        const Pose3d corner_t_center(eigenmath::Quaterniond(rotate_x), move);
        const Pose3d sensor_t_object = pose * corner_t_center;
        const Pose3d camera_t_object = camera_t_sensors[i] * sensor_t_object;
        auto* pose_pair = requests[i].add_input_pose_pairs();
        *pose_pair->mutable_camera_t_object() = ToProto(camera_t_object);
        *pose_pair->mutable_base_t_flange() = *data.base_t_flange;
      }
    }
  }

  return requests;
}

absl::Status CalibrationServiceImpl::CalibrateCameraToRobot(
    const intrinsic_proto::perception::v1::CalibrationRequest* request,
    intrinsic_proto::perception::v1::CalibrationResult* response,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  INTR_ASSIGN_OR_RETURN(
      auto requests,
      CreateCameraToRobotCalibrationRequests(
          request->camera_to_robot_calibration_type(), calibration_data));

  // Run calibration and update camera poses in the Execute world.
  auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
  world::ObjectWorldClient object_world_client(execute_world_id_,
                                               object_world_service_stub);
  for (int i = 0; i < requests.size(); ++i) {
    LOG(INFO) << "Calibrating camera to robot pose for camera "
              << camera_info_[i].camera_resource_handle.name();
    INTR_ASSIGN_OR_RETURN(
        const auto camera_to_robot_calibration_result,
        ::intrinsic::perception::CalibrateCameraToRobot(requests[i]));
    *response->mutable_camera_to_robot_calibration_results()->Add() =
        camera_to_robot_calibration_result;

    INTR_ASSIGN_OR_RETURN(
        const world::WorldObject camera,
        object_world_client.GetObject(camera_info_[i].camera_resource_handle));
    INTR_ASSIGN_OR_RETURN(const world::Frame camera_frame,
                          camera.GetFrame(SensorFrameName()));

    struct WorldUpdateParameters world_update_params{
        .camera = camera,
        .camera_frame = camera_frame,
        .robot = robot_object_.value(),
        .flange = robot_flange_.value(),
        .calibration_object = calibration_object_,
    };

    INTR_RETURN_IF_ERROR(
        UpdateCameraPoseInWorld(camera_to_robot_calibration_result,
                                world_update_params, object_world_client));
    INTR_RETURN_IF_ERROR(UpdateCalibrationObjectInWorld(
        camera_to_robot_calibration_result, world_update_params,
        object_world_client));
  }
  return absl::OkStatus();
}

absl::Status CalibrationServiceImpl::CalibrateCameraToCamera(
    const intrinsic_proto::perception::v1::CalibrationRequest* request,
    intrinsic_proto::perception::v1::CalibrationResult* response,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  if (camera_info_.size() < 2) {
    return absl::FailedPreconditionError(
        "At least two cameras are required for camera-to-camera "
        "calibration.");
  }

  if (calibration_data.empty()) {
    return absl::InvalidArgumentError("No calibration data provided.");
  }
  INTR_ASSIGN_OR_RETURN(
      const auto camera_params,
      GetCameraParamsFromCachedResultsOrData(calibration_data[0]));

  // Pre-flight check: ensure every capture has pattern_detections.size() ==
  // camera_info_.size().
  for (int k = 0; k < calibration_data.size(); ++k) {
    if (calibration_data[k].pattern_detections.size() != camera_info_.size()) {
      return absl::FailedPreconditionError(
          "Each calibration data point must have one pattern "
          "detection per camera.");
    }
  }
  const CameraParams& reference_camera_params =
      camera_params[kDefaultReferenceCameraId];

  intrinsic_proto::perception::v1::MultiCameraCalibrationResult*
      multi_camera_calibration_result =
          response->mutable_camera_to_camera_calibration_result();

  std::vector<intrinsic_proto::perception::v1::PatternDetection>
      reference_pattern_detections;
  reference_pattern_detections.reserve(calibration_data.size());

  for (int k = 0; k < calibration_data.size(); ++k) {
    reference_pattern_detections.push_back(
        calibration_data[k].pattern_detections[kDefaultReferenceCameraId]);
  }

  auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
  world::ObjectWorldClient object_world_client(execute_world_id_,
                                               object_world_service_stub);

  INTR_ASSIGN_OR_RETURN(
      const world::WorldObject reference_camera,
      object_world_client.GetObject(
          camera_info_[kDefaultReferenceCameraId].camera_resource_handle));

  // Calibrates each camera pair (0th camera is always considered the reference
  // camera).
  for (int i = 0; i < camera_info_.size(); ++i) {
    if (i == kDefaultReferenceCameraId) {
      continue;
    }
    std::vector<intrinsic_proto::perception::v1::PatternDetection>
        camera_i_pattern_detections;
    camera_i_pattern_detections.reserve(calibration_data.size());

    for (int k = 0; k < calibration_data.size(); ++k) {
      camera_i_pattern_detections.push_back(
          calibration_data[k].pattern_detections[i]);
    }

    INTR_ASSIGN_OR_RETURN(
        const StereoCalibrationResult& camera_to_camera_calibration_result,
        ComputeStereoCalibrationResult(
            reference_pattern_detections, reference_camera_params,
            camera_i_pattern_detections, camera_params[i], i,
            kMinimumCameraDetectionsForCameraToCameraCalibration));

    intrinsic_proto::perception::v1::StereoCalibrationResult*
        stereo_calibration_result =
            multi_camera_calibration_result->add_stereo_calibration_results();
    *stereo_calibration_result->mutable_cam_t_cam0() =
        ToProto(camera_to_camera_calibration_result.cam1_t_cam0);
    stereo_calibration_result->set_error2d(
        camera_to_camera_calibration_result.reprojection_error);

    INTR_ASSIGN_OR_RETURN(
        const world::WorldObject camera,
        object_world_client.GetObject(camera_info_[i].camera_resource_handle));

    INTR_RETURN_IF_ERROR(UpdateCameraPoseInWorld(*stereo_calibration_result,
                                                 reference_camera, camera,
                                                 object_world_client));
  }
  return absl::OkStatus();
}

absl::Status CalibrationServiceImpl::CalibrateCaptureToCapture(
    const intrinsic_proto::perception::v1::CalibrationRequest* request,
    intrinsic_proto::perception::v1::CalibrationResult* response,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  if (camera_info_.size() == 1 && calibration_data.size() == 1) {
    return absl::FailedPreconditionError(
        "At least two captures are required for capture-to-capture "
        "calibration.");
  }

  if (calibration_data.empty()) {
    return absl::InvalidArgumentError("No calibration data provided.");
  }
  INTR_ASSIGN_OR_RETURN(const auto ref_camera_params,
                        GetCameraParamsFromCachedResultsOrData(
                            calibration_data[kDefaultReferenceCaptureId]));
  const CameraParams& reference_camera_params =
      ref_camera_params[kDefaultReferenceCameraId];

  std::vector<intrinsic_proto::perception::v1::PatternDetection>
      reference_pattern_detection{
          calibration_data[kDefaultReferenceCaptureId]
              .pattern_detections[kDefaultReferenceCameraId]};

  intrinsic_proto::perception::v1::CaptureToCaptureCalibrationResult*
      capture_to_capture_calibration_result =
          response->mutable_capture_to_capture_calibration_result();

  for (size_t camera_index = 0; camera_index < camera_info_.size();
       ++camera_index) {
    intrinsic_proto::perception::v1::
        SingleCameraCaptureToCaptureCalibrationResult*
            this_camera_capture_to_capture_calibration_result =
                capture_to_capture_calibration_result
                    ->add_camera_capture_to_capture_calibration_results();

    for (size_t capture_index = 0; capture_index < calibration_data.size();
         ++capture_index) {
      intrinsic_proto::perception::v1::SingleCaptureCalibrationResult*
          single_capture_calibration_result =
              this_camera_capture_to_capture_calibration_result
                  ->add_single_capture_calibration_results();

      if (calibration_data[capture_index].base_t_flange.has_value()) {
        *single_capture_calibration_result->mutable_robot_base_t_flange() =
            calibration_data[capture_index].base_t_flange.value();
      }

      if (camera_index == kDefaultReferenceCameraId &&
          capture_index == kDefaultReferenceCaptureId) {
        *single_capture_calibration_result->mutable_capture_t_capture0() =
            ToProto(Pose3d::Identity());
        single_capture_calibration_result->set_error2d(0);
        continue;
      }

      std::vector<intrinsic_proto::perception::v1::PatternDetection>
          camera_i_pattern_detection{
              calibration_data[capture_index].pattern_detections[camera_index]};

      INTR_ASSIGN_OR_RETURN(const auto capture_camera_params,
                            GetCameraParamsFromCachedResultsOrData(
                                calibration_data[capture_index]));
      const CameraParams& camera_params = capture_camera_params[camera_index];

      INTR_ASSIGN_OR_RETURN(
          const StereoCalibrationResult&
              capture_i_to_capture_0_calibration_result,
          ComputeStereoCalibrationResult(
              reference_pattern_detection, reference_camera_params,
              camera_i_pattern_detection, camera_params,
              camera_index * calibration_data.size() + capture_index,
              kMinimumCameraDetectionsForCaptureToCaptureCalibration));

      *single_capture_calibration_result->mutable_capture_t_capture0() =
          ToProto(capture_i_to_capture_0_calibration_result.cam1_t_cam0);
      single_capture_calibration_result->set_error2d(
          capture_i_to_capture_0_calibration_result.reprojection_error);
    }
  }

  return absl::OkStatus();
}
absl::Status CalibrationServiceImpl::ValidateIntrinsics(
    const intrinsic_proto::perception::v1::ValidateRequest* request,
    intrinsic_proto::perception::v1::ValidationResult* response,
    absl::Span<const CalibrationDataPoint> validation_data) {
  for (int i = 0; i < camera_info_.size(); ++i) {
    if (camera_info_[i].is_multi_sensor) {
      return absl::FailedPreconditionError(
          "Intrinsic validation of multi-sensor cameras is not supported.");
    }
  }

  if (validation_data.empty()) {
    return absl::FailedPreconditionError("Validation data is empty.");
  }

  for (const auto& data : validation_data) {
    if (data.pattern_detections.size() != camera_info_.size()) {
      return absl::FailedPreconditionError(
          "Each validation data point must have one pattern detection per "
          "camera.");
    }
  }

  INTR_ASSIGN_OR_RETURN(
      const std::vector<CameraParams> active_camera_params,
      GetCameraParamsFromCachedResultsOrData(validation_data[0]));

  for (int i = 0; i < camera_info_.size(); ++i) {
    std::vector<intrinsic_proto::perception::v1::PatternDetection>
        pattern_detections;
    pattern_detections.reserve(validation_data.size());

    for (const auto& data : validation_data) {
      const auto& detection = data.pattern_detections[i];
      if (!detection.image_points().empty()) {
        pattern_detections.push_back(detection);
      }
    }

    if (pattern_detections.empty()) {
      LOG(WARNING) << "No pattern detections found for camera "
                   << camera_info_[i].camera_resource_handle.name();
      continue;
    }

    const CameraParams& camera_params = active_camera_params[i];

    double total_reprojection_error = 0.0;
    int total_points = 0;
    double max_error2d = 0.0;

    for (const auto& detection : pattern_detections) {
      absl::StatusOr<Pose3d> camera_t_board =
          ComputePoseFromPatternDetection(detection, camera_params);
      if (!camera_t_board.ok()) {
        continue;
      }

      INTR_ASSIGN_OR_RETURN(ReprojectionErrorResult result,
                            ComputeReprojectionErrorForDetection(
                                detection, camera_params, *camera_t_board));

      total_reprojection_error += result.squared_error;
      total_points += result.num_points;
      max_error2d = std::max(max_error2d, result.max_error);
    }

    if (total_points == 0) {
      return absl::FailedPreconditionError(
          absl::StrCat("Could not compute 2D error for camera ",
                       camera_info_[i].camera_resource_handle.name(),
                       ": no valid pattern detections."));
    }

    double rmse = std::sqrt(total_reprojection_error / total_points);

    intrinsic_proto::perception::v1::IntrinsicValidationResult* result =
        response->add_intrinsic_validation_results();
    result->set_camera_name(camera_info_[i].camera_resource_handle.name());
    result->set_rms_error_2d(rmse);
    result->set_max_error_2d(max_error2d);

    const auto error_3d = GetIntrinsics3DErrorFromPatternDetections(
        pattern_detections, camera_params);
    if (error_3d.ok()) {
      result->set_mean_error_3d(error_3d->mean_3d_error);
      result->set_max_error_3d(error_3d->max_3d_error);
    } else {
      LOG(WARNING) << "Could not compute 3D error for camera "
                   << camera_info_[i].camera_resource_handle.name() << ": "
                   << error_3d.status().message();
    }
  }

  return absl::OkStatus();
}

absl::Status CalibrationServiceImpl::ValidateCameraToRobot(
    const intrinsic_proto::perception::v1::ValidateRequest* request,
    intrinsic_proto::perception::v1::ValidationResult* response,
    absl::Span<const CalibrationDataPoint> calibration_data) {
  INTR_ASSIGN_OR_RETURN(
      auto requests,
      CreateCameraToRobotCalibrationRequests(
          request->camera_to_robot_calibration_type(), calibration_data));

  auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
  world::ObjectWorldClient object_world_client(execute_world_id_,
                                               object_world_service_stub);

  for (int i = 0; i < requests.size(); ++i) {
    LOG(INFO) << "Validating camera to robot pose for camera "
              << camera_info_[i].camera_resource_handle.name();

    INTR_ASSIGN_OR_RETURN(
        const world::WorldObject camera,
        object_world_client.GetObject(camera_info_[i].camera_resource_handle));
    INTR_ASSIGN_OR_RETURN(const world::Frame camera_frame,
                          camera.GetFrame(SensorFrameName()));

    intrinsic_proto::Pose result_pose;
    if (requests[i].type() ==
        intrinsic_proto::perception::v1::CAMERA_SETUP_MOVING) {
      INTR_ASSIGN_OR_RETURN(const Pose3d flange_t_camera,
                            object_world_client.GetTransform(
                                robot_flange_.value(), camera_frame));
      result_pose = ToProto(flange_t_camera);
    } else {
      INTR_ASSIGN_OR_RETURN(const Pose3d base_t_camera,
                            object_world_client.GetTransform(
                                robot_object_.value(), camera_frame));
      result_pose = ToProto(base_t_camera);
    }

    INTR_ASSIGN_OR_RETURN(
        *response->mutable_camera_to_robot_validation_results()->Add(),
        ::intrinsic::perception::ComputeCameraToRobotValidationMetrics(
            requests[i], result_pose));
  }
  return absl::OkStatus();
}

absl::Status CalibrationServiceImpl::ValidateCameraToCamera(
    const intrinsic_proto::perception::v1::ValidateRequest* request,
    intrinsic_proto::perception::v1::ValidationResult* response,
    absl::Span<const CalibrationDataPoint> validation_data) {
  if (validation_data.empty()) {
    return absl::FailedPreconditionError("Validation data is empty.");
  }
  if (camera_info_.size() < 2) {
    return absl::FailedPreconditionError(
        "At least two cameras are required for camera-to-camera validation.");
  }

  for (const auto& data : validation_data) {
    if (data.pattern_detections.size() != camera_info_.size()) {
      return absl::FailedPreconditionError(
          "Each calibration data point must have one pattern detection per "
          "camera.");
    }
  }

  INTR_ASSIGN_OR_RETURN(
      const std::vector<CameraParams> active_camera_params,
      GetCameraParamsFromCachedResultsOrData(validation_data[0]));

  auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
  world::ObjectWorldClient object_world_client(execute_world_id_,
                                               object_world_service_stub);

  INTR_ASSIGN_OR_RETURN(
      const world::WorldObject reference_camera,
      object_world_client.GetObject(
          camera_info_[kDefaultReferenceCameraId].camera_resource_handle));
  INTR_ASSIGN_OR_RETURN(const world::Frame reference_camera_frame,
                        reference_camera.GetFrame(SensorFrameName()));

  intrinsic_proto::perception::v1::MultiCameraValidationResult*
      multi_camera_validation_result =
          response->mutable_camera_to_camera_validation_result();

  for (int i = 0; i < camera_info_.size(); ++i) {
    if (i == kDefaultReferenceCameraId) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        const world::WorldObject camera,
        object_world_client.GetObject(camera_info_[i].camera_resource_handle));
    INTR_ASSIGN_OR_RETURN(const world::Frame camera_frame,
                          camera.GetFrame(SensorFrameName()));

    INTR_ASSIGN_OR_RETURN(
        const Pose3d cam_i_t_cam0,
        object_world_client.GetTransform(camera_frame, reference_camera_frame));

    double total_reprojection_error = 0.0;
    int total_points = 0;
    double max_error2d = 0.0;

    for (const auto& data : validation_data) {
      const auto& detection0 =
          data.pattern_detections[kDefaultReferenceCameraId];
      const auto& detection_i = data.pattern_detections[i];

      if (detection0.image_points().empty() ||
          detection_i.image_points().empty()) {
        continue;
      }

      absl::StatusOr<Pose3d> cam0_t_board = ComputePoseFromPatternDetection(
          detection0, active_camera_params[kDefaultReferenceCameraId]);
      if (!cam0_t_board.ok()) {
        LOG(WARNING) << "Failed to compute pose for reference camera "
                     << camera_info_[kDefaultReferenceCameraId]
                            .camera_resource_handle.name()
                     << ": " << cam0_t_board.status();
        continue;
      }

      Pose3d cam_i_t_board = cam_i_t_cam0 * (*cam0_t_board);

      absl::StatusOr<ReprojectionErrorResult> result =
          ComputeReprojectionErrorForDetection(
              detection_i, active_camera_params[i], cam_i_t_board);
      if (!result.ok()) {
        LOG(WARNING) << "Failed to compute reprojection error for camera "
                     << camera_info_[i].camera_resource_handle.name() << ": "
                     << result.status();
        continue;
      }

      total_reprojection_error += result->squared_error;
      total_points += result->num_points;
      max_error2d = std::max(max_error2d, result->max_error);
    }

    if (total_points == 0) {
      return absl::FailedPreconditionError(absl::StrFormat(
          "Could not compute camera-to-camera validation error between "
          "reference "
          "camera '%s' and camera '%s': no overlapping pattern detections.",
          camera_info_[kDefaultReferenceCameraId].camera_resource_handle.name(),
          camera_info_[i].camera_resource_handle.name()));
    }

    double rmse = std::sqrt(total_reprojection_error / total_points);

    intrinsic_proto::perception::v1::StereoValidationResult* result =
        multi_camera_validation_result->add_stereo_validation_results();
    result->set_rms_error_2d(rmse);
    result->set_max_error_2d(max_error2d);
  }

  return absl::OkStatus();
}

grpc::Status CalibrationServiceImpl::Validate(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::ValidateRequest* request,
    intrinsic_proto::perception::v1::ValidationResult* response) {
  response->Clear();
  absl::MutexLock lock(mutex_);

  if (calibration_data_.empty()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Validation data is empty.");
  }
  if (request->validate_intrinsics()) {
    INTR_RETURN_IF_ERROR_GRPC(
        ValidateIntrinsics(request, response, calibration_data_));
  }
  if (request->validate_camera_to_robot()) {
    INTR_RETURN_IF_ERROR_GRPC(
        ValidateCameraToRobot(request, response, calibration_data_));
  }
  if (request->validate_camera_to_camera()) {
    INTR_RETURN_IF_ERROR_GRPC(
        ValidateCameraToCamera(request, response, calibration_data_));
  }
  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::Calibrate(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::CalibrationRequest* request,
    intrinsic_proto::perception::v1::CalibrationResult* response) {
  response->Clear();
  absl::MutexLock lock(mutex_);

  std::vector<CalibrationDataPoint> filtered_data;
  absl::Span<const CalibrationDataPoint> data_to_use;

  if (request->exclude_capture_ids_size() > 0) {
    absl::flat_hash_set<std::string> excluded_ids(
        request->exclude_capture_ids().begin(),
        request->exclude_capture_ids().end());
    filtered_data.reserve(calibration_data_.size());
    for (const auto& data : calibration_data_) {
      if (!excluded_ids.contains(data.capture_id)) {
        filtered_data.push_back(data);
      }
    }
    data_to_use = filtered_data;
  } else {
    data_to_use = calibration_data_;
  }

  if (data_to_use.empty()) {
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Calibration data is empty after filtering.");
  }
  if (request->calibrate_intrinsics()) {
    INTR_RETURN_IF_ERROR_GRPC(
        CalibrateIntrinsics(request, response, data_to_use));
  }
  if (request->calibrate_camera_to_robot()) {
    INTR_RETURN_IF_ERROR_GRPC(
        CalibrateCameraToRobot(request, response, data_to_use));
  }
  if (request->calibrate_camera_to_camera()) {
    INTR_RETURN_IF_ERROR_GRPC(
        CalibrateCameraToCamera(request, response, data_to_use));
  }
  if (request->calibrate_capture_to_capture()) {
    INTR_RETURN_IF_ERROR_GRPC(
        CalibrateCaptureToCapture(request, response, data_to_use));
  }
  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::Save(
    grpc::ServerContext* context,
    const intrinsic_proto::perception::v1::CalibrationResult* request,
    google::protobuf::Empty* response) {
  if (request->has_capture_to_capture_calibration_result()) {
    LOG(WARNING)
        << "Saving capture-to-capture calibration results is not supported.";
  }
  if (request->intrinsic_calibration_results().empty() &&
      request->camera_to_robot_calibration_results().empty() &&
      !request->has_camera_to_camera_calibration_result()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "No calibration results were provided.");
  }
  absl::MutexLock lock(mutex_);

  if (!request->intrinsic_calibration_results().empty()) {
    INTR_RETURN_IF_ERROR_GRPC(SaveIntrinsicCalibrationResults(request));
  }
  if (request->camera_to_robot_calibration_results_size() > 0 ||
      request->has_camera_to_camera_calibration_result()) {
    INTR_RETURN_IF_ERROR_GRPC(SaveCameraPosesToInitialWorld(request));
  }
  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::SaveCameraPosesToInitialWorld(
    const intrinsic_proto::perception::v1::CalibrationResult* request) {
  auto object_world_service_stub = GetOrCreateObjectWorldServiceStub();
  world::ObjectWorldClient init_world_client(initial_world_id_,
                                             object_world_service_stub);

  if (request->camera_to_robot_calibration_results_size() > 0) {
    if (request->camera_to_robot_calibration_results_size() !=
        camera_info_.size()) {
      return grpc::Status(
          grpc::StatusCode::INVALID_ARGUMENT,
          "Number of camera-to-robot calibration results does not match number "
          "of cameras.");
    }
    const auto& calibration_results =
        request->camera_to_robot_calibration_results();

    for (int i = 0; i < camera_info_.size(); ++i) {
      const std::string& camera_name =
          camera_info_[i].camera_resource_handle.name();
      auto camera_init =
          init_world_client.GetObject(WorldObjectName(camera_name));
      if (!camera_init.ok()) {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            absl::StrCat("Failed to get camera ", camera_name,
                         " from init world: ", camera_init.status().message()));
      }
      INTR_ASSIGN_OR_RETURN_GRPC(const auto camera_frame,
                                 camera_init->GetFrame(SensorFrameName()));
      struct WorldUpdateParameters world_update_params{
          .camera = *camera_init,
          .camera_frame = camera_frame,
          .robot = robot_object_.value(),
          .flange = robot_flange_.value(),
      };
      auto status = UpdateCameraPoseInWorld(
          calibration_results[i], world_update_params, init_world_client);
      if (!status.ok()) {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            absl::StrCat("Failed to update transform for camera ", camera_name,
                         " in init world: ", status.message()));
      }
    }
  }

  if (request->has_camera_to_camera_calibration_result()) {
    if (request->camera_to_camera_calibration_result()
            .stereo_calibration_results_size() != camera_info_.size() - 1) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Number of camera-to-camera calibration results does "
                          "not match number of cameras.");
    }
    const auto& calibration_results =
        request->camera_to_camera_calibration_result()
            .stereo_calibration_results();

    const std::string& reference_camera_name =
        camera_info_[kDefaultReferenceCameraId].camera_resource_handle.name();
    auto reference_camera_init =
        init_world_client.GetObject(WorldObjectName(reference_camera_name));
    if (!reference_camera_init.ok()) {
      return grpc::Status(
          grpc::StatusCode::INTERNAL,
          absl::StrCat("Failed to get reference camera ", reference_camera_name,
                       " from initial world: ",
                       reference_camera_init.status().message()));
    }

    for (int i = 0; i < camera_info_.size() - 1; ++i) {
      const std::string& camera_name =
          camera_info_[i + 1].camera_resource_handle.name();
      auto camera_init =
          init_world_client.GetObject(WorldObjectName(camera_name));
      if (!camera_init.ok()) {
        return grpc::Status(grpc::StatusCode::INTERNAL,
                            absl::StrCat("Failed to get camera ", camera_name,
                                         " from initial world: ",
                                         camera_init.status().message()));
      }
      auto status = UpdateCameraPoseInWorld(calibration_results[i],
                                            *reference_camera_init,
                                            *camera_init, init_world_client);
      if (!status.ok()) {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            absl::StrCat("Failed to update transform for camera ", camera_name,
                         " in initial world: ", status.message()));
      }
    }
  }

  LOG(WARNING) <<
    "Saving the initial "
    "world via the conductor "
    "service is unavailable. "
    "You need to copy camera "
    "poses manually.";
  return grpc::Status::OK;
}

grpc::Status CalibrationServiceImpl::SaveIntrinsicCalibrationResults(
    const intrinsic_proto::perception::v1::CalibrationResult* request) {
  if (request->intrinsic_calibration_results_size() != camera_info_.size()) {
    return grpc::Status(
        grpc::StatusCode::INVALID_ARGUMENT,
        "Number of intrinsic calibration results does not match number of "
        "cameras.");
  }
  for (int i = 0; i < request->intrinsic_calibration_results_size(); ++i) {
    const auto& intrinsic_calibration_result =
        request->intrinsic_calibration_results(i);
    if (!intrinsic_calibration_result.has_intrinsic_params()) {
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          absl::StrCat("No intrinsic parameters were provided "
                                       "in the calibration result for camera ",
                                       i));
    }
    if (camera_info_[i].is_multi_sensor) {
      return grpc::Status(
          grpc::StatusCode::FAILED_PRECONDITION,
          "Updating the camera config of a multi-sensor camera is not "
          "supported.");
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::perception::v1::CameraConfig camera_config,
        GetCameraConfigFromAssetInstances(
            camera_info_[i].camera_resource_handle.name()));

    // Update the camera config based on the latest calibration result.
    auto& configs = *camera_config.mutable_sensor_configs();
    const int64_t default_sensor_id = camera_info_[i].default_sensor_id;
    auto it = absl::c_find_if(
        configs, [&](const auto& c) { return c.id() == default_sensor_id; });

    auto& sensor_config = (it != configs.end()) ? *it : *([&]() {
      auto* new_sensor_config = camera_config.add_sensor_configs();
      new_sensor_config->set_id(default_sensor_id);
      return new_sensor_config;
    }());

    if (intrinsic_calibration_result.has_intrinsic_params()) {
      *sensor_config.mutable_camera_params()->mutable_intrinsic_params() =
          intrinsic_calibration_result.intrinsic_params();
    }
    if (intrinsic_calibration_result.has_distortion_params()) {
      *sensor_config.mutable_camera_params()->mutable_distortion_params() =
          intrinsic_calibration_result.distortion_params();
    }

    // Update the camera config in the resource registry.
    grpc::ClientContext update_context;
    intrinsic_proto::assets::UpdateResourceRequest update_request;
    intrinsic_proto::assets::Resource* resource =
        update_request.mutable_resource();
    resource->set_name(camera_info_[i].camera_resource_handle.name());
    if (!resource->mutable_configuration()->mutable_configuration()->PackFrom(
            camera_config)) {
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "Failed to pack camera config.");
    }
    google::longrunning::Operation operation;
    intrinsic_proto::assets::AssetDeploymentService::StubInterface*
        asset_deployment_service_stub = GetOrCreateAssetDeploymentServiceStub();
    INTR_RETURN_IF_ERROR_GRPC(asset_deployment_service_stub->UpdateResource(
        &update_context, update_request, &operation));

    grpc::ClientContext wait_context;
    google::longrunning::WaitOperationRequest wait_request;
    *wait_request.mutable_name() = operation.name();
    INTR_ASSIGN_OR_RETURN_GRPC(
        *wait_request.mutable_timeout(),
        FromAbslDuration(kGrpcClientServiceCallDefaultTimeout));
    google::longrunning::Operations::StubInterface* operations_stub =
        GetOrCreateOperationsStub();
    INTR_RETURN_IF_ERROR_GRPC(operations_stub->WaitOperation(
        &wait_context, wait_request, &operation));
    if (operation.has_error()) {
      return grpc::Status(
          grpc::StatusCode::INTERNAL,
          absl::StrCat("Failed to update camera config in resource registry: ",
                       operation.error().message()));
    }
    LOG(INFO) << "Successfully updated camera config for"
              << camera_info_[i].camera_resource_handle.name()
              << " in resource registry.";
  }
  return grpc::Status::OK;
}

absl::Status CalibrationServiceImpl::ScheduleCoverageMapsUpdate(
    std::vector<std::vector<Vector2f>>&& image_points_per_camera,
    std::function<absl::StatusOr<Image<Gray8u>>(const std::vector<Vector2f>&,
                                                const Image<Gray8u>&)>
        update_function) {
  return executor_.Schedule([image_points_per_camera =
                                 std::move(image_points_per_camera),
                             update_function = std::move(update_function),
                             this] {
    const auto status = [&]() -> absl::Status {
      absl::MutexLock coverage_lock(&coverage_mutex_);
      if (image_points_per_camera.size() != coverage_maps_.size() ||
          coverage_maps_.size() != coverage_map_publishers_.size()) {
        return absl::InternalError(
            "Invalid assumption about number of cameras.");
      }
      for (int i = 0; i < image_points_per_camera.size(); ++i) {
        const std::vector<Vector2f>& image_points = image_points_per_camera[i];
        const Publisher& coverage_map_publisher = coverage_map_publishers_[i];
        Image<Gray8u>& coverage_map = coverage_maps_[i];
        if (!image_points.empty()) {
          INTR_ASSIGN_OR_RETURN(const std::vector<Vector2f> pattern_contour,
                                ComputeConvexHull(image_points));
          INTR_ASSIGN_OR_RETURN(coverage_map,
                                update_function(pattern_contour, coverage_map));
        }
        INTR_ASSIGN_OR_RETURN(
            const intrinsic_proto::perception::v1::ImageBuffer coverage_heatmap,
            ToProto(
                GenerateHeatmap(coverage_map, kCoverageMapScaleFactor *
                                                  coverage_map.dimensions()),
                Encoding::kJpeg));
        INTR_RETURN_IF_ERROR(coverage_map_publisher.Publish(coverage_heatmap));
      }
      return absl::OkStatus();
    }();
    if (!status.ok()) {
      LOG(ERROR) << "Failed to update/publish coverage map: " << status;
    }
  });
}

google::longrunning::Operations::StubInterface*
CalibrationServiceImpl::GetOrCreateOperationsStub() {
  absl::MutexLock stub_lock(&stub_mutex_);
  if (operations_stub_ == nullptr) {
    auto status_or_operations_channel =
        intrinsic::assets::dependencies::Connect(
            intrinsic_proto::assets::v1::ResolvedDependency(),
            "grpc://google.longrunning.Operations",
            connect::UnlimitedMessageSizeGrpcChannelArgs());
    CHECK_OK(status_or_operations_channel.status());
    auto operations_channel = status_or_operations_channel.value();
    CHECK_OK(intrinsic::connect::WaitForChannelReady(operations_channel));
    operations_stub_ =
        google::longrunning::Operations::NewStub(operations_channel);
  }
  return operations_stub_.get();
}

intrinsic_proto::assets::v1::AssetInstances::StubInterface*
CalibrationServiceImpl::GetOrCreateAssetInstancesStub() {
  absl::MutexLock stub_lock(&stub_mutex_);
  if (asset_instances_stub_ == nullptr) {
    auto status_or_asset_instances_channel =
        intrinsic::assets::dependencies::Connect(
            intrinsic_proto::assets::v1::ResolvedDependency(),
            "grpc://intrinsic_proto.assets.v1.AssetInstances",
            connect::UnlimitedMessageSizeGrpcChannelArgs());
    CHECK_OK(status_or_asset_instances_channel.status());
    auto asset_instances_channel = status_or_asset_instances_channel.value();
    CHECK_OK(intrinsic::connect::WaitForChannelReady(asset_instances_channel));
    asset_instances_stub_ =
        intrinsic_proto::assets::v1::AssetInstances::NewStub(
            asset_instances_channel);
  }
  return asset_instances_stub_.get();
}

std::shared_ptr<grpc::Channel>
CalibrationServiceImpl::GetOrCreateIngressChannel() {
  if (ingress_channel_ == nullptr) {
    auto status_or_ingress_channel = connect::CreateClientChannel(
        kIngressAddress,
        absl::Now() + connect::kGrpcClientConnectDefaultTimeout);
    CHECK_OK(status_or_ingress_channel.status());
    ingress_channel_ = status_or_ingress_channel.value();
  }
  return ingress_channel_;
}

intrinsic_proto::assets::AssetDeploymentService::StubInterface*
CalibrationServiceImpl::GetOrCreateAssetDeploymentServiceStub() {
  absl::MutexLock stub_lock(&stub_mutex_);
  if (asset_deployment_service_stub_ == nullptr) {
    asset_deployment_service_stub_ =
        intrinsic_proto::assets::AssetDeploymentService::NewStub(
            GetOrCreateIngressChannel());
  }
  return asset_deployment_service_stub_.get();
}

std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
CalibrationServiceImpl::GetOrCreateObjectWorldServiceStub() {
  absl::MutexLock stub_lock(&stub_mutex_);
  if (object_world_service_stub_ == nullptr) {
    auto status_or_object_world_service_channel =
        intrinsic::assets::dependencies::Connect(
            intrinsic_proto::assets::v1::ResolvedDependency(),
            "grpc://intrinsic_proto.world.ObjectWorldService",
            connect::UnlimitedMessageSizeGrpcChannelArgs());
    CHECK_OK(status_or_object_world_service_channel.status());
    auto object_world_service_channel =
        status_or_object_world_service_channel.value();
    CHECK_OK(
        intrinsic::connect::WaitForChannelReady(object_world_service_channel));
    object_world_service_stub_ =
        intrinsic_proto::world::ObjectWorldService::NewStub(
            object_world_service_channel);
  }
  return object_world_service_stub_;
}

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
CalibrationServiceImpl::GetCameraConfigFromAssetInstances(
    absl::string_view camera_resource_handle_name) {
  // Get the current camera config from the AssetInstances service.
  intrinsic_proto::assets::v1::GetAssetInstanceRequest get_request;
  // TODO(b/436472041): Validate that the resource instance name matches the
  // camera resource handle name.
  get_request.set_name(camera_resource_handle_name);
  grpc::ClientContext get_context;
  intrinsic_proto::assets::v1::AssetInstance asset_instance;
  intrinsic_proto::assets::v1::AssetInstances::StubInterface*
      asset_instances_stub = GetOrCreateAssetInstancesStub();
  grpc::Status status = asset_instances_stub->GetAssetInstance(
      &get_context, get_request, &asset_instance);
  if (!status.ok()) {
    return absl::InternalError(absl::StrCat(
        "Failed to get asset instance: ", camera_resource_handle_name,
        ". Error: ", status.error_message()));
  }

  if (!asset_instance.has_config() ||
      !asset_instance.config().has_hardware_device() ||
      !asset_instance.config().hardware_device().has_service()) {
    return absl::NotFoundError(
        absl::StrCat("HardwareDevice service configuration is missing for: ",
                     camera_resource_handle_name));
  }

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::perception::v1::CameraConfig camera_config,
      UnpackCameraConfig(asset_instance.config()
                             .hardware_device()
                             .service()
                             .service_config()));
  return camera_config;
}

}  // namespace intrinsic::perception
