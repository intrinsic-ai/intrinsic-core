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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"

namespace intrinsic {

class IMWZenoh;

class IMWZenohQueryContext {
 public:
  IMWZenohQueryContext(IMWZenoh* imw_zenoh_instance, const char* keyexpr,
                       imw_query_callback_fn* callback,
                       imw_query_message_callback_fn* message_callback,
                       imw_query_on_done_callback_fn* on_done,
                       void* user_context, imw_query_options_t* options)
      : imw_zenoh_instance_(imw_zenoh_instance),
        keyexpr_(keyexpr != nullptr ? keyexpr : ""),
        callback_(callback),
        message_callback_(message_callback),
        on_done_(on_done),
        user_context_(user_context) {
    if (options != nullptr) {
      options_ = *options;
    }
  }

  IMWZenoh* imw_zenoh_instance_;
  std::string keyexpr_;
  imw_query_callback_fn* callback_;
  imw_query_message_callback_fn* message_callback_;
  imw_query_on_done_callback_fn* on_done_;
  void* user_context_;
  imw_query_options_t options_;
};

struct IMWZenohQueryParams {
  const char* keyexpr = nullptr;
  imw_query_callback_fn* callback = nullptr;
  imw_query_message_callback_fn* message_callback = nullptr;
  imw_query_on_done_callback_fn* on_done = nullptr;
  void* user_context = nullptr;
  imw_query_options_t options;
};

// Tracks a single in-flight IMWZenoh query and enforces its lifecycle:
//   - Reply callbacks run unlocked and never execute after teardown starts.
//   - Teardown drains any in-flight reply callbacks on other threads before
//     invoking `on_done` once (or suppressing `on_done` if `z_get` failed).
//   - The destructor invokes `options_.user_context_deleter` once after all
//     callbacks and `on_done` return (when the final `shared_ptr` drops, so
//     the deleter never runs while another thread holds the query).
class IMWZenohActiveQuery {
 public:
  IMWZenohActiveQuery(uint64_t id, absl::Time gc_deadline,
                      const IMWZenohQueryParams& params);

  IMWZenohActiveQuery(const IMWZenohActiveQuery&) = delete;
  IMWZenohActiveQuery& operator=(const IMWZenohActiveQuery&) = delete;

  ~IMWZenohActiveQuery();

  uint64_t id() const { return id_; }
  absl::Time gc_deadline() const { return gc_deadline_; }
  const std::string& keyexpr() const { return keyexpr_; }
  imw_query_callback_fn* callback() const { return callback_; }
  imw_query_message_callback_fn* message_callback() const {
    return message_callback_;
  }
  void* user_context() const { return user_context_; }
  const imw_query_options_t& options() const { return options_; }

  // Registers the current thread as executing a reply callback. Returns false
  // if teardown has already begun so late replies become safe no-ops.
  bool TryBeginCallback() ABSL_LOCKS_EXCLUDED(mutex_);
  void EndCallback() ABSL_LOCKS_EXCLUDED(mutex_);

  // Claims teardown from Zenoh's drop callback, the reaper thread, or session
  // shutdown. Defers `on_done` to `FinishZGet` while `z_get` is still running;
  // otherwise drains active other-thread callbacks and invokes `on_done` once.
  void CloseAndNotify() ABSL_LOCKS_EXCLUDED(mutex_);

  // Completes `z_get` dispatch. Suppresses `on_done` when `z_get_ok` is false,
  // or delivers a deferred synchronous `on_done` when `z_get_ok` is true.
  void FinishZGet(bool z_get_ok) ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  enum class State {
    kDispatching,
    kActive,
    kClosing,
    kClosed,
  };

  bool DrainAndMarkClosedLocked(std::thread::id self_tid)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const uint64_t id_;
  const absl::Time gc_deadline_;
  const std::string keyexpr_;
  imw_query_callback_fn* const callback_;
  imw_query_message_callback_fn* const message_callback_;
  imw_query_on_done_callback_fn* const on_done_;
  void* const user_context_;
  const imw_query_options_t options_;

