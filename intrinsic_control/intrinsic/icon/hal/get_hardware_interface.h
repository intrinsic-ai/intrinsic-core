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

#ifndef INTRINSIC_ICON_HAL_GET_HARDWARE_INTERFACE_H_
#define INTRINSIC_ICON_HAL_GET_HARDWARE_INTERFACE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "flatbuffers/table.h"
#include "flatbuffers/verifier.h"
#include "intrinsic/icon/hal/hardware_interface_handle.h"
#include "intrinsic/icon/hal/hardware_interface_traits.h"
#include "intrinsic/icon/hal/icon_state_register.h"  // IWYU pragma: keep
#include "intrinsic/icon/hal/interfaces/icon_state.fbs.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/domain_socket_utils.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/memory_segment.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/segment_header.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/segment_info.fbs.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/segment_info_utils.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::icon {

namespace hal {
// LINT.IfChange
static constexpr char kModuleInfoName[] = "intrinsic_module_info";
// LINT.ThenChange(//intrinsic_control/intrinsic/icon/hal/tools/hardware_module_introspection.cc)
}  // namespace hal

// Returns an error if the type or version of the segment header doesn't match
// SegmentHeader::ExpectedVersion().
// The parameter `interface_name` is used for error reporting.
template <class HardwareInterfaceT>
absl::Status SegmentHeaderIsValid(const SegmentHeader& segment_header,
                                  absl::string_view interface_name) {
  if (segment_header.Version() != SegmentHeader::ExpectedVersion()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Version mismatch: Interface '", interface_name, "' has version '",
        segment_header.Version(), "' but expected version '",
        SegmentHeader::ExpectedVersion(), "'"));
  }

  const SegmentHeader::TypeInfo type_info = segment_header.Type();
  if (!type_info.IsValid()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid type ID size: Interface '", interface_name,
                     "' has corrupted type header exceeding max size ",
                     SegmentHeader::TypeInfo::kMaxSize));
  }

  if (type_info.TypeID() !=
      hardware_interface_traits::TypeID<HardwareInterfaceT>::kTypeString) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Type mismatch: Interface '", interface_name, "' has type '",
        type_info.TypeID(), "' but expected type '",
        hardware_interface_traits::TypeID<HardwareInterfaceT>::kTypeString,
        "'"));
  }
  return absl::OkStatus();
}

// This function verifies that flatbuffer tables are internally consistent by
// running the flatbuffer verifier if `HardwareInterfaceT` is a flatbuffer
// table. The verifier checks all offsets, all sizes of fields, and null
// termination of strings to ensure that when a buffer is accessed, all reads
// will end up inside the buffer.
// Skips verification for all other types because they are either a flatbuffer
// struct (and those don't support verification), or not a flatbuffer.
// For those types MemorySegment::Get() already performs all the checking we
// can do. For flatbuffer structs, that simpler check is correct, but for
// non-flatbuffer types, it might not be.
//
// `HardwareInterfaceT` is the type of the data stored in the shared memory
// segment.
//
// The parameter `buffer` is a pointer to the start of the payload of a hardware
// interface's shared memory segment.
// The parameter `buffer_size` is the size of the buffer.
// The parameter `interface_name` is only used for error reporting.
//
// Returns InvalidArgumentError if `HardwareInterfaceT` is a flatbuffer table,
// but the buffer failed verification.
template <class HardwareInterfaceT>
absl::Status FlatbufferIsValid(const uint8_t* buffer, size_t buffer_size,
                               absl::string_view interface_name) {
  // Only run verifier if `HardwareInterfaceT` is a flatbuffer table.
  if constexpr (std::is_base_of_v<flatbuffers::Table, HardwareInterfaceT>) {
    flatbuffers::Verifier verifier(/*buf=*/buffer,
                                   /*size*/ buffer_size);
    if (!verifier.VerifyBuffer<HardwareInterfaceT>()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Flatbuffer verification failed for interface '", interface_name,
          "'. This can be due to a version mismatch of your resources."));
    }
  }
  return absl::OkStatus();
}

// Returns a handle to a HardwareInterfaceT.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<HardwareInterfaceHandle<HardwareInterfaceT>>
GetInterfaceHandle(
    const SegmentNameToFileDescriptorMap& segment_name_to_file_descriptor_map,
    absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(
      auto ro_segment,
      ReadOnlyMemorySegment<HardwareInterfaceT>::Get(
          segment_name_to_file_descriptor_map, interface_name));

  INTR_RETURN_IF_ERROR(SegmentHeaderIsValid<HardwareInterfaceT>(
      ro_segment.Header(), interface_name));
  INTR_RETURN_IF_ERROR(FlatbufferIsValid<HardwareInterfaceT>(
      ro_segment.GetRawValue(), ro_segment.ValueSize(), interface_name));

  return HardwareInterfaceHandle<HardwareInterfaceT>(std::move(ro_segment));
}

