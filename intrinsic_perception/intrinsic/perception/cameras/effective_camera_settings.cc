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

#include "intrinsic/perception/cameras/effective_camera_settings.h"

#include <cmath>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/genicam/feature_names.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic::perception {

namespace {

using SelectorMap = EffectiveCameraSettings::SelectorMap;
using SelectorContext = EffectiveCameraSettings::SelectorContext;
using SettingKey = EffectiveCameraSettings::SettingKey;
using SettingsMap = absl::btree_map<SettingKey, CameraSetting::Value>;
using AutoModeFeatureMap =
    absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>;

constexpr absl::string_view kSelectorSuffix = "Selector";
constexpr absl::string_view kOff = "Off";
constexpr absl::string_view kOnce = "Once";
constexpr absl::string_view kContinuous = "Continuous";

// Returns the baseline map from a selected feature name to the selector feature
// name(s) that govern it, covering standard GenICam SFNC features in
// `genicam/feature_names.h` and Intrinsic virtual driver features
// (`SoftwareHdr`). Additional camera-specific selectors are discovered via
// `GetSelectorsBySelectedFeature(ArvCamera*)`.
const SelectorMap& GetBaselineSelectorsBySelectedFeature() {
  static const absl::NoDestructor<SelectorMap> kMap({
      // Intrinsic virtual SoftwareHdr / MultiSlope HDR features
      {genicam::kMultiSlopeExposureLimit,
       {genicam::kMultiSlopeKneePointSelector}},

      // Analog Control: Gain & White Balance
      {genicam::kGain, {genicam::kGainSelector}},
      {genicam::kGainRaw, {genicam::kGainSelector}},
      {genicam::kGainAbs, {genicam::kGainSelector}},
      {genicam::kGainAuto, {genicam::kGainSelector}},
      {genicam::kBalanceRatio, {genicam::kBalanceRatioSelector}},

      // Acquisition & Exposure Control
      {genicam::kExposureTime, {genicam::kExposureTimeSelector}},
      {genicam::kExposureTimeAbs, {genicam::kExposureTimeSelector}},
      {genicam::kExposureTimeRaw, {genicam::kExposureTimeSelector}},
      {genicam::kTriggerSource, {genicam::kTriggerSelector}},
      {genicam::kTriggerActivation, {genicam::kTriggerSelector}},

      // Image Format / Component Control
      {genicam::kComponentEnable, {genicam::kComponentSelector}},
      {genicam::kComponentIDValue, {genicam::kComponentSelector}},
      {genicam::kPixelFormat, {genicam::kComponentSelector}},

      // Chunk Data Control
      {genicam::kChunkModeActive, {genicam::kChunkSelector}},
  });
  return *kMap;
}

// Returns true if `feature_name` is a known selector feature (either in
// `selector_features` or following the GenICam SFNC `*Selector` naming
// convention).
bool IsSelectorFeature(
    absl::string_view feature_name,
    const absl::flat_hash_set<std::string>& selector_features) {
  return selector_features.contains(feature_name) ||
         absl::EndsWith(feature_name, kSelectorSuffix);
}

// Returns the map from each GenICam auto-control feature (`ExposureAuto`,
// `GainAuto`, `BalanceWhiteAuto`) to the manual features that are overridden
// when that auto mode is set to `Continuous`.
const AutoModeFeatureMap& GetAutoModeControlledManualFeatures() {
  static const absl::NoDestructor<AutoModeFeatureMap> kAutoModeMap({
      {genicam::kExposureAuto,
       {genicam::kExposureTime, genicam::kExposureTimeAbs,
        genicam::kExposureTimeRaw, genicam::kExposureTimeBaseAbs}},
      {genicam::kGainAuto,
       {genicam::kGain, genicam::kGainRaw, genicam::kGainAbs}},
      {genicam::kBalanceWhiteAuto, {genicam::kBalanceRatio}},
  });
  return *kAutoModeMap;
}

// Returns true if `feature_name` is an acquisition/transport mode feature
// managed internally by the driver depending on whether `Capture` or
// `CaptureStream` is active, and should therefore be ignored when comparing
// user-visible camera settings.
bool IsStreamTransportInternalFeature(absl::string_view feature_name) {
  static const absl::NoDestructor<absl::flat_hash_set<std::string>>
      kTransportFeatures({
          genicam::kAcquisitionMode,
          genicam::kTriggerMode,
          genicam::kAcquisitionFrameRateEnable,
          genicam::kAcquisitionFrameRateEnabled,
          genicam::kAcquisitionFrameRate,
          genicam::kAcquisitionFrameRateAbs,
          genicam::kFPS,
      });
  return kTransportFeatures->contains(feature_name);
}

// Returns true if `val` is an `Enumeration` whose value equals `expected`.
bool IsEnumValue(const CameraSetting::Value& val, absl::string_view expected) {
  const auto* enum_val = std::get_if<CameraSetting::Enumeration>(&val);
  return enum_val != nullptr && enum_val->value == expected;
}

// Snapshots the currently active values of all selector features that govern
// `feature_name` (empty if `feature_name` is not selector-governed).
SelectorContext BuildSelectorContext(
    absl::string_view feature_name,
    const SelectorMap& selectors_by_selected_feature,
    const absl::flat_hash_map<std::string, CameraSetting::Value>&
        active_selectors) {
  SelectorContext context;
  auto iter = selectors_by_selected_feature.find(feature_name);
  if (iter == selectors_by_selected_feature.end()) {
    return context;
  }
  for (const std::string& selector : iter->second) {
    if (auto sel_iter = active_selectors.find(selector);
        sel_iter != active_selectors.end()) {
      context.emplace(selector, sel_iter->second);
    }
  }
  return context;
}

// Returns true if `key` is a multi-slope HDR knee-point setting
// (`MultiSlopeKneePointCount` or `MultiSlopeExposureLimit`) that has no effect
// because `MultiSlopeMode` is set to `Off` in `settings_map`.
bool IsIgnoredWhenMultiSlopeOff(const SettingKey& key,
                                const SettingsMap& settings_map) {
  if (key.name != genicam::kMultiSlopeKneePointCount &&
      key.name != genicam::kMultiSlopeExposureLimit) {
    return false;
  }
  auto iter = settings_map.find(SettingKey{.name = genicam::kMultiSlopeMode});
  return iter != settings_map.end() && IsEnumValue(iter->second, kOff);
}

// Returns true if `key` is a `MultiSlopeExposureLimit` setting whose
// `MultiSlopeKneePointSelector` index is outside
// `[1, MultiSlopeKneePointCount]` in `settings_map` and is therefore inactive.
bool IsTruncatedMultiSlopeKneePoint(const SettingKey& key,
                                    const SettingsMap& settings_map) {
  if (key.name != genicam::kMultiSlopeExposureLimit) {
    return false;
  }
  auto count_iter =
      settings_map.find(SettingKey{.name = genicam::kMultiSlopeKneePointCount});
  if (count_iter == settings_map.end()) {
    return false;
  }
  const auto* count_val =
      std::get_if<CameraSetting::Integer>(&count_iter->second);
  if (count_val == nullptr) {
    return false;
  }
  auto sel_iter =
      key.active_selectors.find(genicam::kMultiSlopeKneePointSelector);
  if (sel_iter == key.active_selectors.end()) {
    return false;
  }
  const auto* selector_idx =
      std::get_if<CameraSetting::Integer>(&sel_iter->second);
  if (selector_idx == nullptr) {
    return false;
  }
  return selector_idx->value < 1 || selector_idx->value > count_val->value;
}

// Returns true if `auto_feature` is set to `Continuous` in `settings_map`,
// projecting `active_selectors` onto the selectors governing `auto_feature`
// before checking the selector-scoped key and falling back to the global
// (unselected) key only when no selector-scoped entry exists.
bool HasActiveContinuousAutoSetting(
    absl::string_view auto_feature, const SelectorContext& active_selectors,
    const SelectorMap& selectors_by_selected_feature,
    const SettingsMap& settings_map) {
  SelectorContext auto_selectors;
  if (auto sel_iter = selectors_by_selected_feature.find(auto_feature);
      sel_iter != selectors_by_selected_feature.end()) {
    for (const auto& [selector_name, selector_value] : active_selectors) {
      if (sel_iter->second.contains(selector_name)) {
        auto_selectors.emplace(selector_name, selector_value);
      }
    }
  }
  if (!auto_selectors.empty()) {
    auto selected_iter = settings_map.find(SettingKey{
        .name = std::string(auto_feature), .active_selectors = auto_selectors});
    if (selected_iter != settings_map.end()) {
      return IsEnumValue(selected_iter->second, kContinuous);
    }
  }
  auto global_iter =
      settings_map.find(SettingKey{.name = std::string(auto_feature)});
  return global_iter != settings_map.end() &&
         IsEnumValue(global_iter->second, kContinuous);
}

// Returns true if `key` is a manual setting (e.g. `ExposureTime`, `Gain`,
// `BalanceRatio`) that is overridden by an active `Continuous` auto mode in
// `settings_map`.
bool IsIgnoredByContinuousAutoMode(
    const SettingKey& key, const SelectorMap& selectors_by_selected_feature,
    const SettingsMap& settings_map) {
  return absl::c_any_of(
      GetAutoModeControlledManualFeatures(), [&](const auto& entry) {
        const auto& [auto_feature, manual_features] = entry;
        return manual_features.contains(key.name) &&
               HasActiveContinuousAutoSetting(
                   auto_feature, key.active_selectors,
                   selectors_by_selected_feature, settings_map);
      });
}

// Returns true if `key` is inactive in `settings_map` due to
// `MultiSlopeMode=Off`, a knee-point index exceeding
// `MultiSlopeKneePointCount`, or an active `Continuous` auto mode.
bool IsIgnoredByActiveAutoOrHdrMode(
    const SettingKey& key, const SelectorMap& selectors_by_selected_feature,
    const SettingsMap& settings_map) {
  return IsIgnoredWhenMultiSlopeOff(key, settings_map) ||
         IsTruncatedMultiSlopeKneePoint(key, settings_map) ||
         IsIgnoredByContinuousAutoMode(key, selectors_by_selected_feature,
                                       settings_map);
}

// Returns true if `setting` should not be cached in `EffectiveCameraSettings`
// (unset value, one-shot command, or stream transport-internal feature).
bool IsNonCacheableSetting(const CameraSetting& setting) {
  return std::holds_alternative<std::monostate>(setting.value) ||
         std::holds_alternative<CameraSetting::Command>(setting.value) ||
         IsStreamTransportInternalFeature(setting.name);
}

// Returns true if `value` is a `CameraSetting::Float` holding `NaN`.
bool IsNanFloat(const CameraSetting::Value& value) {
  const auto* float_val = std::get_if<CameraSetting::Float>(&value);
  return float_val != nullptr && std::isnan(float_val->value);
}

// Applies a single `setting`, updating `active_selectors` if `setting` is a
// selector feature or recording the `(feature_name, active_selectors)` entry in
// `settings_map` while skipping unset values, one-shot commands, and
// transport-internal features.
void ApplySetting(
    const CameraSetting& setting,
    const SelectorMap& selectors_by_selected_feature,
    const absl::flat_hash_set<std::string>& selector_features,
    absl::flat_hash_map<std::string, CameraSetting::Value>& active_selectors,
    SettingsMap& settings_map) {
  if (IsNonCacheableSetting(setting)) {
    return;
  }
  if (IsSelectorFeature(setting.name, selector_features)) {
    if (IsNanFloat(setting.value)) {
      LOG(WARNING) << "Ignoring NaN value for selector feature '"
                   << setting.name << "'.";
      return;
    }
    active_selectors[setting.name] = setting.value;
    return;
  }
  SelectorContext selector_ctx = BuildSelectorContext(
      setting.name, selectors_by_selected_feature, active_selectors);
  settings_map[SettingKey{.name = setting.name,
                          .active_selectors = std::move(selector_ctx)}] =
      setting.value;
}

// Merges `additional_selectors_by_selected_feature` (e.g. Tier 2
// device-specific selectors from Aravis DOM introspection) into a copy of the
// Tier 1 baseline selector map.
SelectorMap MergeWithBaselineSelectors(
    const SelectorMap& additional_selectors_by_selected_feature) {
  SelectorMap merged = GetBaselineSelectorsBySelectedFeature();
  for (const auto& [selected, selectors] :
       additional_selectors_by_selected_feature) {
    merged[selected].insert(selectors.begin(), selectors.end());
  }
  return merged;
}

absl::flat_hash_set<std::string> CollectSelectorFeatures(
    const SelectorMap& selectors_by_selected_feature) {
  absl::flat_hash_set<std::string> selector_features;
  for (const auto& [selected, selectors] : selectors_by_selected_feature) {
    selector_features.insert(selectors.begin(), selectors.end());
  }
  return selector_features;
}

// Returns `" under active selectors {<selector_1>, ...}"` when
// `active_selectors` is non-empty, or an empty string for stateless settings.
std::string FormatActiveSelectors(const SelectorContext& active_selectors) {
  if (active_selectors.empty()) {
    return "";
  }
  return absl::StrCat(" under active selectors {",
                      absl::StrJoin(active_selectors, ", ",
                                    [](std::string* out, const auto& entry) {
                                      const auto& [sel_name, sel_val] = entry;
                                      absl::StreamFormatter()(
                                          out, CameraSetting{.name = sel_name,
                                                             .value = sel_val});
                                    }),
                      "}");
}

// Checks whether a single `(key, req_val)` entry is compatible with `settings`.
absl::Status CheckSingleSettingCompatibility(
    const SettingKey& key, const CameraSetting::Value& req_val,
    const SelectorMap& selectors_by_selected_feature,
    const SettingsMap& settings) {
  if (IsIgnoredByActiveAutoOrHdrMode(key, selectors_by_selected_feature,
                                     settings)) {
    return absl::OkStatus();
  }

  const CameraSetting requested_setting{.name = key.name, .value = req_val};
  if (auto iter = settings.find(key); iter != settings.end()) {
    const CameraSetting active_setting{.name = key.name, .value = iter->second};
    if (!active_setting.IsApprox(requested_setting)) {
      return intrinsic::FailedPreconditionErrorBuilder()
             << "Requested camera setting (" << requested_setting
             << ") conflicts with active stream setting (" << active_setting
             << ")" << FormatActiveSelectors(key.active_selectors) << ".";
    }
    return absl::OkStatus();
  }

  return intrinsic::NotFoundErrorBuilder()
         << "Requested camera setting (" << requested_setting
         << ") is not configured on active stream"
         << FormatActiveSelectors(key.active_selectors) << ".";
}

}  // namespace

