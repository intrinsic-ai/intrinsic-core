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

#include "intrinsic/util/grpc/connection_params.h"

#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_format.h"

namespace intrinsic {

// static
ConnectionParams ConnectionParams::ResourceInstance(
    std::string_view instance_name) {
  return ConnectionParams::ResourceInstance(
      /*instance_name=*/instance_name,
      /*address=*/"istio-ingressgateway.app-ingress.svc.cluster.local:80");
}

// static
ConnectionParams ConnectionParams::ResourceInstance(
    std::string_view instance_name, std::string_view address) {
  return {
      .address = std::string(address),
      .instance_name = std::string(instance_name),
      .header = "x-resource-instance-name",
  };
}

// static
ConnectionParams ConnectionParams::NoIngress(std::string_view address) {
  return {
      .address = std::string(address),
      .instance_name = "",
      .header = "",
  };
}

// static
ConnectionParams ConnectionParams::LocalPort(int port) {
  return NoIngress(absl::StrFormat("localhost:%d", port));
}

std::vector<std::pair<std::string, std::string>> ConnectionParams::Metadata()
    const {
  if (header.empty() || instance_name.empty()) {
    return {};
  }
  return std::vector<std::pair<std::string, std::string>>{
      {header, instance_name}};
}

std::ostream& operator<<(std::ostream& os, const ConnectionParams& p) {
  if (p.instance_name.empty()) {
    return os << p.address;
  }
  return os << absl::StreamFormat("%s (%s=%s)", p.address, p.header,
                                  p.instance_name);
}

}  // namespace intrinsic
