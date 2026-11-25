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

#include "intrinsic/executive/engine/span_file_exporter.h"

#include <cstdint>
#include <filesystem>  // NOLINT
#include <memory>
#include <span>
#include <string>
#include <system_error>  // NOLINT
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "opentelemetry/exporters/otlp/otlp_recordable.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "opentelemetry/sdk/trace/batch_span_processor_factory.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/sdk/trace/tracer_provider.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_id.h"
#include "opentelemetry/trace/trace_id.h"
#include "riegeli/bytes/cfile_writer.h"
#include "riegeli/records/record_writer.h"

namespace intrinsic::executive {

constexpr std::string_view kDefaultOutputDirectory = "/debug-logs/traces";

namespace {

namespace otel_common_proto = opentelemetry::proto::common::v1;
namespace otel_trace_proto = opentelemetry::proto::trace::v1;

namespace trace_sdk = opentelemetry::sdk::trace;
namespace trace_api = opentelemetry::trace;
namespace sdkcommon = opentelemetry::sdk::common;

// Actual OpenCensus exporter handler. There is only one instance of this and
// once registered it cannot be unregistred. SpanFileExporter is the external
// interface forwarding to this instance.
class SpanFileExporterHandler : public trace_sdk::SpanExporter {
 public:
  SpanFileExporterHandler()
      : enabled_(false), output_directory_(kDefaultOutputDirectory) {}

  std::unique_ptr<trace_sdk::Recordable> MakeRecordable() noexcept override {
    return std::make_unique<opentelemetry::exporter::otlp::OtlpRecordable>();
  }

  sdkcommon::ExportResult Export(
      const std::span<std::unique_ptr<trace_sdk::Recordable>>& spans) noexcept
      override {
    absl::MutexLock lock(&mu_);
    if (!enabled_) {
      return sdkcommon::ExportResult::kSuccess;
    }

    std::error_code ec;
    if (!std::filesystem::exists(output_directory_, ec) ||
        !std::filesystem::is_directory(output_directory_, ec)) {
      LOG_EVERY_N_SEC(ERROR, 60)
          << "Output directory does not exist or is not a directory: "
          << output_directory_ << ". Discarding spans.";
      return sdkcommon::ExportResult::kFailure;
    }

    // Group spans by trace_id. The raw pointers in the map remain valid
    // throughout this function since the input span<unique_ptr> outlives them.
    absl::flat_hash_map<
        std::string,
        std::vector<const opentelemetry::exporter::otlp::OtlpRecordable*>>
        spans_by_trace;
    for (const std::unique_ptr<trace_sdk::Recordable>& recordable : spans) {
      if (recordable == nullptr) {
        continue;
      }
      // This exporter requires OTLP protobuf serialization. The downcast is
      // expected to succeed because MakeRecordable() produces OtlpRecordable.
      const opentelemetry::exporter::otlp::OtlpRecordable* otlp_recordable =
          dynamic_cast<const opentelemetry::exporter::otlp::OtlpRecordable*>(
              recordable.get());
      if (otlp_recordable == nullptr) {
        LOG_EVERY_N_SEC(ERROR, 60) << "Failed to downcast Recordable to "
                                      "OtlpRecordable; discarding span.";
        continue;
      }

      std::string trace_id_hex =
          absl::BytesToHexString(otlp_recordable->span().trace_id());
      spans_by_trace[trace_id_hex].push_back(otlp_recordable);
    }

    // Write each group to a separate file.
    for (const auto& [trace_id_hex, trace_spans] : spans_by_trace) {
      int& file_index = trace_file_indexes_[trace_id_hex];
      file_index++;

      std::string filename =
          absl::StrFormat("trace_%s_%06d.riegeli", trace_id_hex, file_index);
      std::string tmp_filename = filename + ".tmp";
      std::filesystem::path filepath =
          std::filesystem::path(output_directory_) / filename;
      std::filesystem::path tmp_filepath =
          std::filesystem::path(output_directory_) / tmp_filename;

      // First write to a tmp file, so that reading a .riegeli file always reads
      // a complete file.
      riegeli::RecordWriter writer(riegeli::CFileWriter(tmp_filepath.string()));
      if (!writer.ok()) {
        LOG_EVERY_N_SEC(ERROR, 60)
            << "Failed to open Riegeli file for writing: "
            << tmp_filepath.string() << " status: " << writer.status();
        continue;  // Try next trace group.
      }

      for (const opentelemetry::exporter::otlp::OtlpRecordable*
               otlp_recordable : trace_spans) {
        if (!writer.WriteRecord(otlp_recordable->span())) {
          LOG_EVERY_N_SEC(ERROR, 60)
              << "Failed to write span record to Riegeli: " << writer.status();
        }
      }

      if (!writer.Close()) {
        LOG_EVERY_N_SEC(ERROR, 60)
            << "Failed to close Riegeli writer: " << writer.status();
        continue;
      }

      std::error_code rename_ec;
      std::filesystem::rename(tmp_filepath, filepath, rename_ec);
      if (rename_ec) {
        LOG_EVERY_N_SEC(ERROR, 60)
            << "Failed to rename temp file " << tmp_filepath.string() << " to "
            << filepath.string() << " error: " << rename_ec.message();
      }
    }

    return sdkcommon::ExportResult::kSuccess;
  }

