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

#include "intrinsic/assets/services/examples/calcserver/calc_server.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.grpc.pb.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.pb.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::services {

namespace {

char kCustomCalculationInterface[] =
    "grpc://intrinsic_proto.services.CustomCalculation";
char kConversionDatasetInterface[] =
    "data://intrinsic_proto.services.ConversionDataset";

// Retrieves the linear conversion parameters (factor and offset) for a given
// unit. Returns NotFoundError if the unit is not found (null), or
// UnimplementedError if the conversion type is not supported.
absl::StatusOr<std::pair<double, double>> GetLinearParams(
    const intrinsic_proto::services::Unit* unit, absl::string_view unit_name) {
  if (unit) {
    if (!unit->has_linear()) {
      return absl::UnimplementedError(
          absl::StrCat("unsupported conversion type for unit: ", unit_name));
    }
    return std::make_pair(unit->linear().factor(), unit->linear().offset());
  }
  return absl::NotFoundError(absl::StrCat("unit not found: ", unit_name));
}

}  // namespace

grpc::Status CalculatorServiceImpl::Calculate(
    grpc::ServerContext* context,
    const intrinsic_proto::services::CalculatorRequest* request,
    intrinsic_proto::services::CalculatorResponse* response) {
  int64_t result;

  int64_t a, b;
  if (config_.reverse_order()) {
    a = request->y();
    b = request->x();
  } else {
    a = request->x();
    b = request->y();
  }

  switch (request->operation()) {
    case intrinsic_proto::services::CALCULATOR_OPERATION_ADD:
      result = a + b;
      LOG(INFO) << a << " + " << b << " = " << result;
      break;
    case intrinsic_proto::services::CALCULATOR_OPERATION_MULTIPLY:
      result = a * b;
      LOG(INFO) << a << " * " << b << " = " << result;
      break;
    case intrinsic_proto::services::CALCULATOR_OPERATION_SUBTRACT:
      result = a - b;
      LOG(INFO) << a << " - " << b << " = " << result;
      break;
    case intrinsic_proto::services::CALCULATOR_OPERATION_DIVIDE:
      if (b == 0) {
        LOG(INFO) << "Cannot divide by 0 (" << a << " / " << b << ")";
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                            "Cannot divide by 0");
      }
      result = a / b;
      LOG(INFO) << a << " / " << b << " = " << result;
      break;
    case intrinsic_proto::services::CALCULATOR_OPERATION_CUSTOM: {
      // Connect to the CustomCalculation service.
      INTR_ASSIGN_OR_RETURN_GRPC(
          std::shared_ptr<grpc::Channel> channel,
          assets::dependencies::Connect(config_.custom_calculation(),
                                        kCustomCalculationInterface));
      auto stub =
          intrinsic_proto::services::CustomCalculation::NewStub(channel);

      // Create the request.
      intrinsic_proto::services::CustomCalculateRequest custom_request;
      custom_request.set_x(a);
      custom_request.set_y(b);

      // Call the CustomCalculation service.
      ::grpc::ClientContext ctx;
      intrinsic_proto::services::CalculatorResponse custom_response;
      INTR_RETURN_IF_ERROR_GRPC(
          stub->Calculate(&ctx, custom_request, &custom_response));
      result = custom_response.result();
      LOG(INFO) << "Custom(" << a << ", " << b << ") = " << result;

      break;
    }
    default:
      return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Invalid operation");
  }
  response->set_result(result);
  return grpc::Status::OK;
}

absl::StatusOr<intrinsic_proto::services::ConversionDataset>
CalculatorServiceImpl::GetConversionDataset() {
  if (!config_.has_conversion_dataset()) {
    return absl::FailedPreconditionError("conversion dataset not configured");
  }

  INTR_ASSIGN_OR_RETURN(auto payload, fetcher_(config_.conversion_dataset(),
                                               kConversionDatasetInterface));

  intrinsic_proto::services::ConversionDataset dataset;
  if (!payload.UnpackTo(&dataset)) {
    return absl::InternalError("failed to unmarshal dataset");
  }

  return dataset;
}

grpc::Status CalculatorServiceImpl::Convert(
    grpc::ServerContext* context,
    const intrinsic_proto::services::ConvertRequest* request,
    intrinsic_proto::services::ConvertResponse* response) {
  if (request->category().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "category not specified");
  }
  if (request->from_unit().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "from unit not specified");
  }
  if (request->to_unit().empty()) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "to unit not specified");
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      const intrinsic_proto::services::ConversionDataset& dataset,
      GetConversionDataset());

  // Find the requested category within the dataset.
  const intrinsic_proto::services::UnitCategory* target_category_ptr = nullptr;
  for (const intrinsic_proto::services::UnitCategory& category :
       dataset.categories()) {
    if (category.name() == request->category()) {
      target_category_ptr = &category;
      break;
    }
  }
  if (!target_category_ptr) {
    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                        "category not found: " + request->category());
  }

  // Check that the base unit is not redefined in the units list.
  for (const intrinsic_proto::services::Unit& unit :
       target_category_ptr->units()) {
    if (unit.name() == target_category_ptr->base_unit()) {
      return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                          "base unit redefined in units list: " + unit.name());
    }
  }

  // Create a mutable copy to add the base unit manually.
  intrinsic_proto::services::UnitCategory target_category =
      *target_category_ptr;
  intrinsic_proto::services::Unit* new_unit = target_category.add_units();
  new_unit->set_name(target_category.base_unit());
  new_unit->mutable_linear()->set_factor(1.0);
  new_unit->mutable_linear()->set_offset(0.0);

  const intrinsic_proto::services::Unit* from_proto_unit = nullptr;
  const intrinsic_proto::services::Unit* to_proto_unit = nullptr;

  for (const intrinsic_proto::services::Unit& unit : target_category.units()) {
    if (unit.name() == request->from_unit()) {
      from_proto_unit = &unit;
    }
    if (unit.name() == request->to_unit()) {
      to_proto_unit = &unit;
    }
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      (std::pair<double, double> from_params),
      GetLinearParams(from_proto_unit, request->from_unit()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      (std::pair<double, double> to_params),
      GetLinearParams(to_proto_unit, request->to_unit()));

  double from_factor = from_params.first;
  double from_offset = from_params.second;
  double to_factor = to_params.first;
  double to_offset = to_params.second;
  if (config_.reverse_order()) {
    std::swap(from_factor, to_factor);
    std::swap(from_offset, to_offset);
  }

  if (to_factor == 0) {
    return grpc::Status(
        grpc::StatusCode::FAILED_PRECONDITION,
        "invalid factor 0 in dataset for unit: " + request->to_unit());
  }

  double value = request->value();
  double base_val = (value * from_factor) + from_offset;
  double result = (base_val - to_offset) / to_factor;

  LOG(INFO) << value << " " << request->from_unit() << " = " << result << " "
            << request->to_unit();
  response->set_result(result);
  return grpc::Status::OK;
}

}  // namespace intrinsic::services
