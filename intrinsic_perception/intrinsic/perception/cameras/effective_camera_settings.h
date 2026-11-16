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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_EFFECTIVE_CAMERA_SETTINGS_H_
#define INTRINSIC_PERCEPTION_CAMERAS_EFFECTIVE_CAMERA_SETTINGS_H_

#include <compare>
#include <string>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/perception/cameras/camera_setting.h"

namespace intrinsic::perception {

// Reduces sequential `CameraSetting` lists into canonical
// `SettingKey -> CameraSetting::Value` state and checks compatibility
// between an active camera stream and incoming `Capture` / `CaptureStream`
// requests.
class EffectiveCameraSettings {
 public:
  // Maps a selector-governed feature name (e.g. `"BalanceRatio"` or
  // `"TriggerSource"`) to the name(s) of the selector feature(s) that control
  // it (e.g. `{"BalanceRatioSelector"}` or `{"TriggerSelector"}`).
  using SelectorMap =
      absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;

  // Maps each governing selector feature name to its active value when the
  // selected setting was applied (empty for stateless settings).
  using SelectorContext = absl::btree_map<std::string, CameraSetting::Value>;

  // Canonical key identifying a setting and the active selector state under
  // which it was applied. Selector values in `active_selectors` must not be NaN
  // so that `operator<=>` induces a strict weak ordering for `absl::btree_map`.
  struct SettingKey {
    std::string name;
    SelectorContext active_selectors;

    friend std::partial_ordering operator<=>(const SettingKey&,
                                             const SettingKey&) = default;
  };

  // An active-mode-relevant `(SettingKey, requested_value)` entry from a
  // requested setting list that is not yet configured in `this`.
  struct UnconfiguredSetting {
    SettingKey key;
    CameraSetting::Value requested_value;
  };

  // Initializes with the baseline GenICam SFNC and `SoftwareHdr` selector map
  // merged with `additional_selectors_by_selected_feature` (such as
  // device-specific selector mappings discovered via
  // `GetSelectorsBySelectedFeature(ArvCamera*)`) and reduces `settings` into
  // canonical `SettingKey -> CameraSetting::Value` state.
  EffectiveCameraSettings(
      absl::Span<const CameraSetting> settings,
      const SelectorMap& additional_selectors_by_selected_feature);

  // Returns true if `feature_name` is treated as a selector feature (either
  // registered in the merged selector map or ending with `"Selector"`).
  [[nodiscard]] bool IsSelector(absl::string_view feature_name) const;

  // Applies a single `setting` or sequential span of `settings` to the active
  // effective state, updating active selector context or recording the new
  // `SettingKey -> CameraSetting::Value` entry.
  void UpdateSetting(const CameraSetting& setting);
  void UpdateSettings(absl::Span<const CameraSetting> settings);

  // Records a hardware-verified `key -> value` entry without modifying the
  // active selector context stored in `this`.
  void CacheVerifiedSetting(SettingKey key, CameraSetting::Value value);

  // Reduces `requested_settings` (inheriting active selectors from `this` and
  // ignoring unset, command, transport-internal, and auto/HDR-overridden
  // entries), returning `FAILED_PRECONDITION` if any entry conflicts with
  // `this`, or the list of entries not configured in `this` (empty when all
  // entries are compatible).
  absl::StatusOr<std::vector<UnconfiguredSetting>> FindUnconfiguredSettings(
      absl::Span<const CameraSetting> requested_settings) const;

  // Returns true if every setting in `requested_settings` is already applied
  // with an approximately equal value in `this` and leaves the active selector
  // context unchanged. Unlike `CheckCompatibility`, returns false if any entry
  // is unset (`std::monostate`), a `Command`, a stream-transport feature (such
  // as `AcquisitionFrameRate` or `FPS`), a `"Once"` auto mode (which converges
  // to `"Off"` on hardware), a setting shadowed by `Continuous` auto or
  // `MultiSlopeMode=Off` whose stored value differs or is missing, or a
  // selector change that alters the active selector state.
  [[nodiscard]] bool AreAllSettingsAlreadyApplied(
      absl::Span<const CameraSetting> requested_settings) const;

  // Checks whether `requested_settings` is compatible with `this` (the active
  // camera/stream settings), returning `absl::OkStatus()` when compatible:
  // - Empty `requested_settings` is always compatible.
  // - Subset of active settings is compatible if all requested values match.
  // - Reordered settings or reordered selector blocks are compatible if their
  //   normalized effective `SettingKey` values match. Omitting a selector in
  //   `requested_settings` inherits the active selector state stored in `this`.
  // - Unset (`std::monostate`) values, `CameraSetting::Command` entries, stream
  //   transport-internal features (`AcquisitionMode`, `TriggerMode`, frame-rate
  //   settings), and manual or multi-slope HDR settings inactive under
  //   `Continuous` auto modes, `MultiSlopeMode=Off`, or out-of-range knee-point
  //   indices are ignored during comparison.
  // - Returns `FAILED_PRECONDITION` if any requested setting conflicts with a
  //   different value configured in `this`, or `NOT_FOUND` if no requested
  //   settings conflict but at least one requested setting is not configured in
  //   `this`.
  absl::Status CheckCompatibility(
      absl::Span<const CameraSetting> requested_settings) const;

 private:
  SelectorMap selectors_by_selected_feature_;
  absl::flat_hash_set<std::string> selector_features_;
  absl::flat_hash_map<std::string, CameraSetting::Value> active_selectors_;
  absl::btree_map<SettingKey, CameraSetting::Value> settings_;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_EFFECTIVE_CAMERA_SETTINGS_H_