  bool ForceFlush(std::chrono::microseconds /*timeout*/) noexcept override {
    return true;
  }

  bool Shutdown(std::chrono::microseconds /*timeout*/) noexcept override {
    return true;
  }

  void SetEnabled(bool enabled) {
    absl::MutexLock lock(&mu_);
    enabled_ = enabled;
  }

  bool IsEnabled() const {
    absl::MutexLock lock(&mu_);
    return enabled_;
  }

  void SetOutputDirectory(std::string_view dir) {
    absl::MutexLock lock(&mu_);
    output_directory_ = std::string(dir);
    trace_file_indexes_.clear();
  }

  std::string GetOutputDirectory() const {
    absl::MutexLock lock(&mu_);
    return output_directory_;
  }

 private:
  mutable absl::Mutex mu_;
  bool enabled_ ABSL_GUARDED_BY(mu_);
  std::string output_directory_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_map<std::string, int> trace_file_indexes_
      ABSL_GUARDED_BY(mu_);
};

// Global handler pointer.
SpanFileExporterHandler* g_handler = nullptr;

// Registers the exporter with OpenTelemetry. Safe to call multiple times.
// Must be called at least once to initialize the exporter.
void RegisterSpanFileExporter() {
  static absl::once_flag register_exporter;
  absl::call_once(register_exporter, []() {
    auto exporter = std::make_unique<SpanFileExporterHandler>();
    g_handler = exporter.get();

    trace_sdk::BatchSpanProcessorOptions processor_options;
    auto processor = trace_sdk::BatchSpanProcessorFactory::Create(
        std::move(exporter), processor_options);

    std::shared_ptr<opentelemetry::trace::TracerProvider> global_provider =
        trace_api::Provider::GetTracerProvider();
    auto* sdk_provider =
        dynamic_cast<trace_sdk::TracerProvider*>(global_provider.get());

    if (sdk_provider != nullptr) {
      sdk_provider->AddProcessor(std::move(processor));
    } else {
      auto provider =
          trace_sdk::TracerProviderFactory::Create(std::move(processor));
      trace_api::Provider::SetTracerProvider(std::move(provider));
    }
  });
}

}  // namespace

// static
void SpanFileExporter::Enable() {
  RegisterSpanFileExporter();  // Ensure registered.
  if (g_handler != nullptr) {
    g_handler->SetEnabled(true);
  }
}

// static
void SpanFileExporter::Disable() {
  if (g_handler != nullptr) {
    g_handler->SetEnabled(false);
  }
}

// static
bool SpanFileExporter::IsEnabled() {
  if (g_handler != nullptr) {
    return g_handler->IsEnabled();
  }
  return false;
}

// static
void SpanFileExporter::ForceFlush() {
  std::shared_ptr<opentelemetry::trace::TracerProvider> global_provider =
      trace_api::Provider::GetTracerProvider();
  auto* sdk_provider = dynamic_cast<opentelemetry::sdk::trace::TracerProvider*>(
      global_provider.get());
  if (sdk_provider != nullptr) {
    sdk_provider->ForceFlush();
  }
}

// static
void SpanFileExporter::SetOutputDirectoryForTesting(std::string_view dir) {
  if (g_handler != nullptr) {
    g_handler->SetOutputDirectory(dir);
  }
}

// static
std::string SpanFileExporter::GetOutputDirectory() {
  if (g_handler != nullptr) {
    return g_handler->GetOutputDirectory();
  }
  return std::string(kDefaultOutputDirectory);
}

}  // namespace intrinsic::executive
