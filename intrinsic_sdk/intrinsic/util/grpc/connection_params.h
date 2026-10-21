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

#ifndef INTRINSIC_UTIL_GRPC_CONNECTION_PARAMS_H_
#define INTRINSIC_UTIL_GRPC_CONNECTION_PARAMS_H_

#include <iosfwd>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace intrinsic {

struct ConnectionParams {
  // Constructs ConnectionParams to connect to a resource using the
  // cluster internal ingress.
  // This is the default when running within a cluster.
  static ConnectionParams ResourceInstance(std::string_view instance_name);

  // Constructs ConnectionParams to connect to a resource.
  // The parameter `address` allows using custom ingress endpoints.
  static ConnectionParams ResourceInstance(std::string_view instance_name,
                                           std::string_view address);

  // Constructs a ConnectionParams that is appropriately configured to work with
  // the no-ingress case, such as integration tests.
  static ConnectionParams NoIngress(std::string_view address);

  // Helper for connecting to a local instance of ICON on a specific port. This
  // primarily should be used for testing purposes.  It will not specify
  // information for ingress into a kubernetes cluster.
  static ConnectionParams LocalPort(int port);

  // The full address of the server "ip_address_or_hostname:port_number".
  std::string address;
  // The ingress instance name.  This determines which VirtualService in
  // kubernetes is targeted.  If empty, the header information is not added to
  // the gRPC connection.
  std::string instance_name;
  // The header to be used when establishing a gRPC connection to the ingress.
  // The header's value will be instance_name.
  std::string header;

  // Returns the metadata required by the connection to talk to the server, if
  // it is necessary.  Each pair represents the key, and value of the metadata,
  // respectively.
  std::vector<std::pair<std::string, std::string>> Metadata() const;

  friend bool operator==(const ConnectionParams& lhs,
                         const ConnectionParams& rhs) {
    return lhs.address == rhs.address &&
           lhs.instance_name == rhs.instance_name && lhs.header == rhs.header;
  }

  friend bool operator!=(const ConnectionParams& lhs,
                         const ConnectionParams& rhs) {
    return !(lhs == rhs);
  }

  template <typename H>
  friend H AbslHashValue(H h, const ConnectionParams& p) {
    return H::combine(std::move(h), p.address, p.instance_name, p.header);
  }

  friend std::ostream& operator<<(std::ostream& os, const ConnectionParams& p);
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_CONNECTION_PARAMS_H_
