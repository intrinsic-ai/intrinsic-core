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

#ifndef INTRINSIC_ICON_INTERPROCESS_SHARED_MEMORY_MANAGER_SEGMENT_HEADER_H_
#define INTRINSIC_ICON_INTERPROCESS_SHARED_MEMORY_MANAGER_SEGMENT_HEADER_H_

#include <semaphore.h>
#include <stddef.h>

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>

#include "absl/strings/string_view.h"
#include "intrinsic/icon/utils/clock.h"

namespace intrinsic::icon {

// SegmentHeader is meta information about the shared memory segment.
// Each allocated segment is offset by this header information before its actual
// payload.
// An example shared memory segment for a single `int` type would look like the
// following:
//
// [SegmentHeader][payload = int]
//
// The payload for the allocated `int` can thus be natively accessed by
// offsetting the shared memory segment by the size of this header struct.
//
// TODO(b/211669943) We further have to provide some type information in this
// header to prevent undefined behavior when a segment is accessed by a
// different type than it was originally created.
//
// The SegmentHeader class is aligned to 64bit in order to guarantee a valid
// data representation across different platforms.
class alignas(64) SegmentHeader final {
 public:
  // The TypeInfo class contains convenience functions around the type string
  // associated with the shm segment payload.
  class alignas(64) TypeInfo {
   public:
    // The max size a type id can have.
    constexpr static size_t kMaxSize = 100;

    // Creates a new TypeInfo instance. Truncates `type_id` to `kMaxSize`
    // characters.
    explicit TypeInfo(const std::string& type_id) {
      std::memset(&type_id_, '\0', kMaxSize);
      size_t string_size =
          type_id.size() < kMaxSize ? type_id.size() : kMaxSize;
      std::memcpy(&type_id_, type_id.c_str(), string_size);
      type_id_size_ = string_size;
    }

    // Returns true if the stored size does not exceed kMaxSize.
    bool IsValid() const { return type_id_size_ <= kMaxSize; }

    // Returns the type id string, clamped to kMaxSize in case shared memory
    // was corrupted.
    absl::string_view TypeID() const {
      return absl::string_view(type_id_, std::min(type_id_size_, kMaxSize));
    }

    // Comparison operators.
    bool operator==(const TypeInfo& rhs) const {
      return IsValid() && rhs.IsValid() && type_id_size_ == rhs.type_id_size_ &&
             std::strncmp(type_id_, rhs.type_id_, type_id_size_) == 0;
    }
    bool operator!=(const TypeInfo& rhs) const { return !(*this == rhs); }

   private:
    size_t type_id_size_;
    char type_id_[kMaxSize];
  };

  enum class Flags : int {
    // Creates an exclusively owned memory segment.
    // Indicates that the memory segment is exclusively created by a single
    // owner. The shared memory manager shall not repurpose or override the
    // shared memory segment if this flag is set.
    kExclusiveOwnership = 0,
  };

  // The SegmentHeader class is move-only.
  SegmentHeader() noexcept;
  explicit SegmentHeader(const std::string& type_id) noexcept;
  SegmentHeader(const std::string& type_id,
                const std::initializer_list<Flags>& flags) noexcept;
  SegmentHeader(const SegmentHeader& other) noexcept = delete;
  SegmentHeader& operator=(const SegmentHeader& other) noexcept = delete;
  SegmentHeader(SegmentHeader&& other) noexcept = default;
  SegmentHeader& operator=(SegmentHeader&& other) noexcept = delete;
  ~SegmentHeader() noexcept;

  // Gets the current reference count of read-only access handles.
  int ReaderRefCount() const;
  // Increments the current reference count of read-only access handles.
  void IncrementReaderRefCount();
  // Decrements the current reference count of read-only access handles.
  void DecrementReaderRefCount();

  // Gets the current reference count of writer access handles.
  int WriterRefCount() const;
  // Increments the current reference count of writer access handles.
  void IncrementWriterRefCount();
  // Decrements the current reference count of writer access handles.
  void DecrementWriterRefCount();

  // Returns the type information for this segment.
  TypeInfo Type() const;

  // Queries if a specified flag is set.
  bool FlagIsSet(Flags flag) const;

  // Returns intrinsic::Clock::Zero() if no update has occurred yet.
  Time LastUpdatedTime() const;

  // Returns the number of updates made to the segment.
  int64_t NumUpdates() const;

  // Returns the control cycle that this segment was updated at.
  // Returns Zero if no updates were made, or when uint64_t overruns.
  uint64_t LastUpdatedCycle() const;

  // Sets the time the associated segment was updated and increments the number
  // of updates.
  //
  // Important: Typically, the time passed should be obtained from a monotonic
  // clock to ensure there is no bad behavior caused by the clock changing in
  // unexpected ways (for example, going backwards in time).
  // 'current_cycle' is the control cycle that the segment was updated.
  void UpdatedAt(Time time, uint64_t current_cycle);

  // The expected version of the SegmentHeader not stored in shared memory.
  static constexpr size_t ExpectedVersion() {
    // Version of the SegmentHeader.
    // Increment on changes that break backwards compatibility. E.g. if the size
    // of the members changes.
    // Is static to compare the expected version to the version in the shared
    // memory segment.
    // LINT.IfChange(expected_version)
    return 4;
    // LINT.ThenChange()
  }

  // The version of the SegmentHeader as stored in shared memory.
  size_t Version() const { return kVersion; }

 private:
  friend class SegmentHeaderTestPeer;

  // Initializes a new shared memory segment with the expected version.
  const size_t kVersion = ExpectedVersion();

  // LINT.IfChange()
  // Unnamed process-shared semaphore to protect header modifications.
  mutable sem_t mutex_;

  // A reference counter on read-only access handles.
  int ref_count_reader_ = 0;

  // A reference counter on writer access handles.
  int ref_count_writer_ = 0;

  // The type information associated with that segment.
  TypeInfo type_info_;

  // Bitmask for single bit flags.
  std::bitset<8> flags_;

  // Time for the last time the segment was updated. This is used to detect
  // stale information in segments.
  //
  // It is up to the user of the segment to mark when it is updated. This
  // reduces the number of times we need to grab the clock, for example, the
  // user might record the time once at the start of a cycle, and use the same
  // time when updating all segments.
  Time last_updated_time_ = Clock::Zero();

  // Counter for updates to the segment. This is used by the reader to detect
  // missed updates.
  //
  // Given that a segment can contain arbitrary data, we won't make assumptions
  // about the access to the segment, and instead allow the writer of the
  // segment to tick it when appropriate.
  int64_t update_counter_ = 0;
  // The cycle at which the segment was updated.
  //
  // Uses different initial value than
  // intrinsic/icon/hal/interfaces/icon_state_utils.cc
  // so that uninitialized commands are invalid.
  uint64_t updated_at_cycle_ = 0;
  // LINT.ThenChange(:expected_version)
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_INTERPROCESS_SHARED_MEMORY_MANAGER_SEGMENT_HEADER_H_
