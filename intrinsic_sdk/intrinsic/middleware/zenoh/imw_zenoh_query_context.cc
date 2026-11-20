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

#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_query_context.h"

#ifdef __linux__
#include <pthread.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace intrinsic {
namespace {

bool HasOtherThread(const std::vector<std::thread::id>& threads,
                    std::thread::id self_tid) {
  return std::any_of(
      threads.begin(), threads.end(),
      [self_tid](std::thread::id tid) { return tid != self_tid; });
}

void RemoveOneThread(std::vector<std::thread::id>* threads,
                     std::thread::id self_tid) {
  auto it = std::find(threads->begin(), threads->end(), self_tid);
  if (it != threads->end()) {
    threads->erase(it);
  }
}

}  // namespace

IMWZenohActiveQuery::IMWZenohActiveQuery(uint64_t id, absl::Time gc_deadline,
                                         const IMWZenohQueryParams& params)
    : id_(id),
      gc_deadline_(gc_deadline),
      keyexpr_(params.keyexpr != nullptr ? params.keyexpr : ""),
      callback_(params.callback),
      message_callback_(params.message_callback),
      on_done_(params.on_done),
      user_context_(params.user_context),
      options_(params.options) {}

IMWZenohActiveQuery::~IMWZenohActiveQuery() {
  if (options_.user_context_deleter != nullptr) {
    options_.user_context_deleter(user_context_);
  }
}

bool IMWZenohActiveQuery::TryBeginCallback() {
  absl::MutexLock lock(&mutex_);
  if (state_ == State::kClosing || state_ == State::kClosed ||
      on_done_pending_) {
    return false;
  }
  active_threads_.push_back(std::this_thread::get_id());
  return true;
}

void IMWZenohActiveQuery::EndCallback() {
  absl::MutexLock lock(&mutex_);
  RemoveOneThread(&active_threads_, std::this_thread::get_id());
  if (state_ == State::kClosing) {
    cv_.SignalAll();
  }
}

bool IMWZenohActiveQuery::DrainAndMarkClosedLocked(std::thread::id self_tid) {
  state_ = State::kClosing;
  while (HasOtherThread(active_threads_, self_tid)) {
    cv_.Wait(&mutex_);
  }
  state_ = State::kClosed;
  return !suppress_on_done_ && on_done_ != nullptr;
}

void IMWZenohActiveQuery::CloseAndNotify() {
  const std::thread::id self_tid = std::this_thread::get_id();
  bool should_notify = false;
  {
    absl::MutexLock lock(&mutex_);
    if (state_ == State::kDispatching) {
      on_done_pending_ = true;
      return;
    }
    if (state_ == State::kClosing || state_ == State::kClosed) {
      return;
    }
    should_notify = DrainAndMarkClosedLocked(self_tid);
  }
  if (should_notify) {
    on_done_(keyexpr_.c_str(), user_context_);
  }
}

void IMWZenohActiveQuery::FinishZGet(bool z_get_ok) {
  const std::thread::id self_tid = std::this_thread::get_id();
  bool should_notify = false;
  {
    absl::MutexLock lock(&mutex_);
    if (!z_get_ok) {
      suppress_on_done_ = true;
      if (state_ != State::kClosed) {
        DrainAndMarkClosedLocked(self_tid);
      }
      return;
    }
    if (state_ != State::kDispatching) {
      return;
    }
    if (!on_done_pending_) {
      state_ = State::kActive;
      return;
    }
    should_notify = DrainAndMarkClosedLocked(self_tid);
  }
  if (should_notify) {
    on_done_(keyexpr_.c_str(), user_context_);
  }
}

IMWZenohQueryTable::~IMWZenohQueryTable() {
  BeginShutdownAndJoinReaper();
  DrainRemaining();
}

void IMWZenohQueryTable::StartReaperLocked() {
  if (reaper_started_) {
    return;
  }
  reaper_started_ = true;
  reaper_thread_ = std::thread(&IMWZenohQueryTable::ReaperLoop, this);
#ifdef __linux__
  if (int errnum =
          pthread_setname_np(reaper_thread_.native_handle(), "imw_query_reap");
      errnum != 0) {
    LOG(WARNING) << "Failed to set query reaper thread name: "
                 << std::strerror(errnum);
  }
#endif
}

std::shared_ptr<IMWZenohActiveQuery> IMWZenohQueryTable::Insert(
    const IMWZenohQueryParams& params, absl::Time gc_deadline) {
  absl::MutexLock lock(&mutex_);
  if (shutting_down_) {
    return nullptr;
  }
  const uint64_t id = ++next_id_;
  auto entry = std::make_shared<IMWZenohActiveQuery>(id, gc_deadline, params);
  entries_.emplace(id, entry);
  if (gc_deadline == absl::InfiniteFuture()) {
    return entry;
  }
  expiry_index_.emplace(gc_deadline, id);
  StartReaperLocked();
  if (expiry_index_.begin()->second == id) {
    reaper_cv_.Signal();
  }
  return entry;
}

