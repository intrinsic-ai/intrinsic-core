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

#include "intrinsic/simulation/world/gripper_plugin_spec.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/text_format.h"
#include "tinyxml2.h"

namespace intrinsic {
namespace simulation {

std::string ActuatedGripperPluginSpec::ToSdformatXmlString(
    std::string_view plugin_name_suffix) const {
  static constexpr char kPluginXml[] =
      R"xml(<plugin filename="static://intrinsic::simulation::ActuatedStickyGripperPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name = absl::StrCat(kPluginName, plugin_name_suffix);
  xml_doc.RootElement()->SetAttribute("name", name.c_str());
  if (!status_topic.empty()) {
    auto* e = xml_doc.NewElement("status_topic");
    e->SetText(status_topic.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (pinch_gripper_config.has_value()) {
    auto* e = xml_doc.NewElement("pinch_gripper_config");
    std::string config_string;
    static_cast<void>(::google::protobuf::TextFormat::PrintToString(
        *pinch_gripper_config, &config_string));  // TODO: Handle return value
    e->SetText(config_string.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (service_pinch_gripper_config.has_value()) {
    auto* e = xml_doc.NewElement("service_pinch_gripper_config");
    std::string config_string;
    static_cast<void>(::google::protobuf::TextFormat::PrintToString(
        *service_pinch_gripper_config,
        &config_string));  // TODO: Handle return value
    e->SetText(config_string.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (!pinch_gripper_handle.empty()) {
    auto* e = xml_doc.NewElement("pinch_gripper_handle");
    e->SetText(pinch_gripper_handle.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (!sticky_link_name.empty()) {
    auto* e = xml_doc.NewElement("sticky_link");
    e->SetText(sticky_link_name.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  for (const auto& joint_name : joint_names) {
    auto* e = xml_doc.NewElement("joint");
    e->SetAttribute("name", joint_name.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (is_default_closed.has_value()) {
    auto* e = xml_doc.NewElement("is_default_closed");
    e->SetText(*is_default_closed);
    xml_doc.RootElement()->InsertEndChild(e);
  }
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

std::string FixedJointGripperPluginSpec::ToSdformatXmlString(
    std::string_view plugin_name_suffix) const {
  static constexpr char kPluginXml[] =
      R"xml(<plugin filename="static://intrinsic::simulation::FixedJointGripperPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name = absl::StrCat(kPluginName, plugin_name_suffix);
  xml_doc.RootElement()->SetAttribute("name", name.c_str());
  if (!gripper_command_topic.empty()) {
    auto* e = xml_doc.NewElement("gripper_command_topic");
    e->SetText(gripper_command_topic.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (!gripper_status_topic.empty()) {
    auto* e = xml_doc.NewElement("gripper_status_topic");
    e->SetText(gripper_status_topic.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (!gripper_link_name.empty()) {
    auto* e = xml_doc.NewElement("gripper_link");
    e->SetText(gripper_link_name.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  if (suction_gripper_config.has_value()) {
    auto* e = xml_doc.NewElement("suction_gripper_config");
    std::string command_string;
    static_cast<void>(::google::protobuf::TextFormat::PrintToString(
        *suction_gripper_config,
        &command_string));  // TODO: Handle return value
    e->SetText(command_string.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

}  // namespace simulation
}  // namespace intrinsic
