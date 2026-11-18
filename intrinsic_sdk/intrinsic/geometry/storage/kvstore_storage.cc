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

#include "intrinsic/geometry/storage/kvstore_storage.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/storage/geometry_storage_uri.h"
#include "intrinsic/util/hash.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {
namespace {

using ::google::protobuf::Any;
using ::google::protobuf::BytesValue;
using ::intrinsic_proto::geometry::v1::GeometryStorageRefs;
using ::intrinsic_proto::geometry::v1::MaterialProperties;
using ExactGeometryProto = ::intrinsic_proto::geometry::v1::ExactGeometry;

constexpr absl::string_view kExactKeyPrefix = "geo_v1_exact_";
constexpr absl::string_view kRenderableKeyPrefix = "geo_glb_";
constexpr absl::string_view kForbiddenKvStoreKeyChars{"/*?#[]$\0", 8};


using GeometryStorageRefsV0 = ::intrinsic_proto::geometry::GeometryStorageRefs;

constexpr absl::string_view kV0UnsupportedMessage =
    "KVStore GeometryLibrary only supports V1 GeometryStorageRefs.";


// Returns true if `segment` is non-empty and contains no Zenoh-forbidden
// characters (`/*?#[]$` or NUL).
constexpr bool IsValidKvStoreSegment(absl::string_view segment) {
  return !segment.empty() && segment.find_first_of(kForbiddenKvStoreKeyChars) ==
                                 absl::string_view::npos;
}

// Validates that `uri` is "intkv://<storage_prefix>/<key>" where
// `<storage_prefix>` matches `storage_prefix` and `<key>` is non-empty with no
// Zenoh-forbidden characters (`/*?#[]$` or NUL), and returns `<key>`.
absl::StatusOr<absl::string_view> ExtractAndValidateKvStoreKey(
    absl::string_view uri, absl::string_view storage_prefix) {
  const std::optional<absl::string_view> key =
      ParseKvStoreGeometryKey(uri, storage_prefix);
  if (!key.has_value() || !IsValidKvStoreSegment(*key)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid KVStore URI: ", uri));
  }
  return *key;
}

// Validates that caller-specified custom key `key` for `field` is non-empty and
// contains no Zenoh-forbidden characters (`/*?#[]$` or NUL).
absl::Status ValidateCustomKvStoreKey(absl::string_view key,
                                      absl::string_view field) {
  if (key.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Custom ", field, " must not be empty."));
  }
  if (!IsValidKvStoreSegment(key)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Custom ", field, " contains invalid character: ", key));
  }
  return absl::OkStatus();
}

// Returns "<key_prefix><16-hex-fingerprint>" computed from `payload`.
std::string ContentAddressedKvStoreKey(absl::string_view key_prefix,
                                       absl::string_view payload) {
  return absl::StrCat(key_prefix,
                      absl::Hex(Fingerprint(payload), absl::kZeroPad16));
}

// Returns OK if `key` does not exist in `kv_store`, ALREADY_EXISTS if present,
// or the error status from KeyValueStore::Get.
absl::Status EnsureKvStoreKeyAbsent(KeyValueStore& kv_store,
                                    absl::string_view key,
                                    absl::Duration timeout) {
  const absl::Status status = kv_store.Get<Any>(key, timeout).status();
  if (status.ok()) {
    return absl::AlreadyExistsError(
        absl::StrCat("KeyValueStore already contains key: ", key));
  }
  if (absl::IsNotFound(status)) {
    return absl::OkStatus();
  }
  return status;
}

// Writes `payload` as `BytesValue` to `kv_store` at `key` and returns its
// `"intkv://<storage_prefix>/<key>"` URI.
absl::StatusOr<std::string> WriteBlobAtKvStoreKey(
    KeyValueStore& kv_store, absl::string_view key, std::string payload,
    const KeyValueStore::SetWithVerificationOptions& write_options) {
  BytesValue bytes;
  bytes.set_value(std::move(payload));
  INTR_RETURN_IF_ERROR(kv_store.SetWithVerification(key, bytes, write_options));
  return FormatKvStoreGeometryUri(kv_store.key_prefix(), key);
}

// Reads the `BytesValue` at `key` from `kv_store` within `timeout`.
absl::StatusOr<std::string> ReadBlobFromKvStore(KeyValueStore& kv_store,
                                                absl::string_view key,
                                                absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(
      BytesValue bytes, kv_store.Get<BytesValue>(key, timeout),
      _ << "Failed to get geometry blob from KeyValueStore for key: " << key);
  return std::move(*bytes.mutable_value());
}

// Replaces `geo`'s material with `material`, or returns `geo` if no override is
// provided.
Geometry ApplyMaterialProperties(Geometry geo,
                                 std::optional<MaterialProperties> material) {
  if (material.has_value()) {
    return Geometry(geo.GetExactGeometry(), geo.GetRenderable(),
                    geo.KeepRenderableForSerialization(), std::move(material));
  }
  return geo;
}

class KvStoreGeometryLibrary : public GeometryLibrary,
                               private GeometryDeserializer,
                               private GeometrySerializer {
 public:
  KvStoreGeometryLibrary(
      absl_nonnull std::shared_ptr<KeyValueStore> kv_store,
      absl::Duration read_timeout,
      KeyValueStore::SetWithVerificationOptions write_options)
      : kv_store_(ABSL_DIE_IF_NULL(std::move(kv_store))),
        read_timeout_(read_timeout),
        write_options_(std::move(write_options)) {}

  const GeometryDeserializer& Deserializer() const override { return *this; }
  GeometrySerializer& Serializer() override { return *this; }


  absl::StatusOr<Geometry> GetGeometry(const GeometryStorageRefsV0&,
                                       const GeometryOptions&) const override {
    return absl::UnimplementedError(kV0UnsupportedMessage);
  }

  absl::StatusOr<GeometryStorageRefsV0> SaveGeometry(const Geometry&) override {
    return absl::UnimplementedError(kV0UnsupportedMessage);
  }


  absl::StatusOr<Geometry> GetGeometry(
      const GeometryStorageRefs& refs,
      std::optional<MaterialProperties> material) const override {
    const absl::string_view storage_prefix = kv_store_->key_prefix();
    INTR_ASSIGN_OR_RETURN(
        const absl::string_view exact_key,
        ExtractAndValidateKvStoreKey(refs.exact_geometry_ref(), storage_prefix),
        _.SetPrepend() << "exact_geometry_ref: ");
    std::optional<absl::string_view> glb_key;
    if (refs.keep_renderable() && !refs.renderable_ref().empty()) {
      INTR_ASSIGN_OR_RETURN(
          const absl::string_view validated_glb_key,
          ExtractAndValidateKvStoreKey(refs.renderable_ref(), storage_prefix),
          _.SetPrepend() << "renderable_ref: ");
      glb_key = validated_glb_key;
    }

    INTR_ASSIGN_OR_RETURN(
        const std::string bytes,
        ReadBlobFromKvStore(*kv_store_, exact_key, read_timeout_));
    ExactGeometryProto proto;
    if (!proto.ParseFromString(bytes)) {
      return absl::InternalError(absl::StrCat(
          "Failed to parse ExactGeometry proto for key: ", exact_key));
    }
    INTR_ASSIGN_OR_RETURN(ExactGeometry exact_geo, ToGeometry(proto));

    std::shared_ptr<const Renderable> renderable;
    if (glb_key.has_value()) {
      INTR_ASSIGN_OR_RETURN(
          std::string glb,
          ReadBlobFromKvStore(*kv_store_, *glb_key, read_timeout_));
      renderable = std::make_shared<Renderable>(std::move(glb));
    }

    return ApplyMaterialProperties(
        Geometry(std::move(exact_geo), std::move(renderable),
                 refs.keep_renderable(), std::nullopt),
        std::move(material));
  }

  absl::StatusOr<GeometryStorageRefs> SaveGeometryV1(
      const Geometry& geometry, const SaveGeometryOptions& options) override {
    const bool keep_renderable = geometry.KeepRenderableForSerialization();
    if (options.exact_key.has_value()) {
      INTR_RETURN_IF_ERROR(
          ValidateCustomKvStoreKey(*options.exact_key, "exact_key"));
    }
    if (options.renderable_key.has_value()) {
      if (!keep_renderable) {
        return absl::InvalidArgumentError(
            "renderable_key cannot be specified when "
            "KeepRenderableForSerialization() is false.");
      }
      INTR_RETURN_IF_ERROR(
          ValidateCustomKvStoreKey(*options.renderable_key, "renderable_key"));
      if (options.exact_key == options.renderable_key) {
        return absl::InvalidArgumentError(
            absl::StrCat("exact_key and renderable_key must not be identical: ",
                         *options.exact_key));
      }
    }
    if (!options.overwrite_if_exists) {
      if (options.exact_key.has_value()) {
        INTR_RETURN_IF_ERROR(EnsureKvStoreKeyAbsent(
            *kv_store_, *options.exact_key, read_timeout_));
      }
      if (options.renderable_key.has_value()) {
        INTR_RETURN_IF_ERROR(EnsureKvStoreKeyAbsent(
            *kv_store_, *options.renderable_key, read_timeout_));
      }
    }

    INTR_ASSIGN_OR_RETURN(const ExactGeometryProto proto,
                          ToProto(geometry.GetExactGeometry()));
    std::string exact_bytes = proto.SerializeAsString();
    const std::string exact_key =
        options.exact_key.has_value()
            ? *options.exact_key
            : ContentAddressedKvStoreKey(kExactKeyPrefix, exact_bytes);

    GeometryStorageRefs refs;
    refs.set_keep_renderable(keep_renderable);
    // TODO(b/570601001): All KVStore refs are marked `MUTABILITY_MUTABLE` for
    // now because `ValidateCustomKvStoreKey` does not yet prevent custom keys
    // from overwriting content-addressed keys (`geo_v1_exact_*` and
    // `geo_glb_*`). Reserving those prefixes in `ValidateCustomKvStoreKey` will
    // allow `!options.HasCustomKeys()` writes to return `MUTABILITY_IMMUTABLE`
    // in O(1) without extra KVStore metadata reads.
    refs.set_mutability(GeometryStorageRefs::MUTABILITY_MUTABLE);
    if (keep_renderable) {
      INTR_ASSIGN_OR_RETURN(const std::shared_ptr<const Renderable> renderable,
                            GetOrGenerateRenderable(geometry));
      if (renderable == nullptr) {
        return absl::InternalError("Generated renderable is null.");
      }
      std::string glb_bytes = renderable->GetGLBString();
      const std::string renderable_key =
          options.renderable_key.has_value()
              ? *options.renderable_key
              : ContentAddressedKvStoreKey(kRenderableKeyPrefix, glb_bytes);
      INTR_ASSIGN_OR_RETURN(
          std::string renderable_uri,
          WriteBlobAtKvStoreKey(*kv_store_, renderable_key,
                                std::move(glb_bytes), write_options_));
      refs.set_renderable_ref(std::move(renderable_uri));
    }

    INTR_ASSIGN_OR_RETURN(
        std::string exact_uri,
        WriteBlobAtKvStoreKey(*kv_store_, exact_key, std::move(exact_bytes),
                              write_options_));
    refs.set_exact_geometry_ref(std::move(exact_uri));
    return refs;
  }

 private:
  const absl_nonnull std::shared_ptr<KeyValueStore> kv_store_;
  const absl::Duration read_timeout_;
  const KeyValueStore::SetWithVerificationOptions write_options_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<GeometryLibrary>> GetKvStoreGeometryLibrary(
    std::shared_ptr<KeyValueStore> kv_store, absl::Duration read_timeout,
    KeyValueStore::SetWithVerificationOptions write_options) {
  if (kv_store == nullptr) {
    return absl::InvalidArgumentError("kv_store must not be null.");
  }
  if (!IsValidKvStoreSegment(kv_store->key_prefix())) {
    return absl::InvalidArgumentError(absl::StrCat(
        "kv_store->key_prefix() is invalid: ", kv_store->key_prefix()));
  }
  if (read_timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError("read_timeout must be positive.");
  }
  if (write_options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError("write_options.timeout must be > 0.");
  }

  return std::make_unique<KvStoreGeometryLibrary>(
      std::move(kv_store), read_timeout, std::move(write_options));
}

}  // namespace intrinsic::geo
