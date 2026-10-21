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

#ifndef INTRINSIC_MARSHAL_RIEGELI_PROTO_CODER_H_
#define INTRINSIC_MARSHAL_RIEGELI_PROTO_CODER_H_

#include <string>

#include "absl/log/check.h"  // IWYU pragma: keep
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "google/protobuf/message.h"
#include "intrinsic/marshal/riegeli_coder.h"      // IWYU pragma: keep
#include "intrinsic/util/status/status_macros.h"  // IWYU pragma: keep
#include "riegeli/messages/serialize_message.h"   // IWYU pragma: keep
#include "riegeli/records/record_reader.h"        // IWYU pragma: keep
#include "riegeli/records/record_writer.h"        // IWYU pragma: keep

namespace intrinsic {
namespace coder_details {
// Helper function to use deterministic serialization. This is necessary because
// we sometimes use the serialized proto as a key to some other data (e.g.,
// memoization keys computed based off the fingerprint of a proto). There is a
// bug (b/36237305) that disables determinism for any message inside of an Any.
inline std::string DeterministicSerialize(
    const google::protobuf::Message& message) {
  std::string serialized;
  {
    google::protobuf::io::StringOutputStream sos(&serialized);
    google::protobuf::io::CodedOutputStream cos(&sos);
    cos.SetSerializationDeterministic(true);
    static_cast<void>(
        message.SerializeToCodedStream(&cos));  // TODO: Handle return value
  }
  return serialized;
}

}  // namespace coder_details
}  // namespace intrinsic

// Registers RiegeliCoder specialization for ClassType
// using RiegeliCoder<std::string> to write/read the proto.
//
// The `EncodeFunc` and `DecodeFunc` specializations convert between
// ClassType and ProtoType.
// The `TypeName` specialization demangles proto type.
#define REGISTER_RIEGELI_PROTO_CODER_EXPLICIT(ClassType, ProtoType,            \
                                              EncodeFunc, DecodeFunc)          \
  template <>                                                                  \
  inline std::string intrinsic::RiegeliCoder<ClassType>::TypeName() {          \
    return Demangle<ProtoType>();                                              \
  }                                                                            \
  template <>                                                                  \
  inline absl::Status intrinsic::RiegeliCoder<ClassType>::Encode(              \
      const ClassType& value, riegeli::RecordWriterBase& writer) {             \
    INTR_ASSIGN_OR_RETURN(auto struct_proto, EncodeFunc(value));               \
    std::string serialized =                                                   \
        intrinsic::coder_details::DeterministicSerialize(struct_proto);        \
    return intrinsic::RiegeliCoder<std::string>::Encode(serialized, writer);   \
  }                                                                            \
  template <>                                                                  \
  inline absl::StatusOr<ClassType> intrinsic::RiegeliCoder<ClassType>::Decode( \
      riegeli::RecordReaderBase& reader) {                                     \
    INTR_ASSIGN_OR_RETURN(                                                     \
        std::string decoded,                                                   \
        intrinsic::RiegeliCoder<std::string>::Decode(reader));                 \
    ProtoType proto;                                                           \
    if (!proto.ParseFromString(decoded)) {                                     \
      return absl::InternalError("Failed to parse proto");                     \
    }                                                                          \
    return DecodeFunc(proto);                                                  \
  }

#endif  // INTRINSIC_MARSHAL_RIEGELI_PROTO_CODER_H_