  absl::Mutex mutex_;  // leaf lock; never held across user callbacks or table
  absl::CondVar cv_;
  State state_ ABSL_GUARDED_BY(mutex_) = State::kDispatching;
  std::vector<std::thread::id> active_threads_ ABSL_GUARDED_BY(mutex_);
  bool on_done_pending_ ABSL_GUARDED_BY(mutex_) = false;
  bool suppress_on_done_ ABSL_GUARDED_BY(mutex_) = false;
};

// Owns all active queries for an IMWZenoh session and reaps overdue entries on
// a lazily-started background thread sleeping until the earliest deadline.
class IMWZenohQueryTable
    : public std::enable_shared_from_this<IMWZenohQueryTable> {
 private:
  struct Passkey {};

 public:
  explicit IMWZenohQueryTable(Passkey) {}
  static std::shared_ptr<IMWZenohQueryTable> Create() {
    return std::make_shared<IMWZenohQueryTable>(Passkey{});
  }

  IMWZenohQueryTable(const IMWZenohQueryTable&) = delete;
  IMWZenohQueryTable& operator=(const IMWZenohQueryTable&) = delete;
  ~IMWZenohQueryTable();

  std::shared_ptr<IMWZenohActiveQuery> Insert(const IMWZenohQueryParams& params,
                                              absl::Time gc_deadline)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Atomically removes and returns the entry for `id`, or nullptr if already
  // removed. Always drop the returned shared_ptr outside `mutex_`.
  std::shared_ptr<IMWZenohActiveQuery> Take(uint64_t id)
      ABSL_LOCKS_EXCLUDED(mutex_);

  // Reaps all entries whose `gc_deadline <= now`, invoking `CloseAndNotify`
  // and releasing them outside `mutex_`. Returns the number of reaped entries.
  size_t ReapExpired(absl::Time now) ABSL_LOCKS_EXCLUDED(mutex_);

  // Rejects further insertions and stops/joins the reaper thread.
  void BeginShutdownAndJoinReaper() ABSL_LOCKS_EXCLUDED(mutex_);

  // Removes and closes any entries still remaining after `z_drop(session_)`.
  void DrainRemaining() ABSL_LOCKS_EXCLUDED(mutex_);

  size_t ActiveCount() const ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  bool IsShuttingDown() const ABSL_LOCKS_EXCLUDED(mutex_);
  void StartReaperLocked() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  std::vector<std::shared_ptr<IMWZenohActiveQuery>> ExtractExpiredLocked(
      absl::Time now) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void ReaperLoop();
  static void CloseEntriesUnlocked(
      std::vector<std::shared_ptr<IMWZenohActiveQuery>> entries,
      bool log_reaped);

  mutable absl::Mutex mutex_;  // leaf lock; never held across query locks
  absl::CondVar reaper_cv_;
  bool shutting_down_ ABSL_GUARDED_BY(mutex_) = false;
  bool reaper_started_ ABSL_GUARDED_BY(mutex_) = false;
  uint64_t next_id_ ABSL_GUARDED_BY(mutex_) = 0;
  absl::flat_hash_map<uint64_t, std::shared_ptr<IMWZenohActiveQuery>> entries_
      ABSL_GUARDED_BY(mutex_);
  std::set<std::pair<absl::Time, uint64_t>> expiry_index_
      ABSL_GUARDED_BY(mutex_);
  std::thread reaper_thread_ ABSL_GUARDED_BY(mutex_);
};

// Closure context passed to Zenoh `z_closure_reply`. Owned exclusively by
// `IMWZenoh::static_query_drop` (never deleted on `z_get` error returns,
// because `Z_ESESSION_CLOSED` strands the closure until session teardown).
struct IMWZenohQueryClosureContext {
  std::weak_ptr<IMWZenohActiveQuery> query;
  std::weak_ptr<IMWZenohQueryTable> table;
  uint64_t query_id = 0;
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_
