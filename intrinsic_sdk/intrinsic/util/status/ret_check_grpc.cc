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

#include "intrinsic/util/status/ret_check_grpc.h"

#include <memory>
#include <string>

#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder_grpc.h"

namespace intrinsic {
namespace internal_status_macros_ret_check {

StatusBuilderGrpc RetCheckFailSlowPathGrpc(intrinsic::SourceLocation location) {
  return InternalErrorBuilderGrpc(location)
             .Log(absl::LogSeverity::kError)
             .EmitStackTrace()
         << "INTR_RET_CHECK failure (" << location.file_name() << ":"
         << location.line() << ") ";
}

StatusBuilderGrpc RetCheckFailSlowPathGrpc(intrinsic::SourceLocation location,
                                           std::string* condition) {
  std::unique_ptr<std::string> cleanup(condition);
  return RetCheckFailSlowPathGrpc(location) << *condition << " ";
}

StatusBuilderGrpc RetCheckFailSlowPathGrpc(intrinsic::SourceLocation location,
                                           const char* condition) {
  return RetCheckFailSlowPathGrpc(location) << condition << " ";
}

StatusBuilderGrpc RetCheckFailSlowPathGrpc(intrinsic::SourceLocation location,
                                           const char* condition,
                                           const absl::Status& status) {
  return RetCheckFailSlowPathGrpc(location)
         << condition << " returned " << status.ToString() << " ";
}

}  // namespace internal_status_macros_ret_check
}  // namespace intrinsic