EffectiveCameraSettings::EffectiveCameraSettings(
    absl::Span<const CameraSetting> settings,
    const SelectorMap& additional_selectors_by_selected_feature)
    : selectors_by_selected_feature_(
          MergeWithBaselineSelectors(additional_selectors_by_selected_feature)),
      selector_features_(
          CollectSelectorFeatures(selectors_by_selected_feature_)) {
  UpdateSettings(settings);
}

bool EffectiveCameraSettings::IsSelector(absl::string_view feature_name) const {
  return IsSelectorFeature(feature_name, selector_features_);
}

void EffectiveCameraSettings::UpdateSetting(const CameraSetting& setting) {
  ApplySetting(setting, selectors_by_selected_feature_, selector_features_,
               active_selectors_, settings_);
}

void EffectiveCameraSettings::UpdateSettings(
    absl::Span<const CameraSetting> settings) {
  for (const CameraSetting& setting : settings) {
    UpdateSetting(setting);
  }
}

void EffectiveCameraSettings::CacheVerifiedSetting(SettingKey key,
                                                   CameraSetting::Value value) {
  settings_.insert_or_assign(std::move(key), std::move(value));
}

absl::StatusOr<std::vector<EffectiveCameraSettings::UnconfiguredSetting>>
EffectiveCameraSettings::FindUnconfiguredSettings(
    absl::Span<const CameraSetting> requested_settings) const {
  std::vector<UnconfiguredSetting> unconfigured;
  if (requested_settings.empty()) {
    return unconfigured;
  }
  absl::flat_hash_map<std::string, CameraSetting::Value> req_selectors =
      active_selectors_;
  SettingsMap req_settings;
  for (const CameraSetting& setting : requested_settings) {
    ApplySetting(setting, selectors_by_selected_feature_, selector_features_,
                 req_selectors, req_settings);
  }
  for (const auto& [key, req_val] : req_settings) {
    if (IsIgnoredByActiveAutoOrHdrMode(key, selectors_by_selected_feature_,
                                       req_settings)) {
      continue;
    }
    absl::Status status = CheckSingleSettingCompatibility(
        key, req_val, selectors_by_selected_feature_, settings_);
    if (!status.ok()) {
      if (absl::IsNotFound(status)) {
        unconfigured.push_back(
            UnconfiguredSetting{.key = key, .requested_value = req_val});
        continue;
      }
      return status;
    }
  }
  return unconfigured;
}

