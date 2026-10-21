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

#include "intrinsic/skills/cc/skill_data.h"

#include <any>
#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace intrinsic::skills {

SkillData& GetSkillData() {
  static absl::NoDestructor<SkillData> skill_data;
  return *skill_data;
}

SkillData::LRUCache::LRUCache(size_t max_contexts)
    : max_contexts_(max_contexts) {}

std::optional<std::any> SkillData::LRUCache::Get(absl::string_view context_id,
                                                 absl::string_view key) {
  if (context_id.empty()) {
    return std::nullopt;
  }
  absl::MutexLock lock(&mutex_);
  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    return std::nullopt;
  }
  // Move accessed context to the front of the LRU list. std::list::splice
  // preserves iterator validity, so it->second.lru_it remains valid.
  lru_contexts_.splice(lru_contexts_.begin(), lru_contexts_, it->second.lru_it);
  auto val_it = it->second.values.find(key);
  if (val_it == it->second.values.end()) {
    return std::nullopt;
  }
  return val_it->second;
}

void SkillData::LRUCache::Put(absl::string_view context_id,
                              absl::string_view key, std::any value) {
  if (max_contexts_ == 0 || context_id.empty()) {
    return;
  }
  absl::MutexLock lock(&mutex_);
  auto it = contexts_.find(context_id);
  if (it != contexts_.end()) {
    it->second.values[std::string(key)] = std::move(value);
    // Touch context to mark as most recently used. std::list::splice preserves
    // iterator validity.
    lru_contexts_.splice(lru_contexts_.begin(), lru_contexts_,
                         it->second.lru_it);
    return;
  }

  if (contexts_.size() >= max_contexts_ && !lru_contexts_.empty()) {
    contexts_.erase(lru_contexts_.back());
    lru_contexts_.pop_back();
  }

  std::string ctx_str(context_id);
  lru_contexts_.push_front(ctx_str);
  ContextEntry entry;
  entry.values[std::string(key)] = std::move(value);
  entry.lru_it = lru_contexts_.begin();
  contexts_[ctx_str] = std::move(entry);
}

bool SkillData::LRUCache::Erase(absl::string_view context_id) {
  if (context_id.empty()) {
    return false;
  }
  absl::MutexLock lock(&mutex_);
  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    return false;
  }
  lru_contexts_.erase(it->second.lru_it);
  contexts_.erase(it);
  return true;
}

bool SkillData::LRUCache::Erase(absl::string_view context_id,
                                absl::string_view key) {
  if (context_id.empty()) {
    return false;
  }
  absl::MutexLock lock(&mutex_);
  auto it = contexts_.find(context_id);
  if (it == contexts_.end()) {
    return false;
  }
  bool erased = it->second.values.erase(key) > 0;
  if (it->second.values.empty()) {
    lru_contexts_.erase(it->second.lru_it);
    contexts_.erase(it);
  }
  return erased;
}

SkillData::SkillData(size_t max_contexts) : cache_(max_contexts) {}

bool SkillData::Delete(absl::string_view context_id) {
  if (context_id.empty()) {
    LOG(WARNING) << "SkillData: context_id is empty; delete skipped.";
    return false;
  }
  return cache_.Erase(context_id);
}

}  // namespace intrinsic::skills
