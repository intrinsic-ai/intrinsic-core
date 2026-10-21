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

#ifndef INTRINSIC_SKILLS_CC_SKILL_DATA_H_
#define INTRINSIC_SKILLS_CC_SKILL_DATA_H_

#include <any>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {

// Provides thread-safe in-memory caching across Skill lifecycle methods.
//
// SkillData caches values keyed by (context_id, key) across Skill lifecycle
// methods with automatic LRU eviction bounded by the maximum number of active
// context_ids.
//
// `max_contexts` corresponds to the maximum number of concurrent action
// execution contexts expected to be kept in memory (i.e., the maximum number of
// expected parallel calls of the Skill by the Executive).
class SkillData {
 public:
  // Constructs a SkillData cache with a maximum number of concurrent contexts.
  explicit SkillData(size_t max_contexts = 32);
  virtual ~SkillData() = default;

  SkillData(const SkillData&) = delete;
  SkillData& operator=(const SkillData&) = delete;

  // Returns the cached value for (context_id, key), or std::nullopt if not
  // found.
  //
  // If `context_id` is empty, logs a warning and returns std::nullopt without
  // querying the cache.
  //
  // If `validate_fn` is provided and a cached value exists:
  // - Returns cached value if `validate_fn` returns true.
  // - Evicts the cached key and returns std::nullopt if `validate_fn` returns
  //   false.
  // - Evicts the cached key and returns error status immediately if
  //   `validate_fn` returns an error status.
  //
  // Returns an InvalidArgumentError if the cached value's type does not match
  // T.
  template <typename T>
  absl::StatusOr<std::optional<T>> Get(
      absl::string_view context_id, absl::string_view key,
      std::function<absl::StatusOr<bool>(const T&)> validate_fn = nullptr) {
    if (context_id.empty()) {
      LOG(WARNING) << "SkillData: context_id is empty for key '" << key
                   << "'; cache lookup skipped.";
      return std::nullopt;
    }

    if (auto cached = cache_.Get(context_id, key)) {
      const T* val = std::any_cast<T>(&(*cached));
      if (val == nullptr) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Type mismatch for context_id '%s' and key '%s'", context_id, key));
      }

      if (validate_fn == nullptr) {
        return *val;
      }

      absl::StatusOr<bool> is_valid = validate_fn(*val);
      if (!is_valid.ok() || !*is_valid) {
        cache_.Erase(context_id, key);
        if (!is_valid.ok()) {
          return is_valid.status();
        }
        return std::nullopt;
      }
      return *val;
    }

    return std::nullopt;
  }

  // Returns the cached value for (context_id, key) or computes, stores, and
  // returns the result of `compute_fn`.
  //
  // If `context_id` is empty, logs a warning and computes via `compute_fn`
  // returning the result without caching.
  //
  // If `validate_fn` is provided and a cached value exists:
  // - Returns cached value if `validate_fn` returns true.
  // - Evicts the cached key, recomputes via `compute_fn`, updates cache, and
  //   returns fresh value if `validate_fn` returns false.
  // - Evicts the cached key and returns error status immediately if
  //   `validate_fn` returns an error status.
  //
  // Automatically evicts the least recently used context_id (and all its
  // associated keys) if the context capacity is reached.
  template <typename T>
  absl::StatusOr<T> GetOrCompute(
      absl::string_view context_id, absl::string_view key,
      std::function<absl::StatusOr<T>()> compute_fn,
      std::function<absl::StatusOr<bool>(const T&)> validate_fn = nullptr) {
    if (compute_fn == nullptr) {
      return absl::InvalidArgumentError("compute_fn must not be null.");
    }

    if (context_id.empty()) {
      LOG(WARNING) << "SkillData: context_id is empty for key '" << key
                   << "'; computing without caching.";
      return compute_fn();
    }

    INTR_ASSIGN_OR_RETURN(std::optional<T> cached,
                          Get<T>(context_id, key, validate_fn));
    if (cached.has_value()) {
      return std::move(*cached);
    }

    INTR_ASSIGN_OR_RETURN(T fresh_value, compute_fn());
    cache_.Put(context_id, key, fresh_value);
    return fresh_value;
  }

  // Deletes all entries for a specific context_id.
  // Returns true if the context was removed, false if not found.
  // If `context_id` is empty, logs a warning and returns false.
  bool Delete(absl::string_view context_id);

 private:
  class LRUCache {
   public:
    explicit LRUCache(size_t max_contexts);

    std::optional<std::any> Get(absl::string_view context_id,
                                absl::string_view key);
    void Put(absl::string_view context_id, absl::string_view key,
             std::any value);
    bool Erase(absl::string_view context_id);
    bool Erase(absl::string_view context_id, absl::string_view key);

   private:
    struct ContextEntry {
      absl::flat_hash_map<std::string, std::any> values;
      std::list<std::string>::iterator lru_it;
    };

    const size_t max_contexts_;
    mutable absl::Mutex mutex_;
    std::list<std::string> lru_contexts_ ABSL_GUARDED_BY(mutex_);
    absl::flat_hash_map<std::string, ContextEntry> contexts_
        ABSL_GUARDED_BY(mutex_);
  };

  LRUCache cache_;
};

// Returns a reference to the process-wide default SkillData instance.
SkillData& GetSkillData();

}  // namespace intrinsic::skills

#endif  // INTRINSIC_SKILLS_CC_SKILL_DATA_H_