// Returns a mutable handle to a HardwareInterfaceT.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<MutableHardwareInterfaceHandle<HardwareInterfaceT>>
GetMutableInterfaceHandle(
    const SegmentNameToFileDescriptorMap& segment_name_to_file_descriptor_map,
    absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(
      auto rw_segment,
      ReadWriteMemorySegment<HardwareInterfaceT>::Get(
          segment_name_to_file_descriptor_map, interface_name));

  INTR_RETURN_IF_ERROR(SegmentHeaderIsValid<HardwareInterfaceT>(
      rw_segment.Header(), interface_name));
  INTR_RETURN_IF_ERROR(FlatbufferIsValid<HardwareInterfaceT>(
      rw_segment.GetRawValue(), rw_segment.ValueSize(), interface_name));

  return MutableHardwareInterfaceHandle<HardwareInterfaceT>(
      std::move(rw_segment));
}

// Returns a handle to a HardwareInterfaceT.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<HardwareInterfaceHandle<HardwareInterfaceT>>
GetInterfaceHandle(const SharedMemoryManager& shared_memory_manager,
                   absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(
      auto ro_segment,
      shared_memory_manager.Get<ReadOnlyMemorySegment<HardwareInterfaceT>>(
          interface_name));

  INTR_RETURN_IF_ERROR(SegmentHeaderIsValid<HardwareInterfaceT>(
      ro_segment.Header(), interface_name));
  INTR_RETURN_IF_ERROR(FlatbufferIsValid<HardwareInterfaceT>(
      ro_segment.GetRawValue(), ro_segment.ValueSize(), interface_name));

  return HardwareInterfaceHandle<HardwareInterfaceT>(std::move(ro_segment));
}

// Returns a mutable handle to a HardwareInterfaceT.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<MutableHardwareInterfaceHandle<HardwareInterfaceT>>
GetMutableInterfaceHandle(const SharedMemoryManager& shared_memory_manager,
                          absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(
      auto rw_segment,
      shared_memory_manager.Get<ReadWriteMemorySegment<HardwareInterfaceT>>(
          interface_name));

  INTR_RETURN_IF_ERROR(SegmentHeaderIsValid<HardwareInterfaceT>(
      rw_segment.Header(), interface_name));
  INTR_RETURN_IF_ERROR(FlatbufferIsValid<HardwareInterfaceT>(
      rw_segment.GetRawValue(), rw_segment.ValueSize(), interface_name));

  return MutableHardwareInterfaceHandle<HardwareInterfaceT>(
      std::move(rw_segment));
}

// Returns a handle to a HardwareInterfaceT that checks that it was
// updated in the same ICON cycle as the "icon_state" interface.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<StrictHardwareInterfaceHandle<HardwareInterfaceT>>
GetStrictInterfaceHandle(const SharedMemoryManager& shared_memory_manager,
                         absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(
      auto handle, GetInterfaceHandle<HardwareInterfaceT>(shared_memory_manager,
                                                          interface_name));

  INTR_ASSIGN_OR_RETURN(auto icon_state,
                        GetInterfaceHandle<intrinsic_fbs::IconState>(
                            shared_memory_manager, kIconStateInterfaceName));

  return StrictHardwareInterfaceHandle<HardwareInterfaceT>(
      std::move(handle), std::move(icon_state));
}

// Returns a mutable handle to a HardwareInterfaceT that checks that it was
// updated in the same ICON cycle as the "icon_state" interface.
// The HardwareInterfaceT must be registered with the
// INTRINSIC_ADD_HARDWARE_INTERFACE macro.
template <class HardwareInterfaceT>
inline absl::StatusOr<MutableStrictHardwareInterfaceHandle<HardwareInterfaceT>>
GetMutableStrictInterfaceHandle(
    const SharedMemoryManager& shared_memory_manager,
    absl::string_view interface_name) {
  INTR_ASSIGN_OR_RETURN(auto handle,
                        GetMutableInterfaceHandle<HardwareInterfaceT>(
                            shared_memory_manager, interface_name));

  INTR_ASSIGN_OR_RETURN(auto icon_state,
                        GetInterfaceHandle<intrinsic_fbs::IconState>(
                            shared_memory_manager, kIconStateInterfaceName));

  return MutableStrictHardwareInterfaceHandle<HardwareInterfaceT>(
      std::move(handle), std::move(icon_state));
}

// Returns information about the exported interfaces from a hardware module.
inline absl::StatusOr<ReadOnlyMemorySegment<intrinsic_fbs::SegmentInfo>>
GetHardwareModuleInfo(
    const SegmentNameToFileDescriptorMap& segment_name_to_file_descriptor_map) {
  return ReadOnlyMemorySegment<intrinsic_fbs::SegmentInfo>::Get(
      segment_name_to_file_descriptor_map, hal::kModuleInfoName);
}

// Extracts the names of the shared memory segments.
inline absl::StatusOr<std::vector<std::string>> GetInterfacesFromModuleInfo(
    const intrinsic_fbs::SegmentInfo& segment_info) {
  return GetNamesFromSegmentInfo(segment_info);
}

// Extracts the names of the shared memory segments that are marked as required.
//
// Subset of GetInterfacesFromModuleInfo.
inline absl::StatusOr<std::vector<std::string>>
GetRequiredInterfacesFromModuleInfo(
    const intrinsic_fbs::SegmentInfo& segment_info) {
  return GetRequiredInterfaceNamesFromSegmentInfo(segment_info);
}

}  // namespace intrinsic::icon
#endif  // INTRINSIC_ICON_HAL_GET_HARDWARE_INTERFACE_H_