std::shared_ptr<IMWZenohActiveQuery> IMWZenohQueryTable::Take(uint64_t id) {
  absl::MutexLock lock(&mutex_);
  auto it = entries_.find(id);
  if (it == entries_.end()) {
    return nullptr;
  }
  std::shared_ptr<IMWZenohActiveQuery> entry = std::move(it->second);
  entries_.erase(it);
  if (entry->gc_deadline() < absl::InfiniteFuture()) {
    expiry_index_.erase({entry->gc_deadline(), id});
  }
  return entry;
}

std::vector<std::shared_ptr<IMWZenohActiveQuery>>
IMWZenohQueryTable::ExtractExpiredLocked(absl::Time now) {
  std::vector<std::shared_ptr<IMWZenohActiveQuery>> expired;
  auto end_it = expiry_index_.begin();
  while (end_it != expiry_index_.end() && end_it->first <= now) {
    auto entry_it = entries_.find(end_it->second);
    if (entry_it != entries_.end()) {
      expired.push_back(std::move(entry_it->second));
      entries_.erase(entry_it);
    }
    ++end_it;
  }
  expiry_index_.erase(expiry_index_.begin(), end_it);
  return expired;
}

void IMWZenohQueryTable::CloseEntriesUnlocked(
    std::vector<std::shared_ptr<IMWZenohActiveQuery>> entries,
    bool log_reaped) {
  for (auto& entry : entries) {
    if (log_reaped) {
      LOG(WARNING) << "Reaping expired IMWZenoh query id=" << entry->id()
                   << " keyexpr=" << entry->keyexpr();
    }
    entry->CloseAndNotify();
    entry.reset();
  }
}

size_t IMWZenohQueryTable::ReapExpired(absl::Time now) {
  std::vector<std::shared_ptr<IMWZenohActiveQuery>> expired;
  {
    absl::MutexLock lock(&mutex_);
    expired = ExtractExpiredLocked(now);
  }
  const size_t count = expired.size();
  CloseEntriesUnlocked(std::move(expired), /*log_reaped=*/true);
  return count;
}

bool IMWZenohQueryTable::IsShuttingDown() const {
  absl::MutexLock lock(&mutex_);
  return shutting_down_;
}

void IMWZenohQueryTable::ReaperLoop() {
  while (true) {
    std::vector<std::shared_ptr<IMWZenohActiveQuery>> expired;
    std::shared_ptr<IMWZenohQueryTable> keep_alive;
    {
      absl::MutexLock lock(&mutex_);
      while (!shutting_down_) {
        if (expiry_index_.empty()) {
          reaper_cv_.Wait(&mutex_);
          continue;
        }
        const absl::Time now = absl::Now();
        if (expiry_index_.begin()->first <= now) {
          expired = ExtractExpiredLocked(now);
          break;
        }
        reaper_cv_.WaitWithDeadline(&mutex_, expiry_index_.begin()->first);
      }
      if (shutting_down_ && expired.empty()) {
        break;
      }
      // Keep the table alive across callbacks in case a reaped `on_done` or
      // deleter triggers `destroy_session()` from the reaper thread.
      keep_alive = weak_from_this().lock();
    }
    CloseEntriesUnlocked(std::move(expired), /*log_reaped=*/true);
    if (keep_alive == nullptr || keep_alive->IsShuttingDown()) {
      break;
    }
  }
}

void IMWZenohQueryTable::BeginShutdownAndJoinReaper() {
  std::thread thread_to_join;
  {
    absl::MutexLock lock(&mutex_);
    shutting_down_ = true;
    reaper_cv_.SignalAll();
    thread_to_join = std::move(reaper_thread_);
  }
  if (!thread_to_join.joinable()) {
    return;
  }
  if (thread_to_join.get_id() == std::this_thread::get_id()) {
    thread_to_join.detach();
  } else {
    thread_to_join.join();
  }
}

void IMWZenohQueryTable::DrainRemaining() {
  std::vector<std::shared_ptr<IMWZenohActiveQuery>> remaining;
  {
    absl::MutexLock lock(&mutex_);
    remaining.reserve(entries_.size());
    for (auto& [id, entry] : entries_) {
      remaining.push_back(std::move(entry));
    }
    entries_.clear();
    expiry_index_.clear();
  }
  CloseEntriesUnlocked(std::move(remaining), /*log_reaped=*/false);
}

size_t IMWZenohQueryTable::ActiveCount() const {
  absl::MutexLock lock(&mutex_);
  return entries_.size();
}

}  // namespace intrinsic
