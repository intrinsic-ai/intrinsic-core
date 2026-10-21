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

#include "intrinsic/perception/cameras/aravis/aravis_software_trigger.h"

#include <arv.h>

#include <string_view>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/perception/cameras/aravis/aravis_utils.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {
namespace {

bool HasReadAccess(const ArvGcAccessMode access) {
  return (access == ArvGcAccessMode::ARV_GC_ACCESS_MODE_RO ||
          access == ArvGcAccessMode::ARV_GC_ACCESS_MODE_RW);
}

}  // namespace

absl::StatusOr<bool> UsesSoftwareTrigger(ArvCamera* absl_nonnull camera) {
  if (camera == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ArvCamera must not be a nullptr.";
  }

  GError* error = nullptr;

  INTR_ASSIGN_OR_RETURN(const ArvGcAccessMode trigger_mode_access,
                        GetAccessMode(genicam::kTriggerMode, camera));
  if (!HasReadAccess(trigger_mode_access)) {
    return false;
  }
  const std::string_view trigger_mode =
      arv_camera_get_string(camera, genicam::kTriggerMode, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  if (trigger_mode == "Off") {
    return false;
  }

  INTR_ASSIGN_OR_RETURN(const ArvGcAccessMode trigger_source_access,
                        GetAccessMode(genicam::kTriggerSource, camera));
  if (!HasReadAccess(trigger_source_access)) {
    return false;
  }
  const std::string_view trigger_source =
      arv_camera_get_string(camera, genicam::kTriggerSource, &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));
  return trigger_source == "Software";
}

absl::Status SetSoftwareTrigger(ArvCamera* absl_nonnull camera) {
  if (camera == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ArvCamera must not be a nullptr.";
  }

  GError* error = nullptr;
  arv_camera_set_trigger(camera, "Software", &error);
  return ValidateAndNullArvError(error);
}

absl::Status SetFreeRunningTrigger(ArvCamera* absl_nonnull camera) {
  if (camera == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ArvCamera must not be a nullptr.";
  }

  GError* error = nullptr;
  arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS,
                                  &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  arv_camera_clear_triggers(camera, &error);
  return ValidateAndNullArvError(error);
}

absl::Status SetPeriodicTrigger(ArvCamera* absl_nonnull camera) {
  if (camera == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "ArvCamera must not be a nullptr.";
  }

  GError* error = nullptr;
  arv_camera_set_acquisition_mode(camera, ARV_ACQUISITION_MODE_CONTINUOUS,
                                  &error);
  INTR_RETURN_IF_ERROR(ValidateAndNullArvError(error));

  arv_camera_set_trigger(camera, "PeriodicSignal1", &error);
  return ValidateAndNullArvError(error);
}

}  // namespace intrinsic::perception
