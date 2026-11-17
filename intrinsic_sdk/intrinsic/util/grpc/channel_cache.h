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

#ifndef INTRINSIC_UTIL_GRPC_CHANNEL_CACHE_H_
#define INTRINSIC_UTIL_GRPC_CHANNEL_CACHE_H_

#include <cstddef>
#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/connection_params.h"

namespace intrinsic {

// A thread-safe bounded LRU cache of gRPC `Channel`s keyed by
// `ConnectionParams`.
//
// Underlying HTTP/2 connections (`Channel`) are shared across callers and
// service stubs, while typed stubs (`Service::NewStub(channel->GetChannel())`)
// are lightweight wrappers created per call or client. Evicted or removed
// channels remain valid via `std::shared_ptr<Channel>` until all active callers
// release their references.
class ChannelCache {
 public:
  static constexpr size_t kDefaultMaxEntries = 64;
  static constexpr absl::Duration kDefaultMaxIdleDuration = absl::Minutes(5);

  // Constructs a cache holding at most `max_entries` channels and lazily
  // evicting idle entries after `max_idle_duration` on cache operations.
  // Passing `absl::InfiniteDuration()` (or a negative duration) disables idle
  // expiration; `max_entries == 0` disables caching.
  explicit ChannelCache(
      size_t max_entries = kDefaultMaxEntries,
      absl::Duration max_idle_duration = kDefaultMaxIdleDuration);

  ChannelCache(const ChannelCache&) = delete;
  ChannelCache& operator=(const ChannelCache&) = delete;
  ChannelCache(ChannelCache&&) = delete;
  ChannelCache& operator=(ChannelCache&&) = delete;

  ~ChannelCache();

  // Returns a cached `Channel` for `connection_params`, or dials and caches a
  // new one if absent, idle-expired, or in `GRPC_CHANNEL_SHUTDOWN` state.
  // `timeout` applies only when dialing a new channel; failed dials are not
  // cached.
  absl::StatusOr<std::shared_ptr<Channel>> Get(
      const ConnectionParams& connection_params,
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

  // Removes any cached entry for `connection_params`. Active `shared_ptr`
  // references already returned by `Get()` remain valid.
  void Remove(const ConnectionParams& connection_params);

  // Purges idle-expired entries and returns the current number of entries in
  // the cache.
  size_t Size() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_CHANNEL_CACHE_H_
