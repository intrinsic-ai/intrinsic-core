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

#include "intrinsic/util/grpc/channel_cache.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpc/grpc.h"
#include "grpcpp/channel.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_macros.h"
#include "third_party/simple_lru_cache/simple_lru_cache_inl.h"

namespace intrinsic {

struct ChannelCache::Impl {
  using Cache = google::simple_lru_cache::SimpleLRUCache<
      ConnectionParams, std::shared_ptr<Channel>, absl::Hash<ConnectionParams>>;

  Impl(size_t max_entries, absl::Duration max_idle_duration)
      : max_entries(max_entries),
        cache(static_cast<int64_t>(std::max<uint64_t>(
            1, std::min<uint64_t>(max_entries,
                                  std::numeric_limits<int64_t>::max())))) {
    if (max_idle_duration < absl::InfiniteDuration() &&
        max_idle_duration >= absl::ZeroDuration()) {
      cache.setMaxIdleSeconds(absl::ToDoubleSeconds(max_idle_duration));
    }
  }

  ~Impl() {
    absl::MutexLock lock(mutex);
    cache.clear();
  }

  std::shared_ptr<Channel> LookupHealthyLocked(
      const ConnectionParams& connection_params)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    {
      Cache::ScopedLookup lookup(&cache, connection_params);
      if (!lookup.found()) {
        return nullptr;
      }
      std::shared_ptr<Channel> existing = *lookup.value();
      if (existing->GetChannel()->GetState(/*try_to_connect=*/false) !=
          GRPC_CHANNEL_SHUTDOWN) {
        return existing;
      }
    }
    cache.remove(connection_params);
    return nullptr;
  }

  const size_t max_entries;
  mutable absl::Mutex mutex;
  mutable Cache cache ABSL_GUARDED_BY(mutex);
};

ChannelCache::ChannelCache(size_t max_entries, absl::Duration max_idle_duration)
    : impl_(std::make_unique<Impl>(max_entries, max_idle_duration)) {}

ChannelCache::~ChannelCache() = default;

absl::StatusOr<std::shared_ptr<Channel>> ChannelCache::Get(
    const ConnectionParams& connection_params, absl::Duration timeout) {
  if (impl_->max_entries == 0) {
    return Channel::MakeFromAddress(connection_params, timeout);
  }
  {
    absl::MutexLock lock(impl_->mutex);
    if (std::shared_ptr<Channel> existing =
            impl_->LookupHealthyLocked(connection_params);
        existing != nullptr) {
      return existing;
    }
  }

  INTR_ASSIGN_OR_RETURN(std::shared_ptr<Channel> channel,
                        Channel::MakeFromAddress(connection_params, timeout));

  absl::MutexLock lock(impl_->mutex);
  if (std::shared_ptr<Channel> existing =
          impl_->LookupHealthyLocked(connection_params);
      existing != nullptr) {
    return existing;
  }
  impl_->cache.insert(connection_params, new std::shared_ptr<Channel>(channel),
                      /*units=*/1);
  return channel;
}

void ChannelCache::Remove(const ConnectionParams& connection_params) {
  if (impl_->max_entries == 0) {
    return;
  }
  absl::MutexLock lock(impl_->mutex);
  impl_->cache.remove(connection_params);
}

size_t ChannelCache::Size() const {
  if (impl_->max_entries == 0) {
    return 0;
  }
  absl::MutexLock lock(impl_->mutex);
  impl_->cache.removeExpiredEntries();
  return static_cast<size_t>(impl_->cache.entries());
}

}  // namespace intrinsic