bool EffectiveCameraSettings::AreAllSettingsAlreadyApplied(
    absl::Span<const CameraSetting> requested_settings) const {
  if (requested_settings.empty()) {
    return true;
  }
  absl::flat_hash_map<std::string, CameraSetting::Value> req_selectors =
      active_selectors_;
  for (const CameraSetting& setting : requested_settings) {
    if (IsNonCacheableSetting(setting)) {
      return false;
    }
    if (IsSelectorFeature(setting.name, selector_features_)) {
      if (IsNanFloat(setting.value)) {
        return false;
      }
      req_selectors[setting.name] = setting.value;
      continue;
    }
    if (GetAutoModeControlledManualFeatures().contains(setting.name) &&
        IsEnumValue(setting.value, kOnce)) {
      return false;
    }
    SelectorContext selector_ctx = BuildSelectorContext(
        setting.name, selectors_by_selected_feature_, req_selectors);
    const SettingKey key{.name = setting.name,
                         .active_selectors = std::move(selector_ctx)};
    auto iter = settings_.find(key);
    if (iter == settings_.end()) {
      return false;
    }
    const CameraSetting active_setting{.name = key.name, .value = iter->second};
    if (!active_setting.IsApprox(setting)) {
      return false;
    }
  }
  return req_selectors == active_selectors_;
}

absl::Status EffectiveCameraSettings::CheckCompatibility(
    absl::Span<const CameraSetting> requested_settings) const {
  absl::StatusOr<std::vector<UnconfiguredSetting>> unconfigured =
      FindUnconfiguredSettings(requested_settings);
  if (!unconfigured.ok()) {
    return unconfigured.status();
  }
  if (!unconfigured->empty()) {
    const UnconfiguredSetting& first = unconfigured->front();
    const CameraSetting requested_setting{.name = first.key.name,
                                          .value = first.requested_value};
    return intrinsic::NotFoundErrorBuilder()
           << "Requested camera setting (" << requested_setting
           << ") is not configured on active stream"
           << FormatActiveSelectors(first.key.active_selectors) << ".";
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::perception
