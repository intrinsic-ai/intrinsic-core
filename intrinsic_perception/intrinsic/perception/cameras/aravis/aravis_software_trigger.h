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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_SOFTWARE_TRIGGER_H_
#define INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_SOFTWARE_TRIGGER_H_

#include <arv.h>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace intrinsic::perception {

// Puts the camera in a mode where frames are acquired through sending a
// software triggering command to it.
absl::Status SetSoftwareTrigger(ArvCamera* absl_nonnull camera);

// Puts the camera in a mode where frames are freely acquired.
absl::Status SetFreeRunningTrigger(ArvCamera* absl_nonnull camera);

// Puts the camera in a mode where frames are aquired based on a periodic signal
// on the camera itself.
absl::Status SetPeriodicTrigger(ArvCamera* absl_nonnull camera);

// Returns true, if the camera has configured software triggering.
absl::StatusOr<bool> UsesSoftwareTrigger(ArvCamera* absl_nonnull camera);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_ARAVIS_ARAVIS_SOFTWARE_TRIGGER_H_
