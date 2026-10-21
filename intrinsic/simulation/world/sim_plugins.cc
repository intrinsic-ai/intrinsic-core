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

#include "intrinsic/simulation/world/sim_plugins.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/text_format.h"
#include "intrinsic/icon/hal/proto/v1/digital_input_output.pb.h"
#include "intrinsic/scene/proto/v1/simulation_spec.pb.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/generic_action.pb.h"
#include "intrinsic/world/proto/robot_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "tinyxml2.h"

namespace intrinsic {
namespace simulation {
namespace {

using CameraPluginSpec =
    intrinsic_proto::world::SensorComponent::CameraPluginSpec;
using IconSimPluginSpec =
    intrinsic_proto::world::RobotComponent::IconSimPluginSpec;
using IconSimDevice = intrinsic_proto::world::RobotComponent::IconSimDevice;
using DigitalInputOutput = intrinsic_proto::icon::v1::DigitalInputOutput;
using DigitalInputBlock =
    intrinsic_proto::icon::v1::DigitalInputOutput::DigitalInputBlock;
using DigitalOutputBlock =
    intrinsic_proto::icon::v1::DigitalInputOutput::DigitalOutputBlock;
using WorldMultiCameraPluginSpec =
    intrinsic_proto::world::RobotComponent::MultiCameraPluginSpec;
using SceneMultiCameraPluginSpec =
    ::intrinsic_proto::scene_object::v1::MultiCameraPluginSpec;
using GenericActionPluginSpec =
    intrinsic_proto::world::generic_action::GenericActionPluginSpec;

using NoiseProto = intrinsic_proto::world::SensorComponent::Noise;
using tinyxml2::XMLDocument;
using tinyxml2::XMLElement;

absl::StatusOr<const tinyxml2::XMLElement*> GetPluginRootElement(
    absl::string_view xml_spec, XMLDocument& xml_doc) {
  if (xml_doc.Parse(xml_spec.data(), xml_spec.length()) !=
      tinyxml2::XML_SUCCESS) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Failed to parse <plugin> element: " << xml_doc.ErrorStr();
  }
  const auto* xml_elem = xml_doc.RootElement();
  if (xml_elem == nullptr) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "<plugin> element is invalid.";
  }
  return xml_elem;
}

absl::StatusOr<std::string> GetNestedElementText(
    absl::string_view xml_spec, absl::string_view parent_element,
    absl::string_view parent_element_name_attribute,
    absl::string_view nested_element) {
  XMLDocument xml_doc;
  INTR_ASSIGN_OR_RETURN(const tinyxml2::XMLElement* xml_elem,
                        GetPluginRootElement(xml_spec, xml_doc));
  const XMLElement* parent_element_ptr =
      xml_elem->FirstChildElement(parent_element.data());
  while (parent_element_ptr) {
    const tinyxml2::XMLAttribute* name_attr =
        parent_element_ptr->FindAttribute("name");
    if (name_attr && name_attr->Value() == parent_element_name_attribute) {
      break;
    }
    parent_element_ptr =
        parent_element_ptr->NextSiblingElement(parent_element.data());
  }
  if (!parent_element_ptr) {
    return intrinsic::NotFoundErrorBuilder()
           << "<" << parent_element
           << "> parent element with 'name' attribute <"
           << parent_element_name_attribute
           << "> does not exist in the following xml:\n"
           << xml_spec;
  }
  const XMLElement* nested_element_ptr =
      parent_element_ptr->FirstChildElement(nested_element.data());
  if (!nested_element_ptr) {
    return intrinsic::NotFoundErrorBuilder()
           << "<" << nested_element << "> nested element in parent element <"
           << parent_element << "> is missing in the following xml:\n"
           << xml_spec;
  }
  return nested_element_ptr->GetText();
}

void InsertXmlStringToXmlDoc(absl::string_view inlined_xml_string,
                             XMLDocument& xml_doc) {
  XMLDocument temp_doc;
  std::string wrapped_xml =
      absl::StrCat("<dummy_root>", inlined_xml_string, "</dummy_root>");
  CHECK_EQ(temp_doc.Parse(wrapped_xml.c_str()), tinyxml2::XML_SUCCESS);
  for (const tinyxml2::XMLNode* node = temp_doc.RootElement()->FirstChild();
       node != nullptr; node = node->NextSibling()) {
    xml_doc.RootElement()->InsertEndChild(node->DeepClone(&xml_doc));
  }
}

}  // namespace

absl::StatusOr<CameraPluginSpec> PluginSdfTrait<CameraPluginSpec>::Parse(
    absl::string_view xml_spec) {
  CameraPluginSpec plugin_spec;
  XMLDocument xml_doc;
  INTR_ASSIGN_OR_RETURN(const XMLElement* xml_elem,
                        GetPluginRootElement(xml_spec, xml_doc));

  // Parse and validate image elements. After validation, inline the xml spec
  // into the plugin spec proto.
  tinyxml2::XMLPrinter xml_printer;
  for (const XMLElement* image_elem = xml_elem->FirstChildElement("image");
       image_elem != nullptr;
       image_elem = image_elem->NextSiblingElement("image")) {
    const char* image_type = image_elem->Attribute("type");
    if (image_type == nullptr) {
      return absl::InvalidArgumentError(
          "Found image element with no type attribute");
    }
    int64_t sensor_id = -1;
    if (image_elem->QueryInt64Attribute("id", &sensor_id) !=
        tinyxml2::XML_SUCCESS) {
      return absl::InvalidArgumentError(
          absl::StrCat("Found image element of type '", image_type,
                       "' with missing or invalid id attribute"));
    }
    image_elem->Accept(&xml_printer);
  }
  *plugin_spec.mutable_inlined_plugin_xml() = xml_printer.CStr();

  return plugin_spec;
}

std::string PluginSdfTrait<CameraPluginSpec>::ToString(
    const CameraPluginSpec& plugin, int index) {
  static constexpr char kPluginXml[] =
      R"xml(<plugin filename="static://intrinsic::simulation::CameraPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name = absl::StrCat(kPluginName, index);
  xml_doc.RootElement()->SetAttribute("name", name.c_str());

  // Write camera identifier proto.
  if (!plugin.camera_identifier_proto().empty()) {
    auto* camera_identifier_elem = xml_doc.NewElement("camera_identifier");
    camera_identifier_elem->SetText(plugin.camera_identifier_proto().c_str());
    xml_doc.RootElement()->InsertEndChild(camera_identifier_elem);
  }

  // Write inlined xml spec.
  if (!plugin.inlined_plugin_xml().empty()) {
    InsertXmlStringToXmlDoc(plugin.inlined_plugin_xml(), xml_doc);
  }

  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

std::string PluginSdfTrait<SceneMultiCameraPluginSpec>::ToString(
    const SceneMultiCameraPluginSpec& plugin, std::optional<int> index) {
  static constexpr char kPluginXml[] =
      R"xml(<plugin filename="static://intrinsic::simulation::MultiCameraPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name =
      index.has_value() ? absl::StrCat(kPluginName, *index) : kPluginName;
  xml_doc.RootElement()->SetAttribute("name", name.c_str());

  // Write sensors.
  for (const auto& sensor : plugin.sensors()) {
    auto* e = xml_doc.NewElement("sensor");
    auto* id = xml_doc.NewElement("id");
    auto* name = xml_doc.NewElement("name");
    id->SetText(sensor.id());
    name->SetText(sensor.name().c_str());
    e->InsertEndChild(id);
    e->InsertEndChild(name);
    if (sensor.has_image_type()) {
      auto* image_type = xml_doc.NewElement("image_type");
      image_type->SetText(sensor.image_type().c_str());
      e->InsertEndChild(image_type);
    }
    xml_doc.RootElement()->InsertEndChild(e);
  }

  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

absl::StatusOr<IconSimPluginSpec> PluginSdfTrait<IconSimPluginSpec>::Parse(
    absl::string_view xml_spec) {
  return IconSimPluginSpec{};
}

std::string PluginSdfTrait<IconSimPluginSpec>::ToString(
    const IconSimPluginSpec& plugin, int index) {
  static constexpr char kPluginXml[] =
      R"xml(<plugin filename="static://intrinsic::simulation::TimeslicerWorld"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name = absl::StrCat(kPluginName, index);
  xml_doc.RootElement()->SetAttribute("name", name.c_str());
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

absl::Status PluginSdfTrait<IconSimDevice>::Parse(
    absl::string_view xml_spec, std::vector<IconSimDevice>* devices) {
  tinyxml2::XMLDocument xml_doc;
  if (xml_doc.Parse(xml_spec.data(), xml_spec.length()) !=
      tinyxml2::XML_SUCCESS) {
    return intrinsic::InternalErrorBuilder()
           << "Failed to parse <plugin> element: " << xml_doc.ErrorStr();
  }
  const auto* xml_elem = xml_doc.RootElement();
  if (xml_elem == nullptr) {
    return intrinsic::InternalErrorBuilder() << "<plugin> element is invalid.";
  }

  std::set<std::string> seen_joint_ids;
  std::set<std::string> seen_link_ids;

  const auto* dev_elem = xml_elem->FirstChildElement("device");
  while (dev_elem != nullptr) {
    devices->emplace_back(IconSimDevice{});
    auto* d = &(devices->back());
    d->set_name(dev_elem->Attribute("name"));
    d->set_type(dev_elem->Attribute("type"));
    const auto* joint_elem = dev_elem->FirstChildElement("joint");
    if (joint_elem != nullptr) {
      if (joint_elem->GetText() == nullptr ||
          strlen(joint_elem->GetText()) == 0) {
        return absl::InvalidArgumentError("Device has an empty joint element");
      }
      if (!seen_joint_ids.insert(joint_elem->GetText()).second) {
        return absl::InvalidArgumentError(
            absl::StrCat("Duplicate joint element (", joint_elem->GetText(),
                         ") found within device element"));
      }
      d->set_joint(joint_elem->GetText());
    }
    const auto* link_elem = dev_elem->FirstChildElement("link");
    if (link_elem != nullptr) {
      if (link_elem->GetText() == nullptr ||
          strlen(link_elem->GetText()) == 0) {
        return absl::InvalidArgumentError("Device has an empty link element");
      }

      if (!seen_link_ids.insert(link_elem->GetText()).second) {
        return absl::InvalidArgumentError(
            absl::StrCat("Duplicate link element (", link_elem->GetText(),
                         ") found within device element"));
      }
      d->set_link(link_elem->GetText());
    }
    const auto* initial_elem = dev_elem->FirstChildElement("initial");
    if (initial_elem != nullptr) {
      d->set_initial(initial_elem->DoubleText());
    }
    const auto* kp_elem = dev_elem->FirstChildElement("kp");
    if (kp_elem != nullptr) {
      d->set_kp(kp_elem->DoubleText());
    }
    const auto* ki_elem = dev_elem->FirstChildElement("ki");
    if (ki_elem != nullptr) {
      d->set_ki(ki_elem->DoubleText());
    }
    const auto* kd_elem = dev_elem->FirstChildElement("kd");
    if (kd_elem != nullptr) {
      d->set_kd(kd_elem->DoubleText());
    }
    dev_elem = dev_elem->NextSiblingElement("device");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> PluginSdfTrait<IconSimDevice>::GetOutputTopic(
    absl::string_view xml_spec, absl::string_view icon_sim_device_name) {
  return GetNestedElementText(xml_spec, "device", icon_sim_device_name,
                              "output_topic");
}

absl::StatusOr<std::string> PluginSdfTrait<IconSimDevice>::GetInputTopic(
    absl::string_view xml_spec, absl::string_view icon_sim_device_name) {
  return GetNestedElementText(xml_spec, "device", icon_sim_device_name,
                              "input_topic");
}

absl::StatusOr<std::string> PluginSdfTrait<IconSimDevice>::ToString(
    absl::Span<const IconSimDevice> devices,
    absl::flat_hash_map<std::string, std::string> device_name_to_output_topic,
    absl::flat_hash_map<std::string, std::string> device_name_to_input_topic,
    int index) {
  if (devices.size() != device_name_to_output_topic.size() ||
      devices.size() != device_name_to_input_topic.size()) {
    return InvalidArgumentErrorBuilder()
           << "device_name_to_output_topic and device_name_to_input_topic "
              "should be the same size as devices. Got "
              "device_name_to_output_topic size = "
           << device_name_to_output_topic.size()
           << ", device_name_to_input_topic size = "
           << device_name_to_input_topic.size()
           << " and devices size = " << devices.size() << " instead.";
  }

  static constexpr char kPluginXml[] =
      R"xml(<plugin
filename="static://intrinsic::simulation::DeviceContainerPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  std::string name = absl::StrCat(kPluginName, index);
  xml_doc.RootElement()->SetAttribute("name", name.c_str());
  for (const auto& d : devices) {
    auto* dev_elem = xml_doc.NewElement("device");
    dev_elem->SetAttribute("name", d.name().c_str());
    dev_elem->SetAttribute("type", d.type().c_str());
    if (d.has_joint()) {
      auto* e = xml_doc.NewElement("joint");
      e->SetText(d.joint().c_str());
      dev_elem->InsertEndChild(e);
    } else {
      // Joint devices shouldn't have any output/input topic specified at all
      auto iter = device_name_to_output_topic.find(d.name());
      if (iter != device_name_to_output_topic.end() && !iter->second.empty()) {
        auto* e = xml_doc.NewElement("output_topic");
        e->SetText(iter->second.data());
        dev_elem->InsertEndChild(e);
      }
      iter = device_name_to_input_topic.find(d.name());
      if (iter != device_name_to_input_topic.end() && !iter->second.empty()) {
        auto* e = xml_doc.NewElement("input_topic");
        e->SetText(iter->second.data());
        dev_elem->InsertEndChild(e);
      }
    }
    if (d.has_link()) {
      auto* e = xml_doc.NewElement("link");
      e->SetText(d.link().c_str());
      dev_elem->InsertEndChild(e);
    }
    if (d.has_initial()) {
      auto* e = xml_doc.NewElement("initial");
      e->SetText(d.initial());
      dev_elem->InsertEndChild(e);
    }
    if (d.has_kp()) {
      auto* e = xml_doc.NewElement("kp");
      e->SetText(d.kp());
      dev_elem->InsertEndChild(e);
    }
    if (d.has_ki()) {
      auto* e = xml_doc.NewElement("ki");
      e->SetText(d.ki());
      dev_elem->InsertEndChild(e);
    }
    if (d.has_kd()) {
      auto* e = xml_doc.NewElement("kd");
      e->SetText(d.kd());
      dev_elem->InsertEndChild(e);
    }
    xml_doc.RootElement()->InsertEndChild(dev_elem);
  }
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

absl::Status PluginSdfTrait<DigitalInputOutput>::Parse(
    absl::string_view xml_spec, DigitalInputOutput* digital_input_output) {
  tinyxml2::XMLDocument xml_doc;
  if (xml_doc.Parse(xml_spec.data(), xml_spec.length()) !=
      tinyxml2::XML_SUCCESS) {
    return intrinsic::InvalidArgumentErrorBuilder()
           << "Failed to parse <plugin> element: " << xml_doc.ErrorStr();
  }
  const auto* xml_elem = xml_doc.RootElement();
  if (xml_elem == nullptr) {
    return intrinsic::InternalErrorBuilder() << "<plugin> element is invalid.";
  }

  for (const auto* input_block_elem =
           xml_elem->FirstChildElement("input_block");
       input_block_elem != nullptr;
       input_block_elem = input_block_elem->NextSiblingElement("input_block")) {
    const char* block_name = input_block_elem->Attribute("name");
    if (block_name == nullptr) {
      return absl::InvalidArgumentError("Digital input block has no name");
    }
    int num_bits = 0;
    if (tinyxml2::XMLError err =
            input_block_elem->QueryIntAttribute("num_bits", &num_bits);
        err != tinyxml2::XML_SUCCESS) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to read number of bits for digital input block '", block_name,
          "'"));
    }
    // This is optional, so don't error if it's nullptr.
    const char* sim_topic = input_block_elem->Attribute("topic_name");

    if (sim_topic != nullptr) {
      LOG(WARNING) << "Digital input block '" << block_name
                   << "' defines a topic name. Please remove the `topic_name` "
                      "attribute from your SDF. Topic names are automatically "
                      "generated based on the name of each DIO device/block.";
    }
    if (num_bits > 32) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Digital input block '", block_name, "' has more than 32 bits (",
          num_bits,
          "). Publishing DIO bits to simulation topics only supports up to 32 "
          "bits. Please reduce the number of bits."));
    }
    // First, check if there's already an input block called `name`:
    if (digital_input_output->digital_input_blocks().contains(block_name)) {
      return absl::AlreadyExistsError(
          absl::StrCat("There is more than one digital input block called '",
                       block_name, "' in the same model."));
    }
    DigitalInputBlock input_block;
    input_block.set_num_bits(num_bits);
    digital_input_output->mutable_digital_input_blocks()->emplace(block_name,
                                                                  input_block);
  }

  // Next, process the output blocks
  for (const auto* output_block_elem =
           xml_elem->FirstChildElement("output_block");
       output_block_elem != nullptr;
       output_block_elem =
           output_block_elem->NextSiblingElement("output_block")) {
    const char* block_name = output_block_elem->Attribute("name");
    if (block_name == nullptr) {
      return absl::InvalidArgumentError("Digital output block has no name");
    }
    int num_bits = 0;
    if (tinyxml2::XMLError err =
            output_block_elem->QueryIntAttribute("num_bits", &num_bits);
        err != tinyxml2::XML_SUCCESS) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Failed to read number of bits for digital output block '",
          block_name, "'"));
    }
    // This is optional, so don't error if it's nullptr.
    const char* sim_topic = output_block_elem->Attribute("topic_name");

    if (sim_topic != nullptr) {
      LOG(WARNING) << "Digital output block '" << block_name
                   << "' defines a topic name. Please remove the `topic_name` "
                      "attribute from your SDF. Topic names are automatically "
                      "generated based on the name of each DIO device/block.";
    }
    if (num_bits > 32) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Digital output block '", block_name, "' has more than 32 bits (",
          num_bits,
          "). Publishing DIO bits to simulation topics only supports up to 32 "
          "bits. Please reduce the number of bits."));
    }
    // First, check if there's already an output block called `name`:
    if (digital_input_output->digital_output_blocks().contains(block_name)) {
      return absl::AlreadyExistsError(
          absl::StrCat("There is more than one digital output black called '",
                       block_name, "' in the same model."));
    }
    DigitalOutputBlock output_block;
    output_block.set_num_bits(num_bits);
    digital_input_output->mutable_digital_output_blocks()->emplace(
        block_name, output_block);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> PluginSdfTrait<DigitalInputOutput>::ToString(
    const DigitalInputOutput& digital_input_output,
    const absl::flat_hash_map<std::string, std::string>&
        block_name_to_output_topic,
    const absl::flat_hash_map<std::string, std::string>&
        block_name_to_input_topic) {
  if (digital_input_output.digital_input_blocks().size() !=
          block_name_to_input_topic.size() ||
      digital_input_output.digital_output_blocks().size() !=
          block_name_to_output_topic.size()) {
    return InvalidArgumentErrorBuilder()
           << "block_name_to_output_topic and block_name_to_input_topic "
              "must have the same number of entries as there are input and "
              "output blocks, respectively. Got block_name_to_output_topic "
              "size = "
           << block_name_to_output_topic.size()
           << ", block_name_to_input_topic size = "
           << block_name_to_input_topic.size() << " and there are "
           << digital_input_output.digital_input_blocks().size()
           << " digital inputs, and "
           << digital_input_output.digital_output_blocks().size()
           << " digital outputs.";
  }
  static constexpr char kPluginXml[] =
      R"xml(<plugin
filename="static://intrinsic::simulation::DigitalInputOutput" />)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  xml_doc.RootElement()->SetAttribute("name", "DigitalInputOutput");

  for (const auto& [name, block] :
       digital_input_output.digital_input_blocks()) {
    auto* dev_elem = xml_doc.NewElement("input_block");
    dev_elem->SetAttribute("name", name.c_str());
    dev_elem->SetAttribute("num_bits", block.num_bits());
    // Find input topic name, error if there isn't one.
    if (auto iter = block_name_to_input_topic.find(name);
        iter != block_name_to_input_topic.end() && !iter->second.empty()) {
      dev_elem->SetAttribute("topic_name", iter->second.c_str());
    } else {
      return absl::InternalError(absl::StrCat(
          "The DIO block input topic map has no entry for block '", name,
          "'. This is a bug in Intrinsic SDF parsing code, please report it."));
    }
    xml_doc.RootElement()->InsertEndChild(dev_elem);
  }
  for (const auto& [name, block] :
       digital_input_output.digital_output_blocks()) {
    auto* dev_elem = xml_doc.NewElement("output_block");
    dev_elem->SetAttribute("name", name.c_str());
    dev_elem->SetAttribute("num_bits", block.num_bits());
    // Find output topic name, error if there isn't one.
    if (auto iter = block_name_to_output_topic.find(name);
        iter != block_name_to_output_topic.end() && !iter->second.empty()) {
      dev_elem->SetAttribute("topic_name", iter->second.c_str());
    } else {
      return absl::InternalError(absl::StrCat(
          "The DIO block output topic map has no entry for block '", name,
          "'. This is a bug in Intrinsic SDF parsing code, please report it."));
    }
    xml_doc.RootElement()->InsertEndChild(dev_elem);
  }
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

absl::StatusOr<GenericActionPluginSpec>
PluginSdfTrait<GenericActionPluginSpec>::Parse(absl::string_view xml_spec) {
  XMLDocument xml_doc;
  INTR_ASSIGN_OR_RETURN(auto xml_elem, GetPluginRootElement(xml_spec, xml_doc));
  GenericActionPluginSpec result;
  const auto* ga_elem = xml_elem->FirstChildElement("generic_action");
  if (ga_elem && !google::protobuf::TextFormat::ParseFromString(
                     ga_elem->GetText(), result.mutable_action_configs())) {
    return absl::InvalidArgumentError(
        "Failed to parse <generic_action> field as an "
        "intrinsic_proto::world::generic_action::GenericActionConfig");
  }
  return result;
}

std::string PluginSdfTrait<GenericActionPluginSpec>::ToString(
    const GenericActionPluginSpec& plugin) {
  static constexpr char kPluginXml[] =
      R"xml(<plugin
  filename="static://intrinsic::simulation::GenericActionPlugin"/>)xml";
  tinyxml2::XMLDocument xml_doc;
  CHECK_EQ(xml_doc.Parse(kPluginXml), tinyxml2::XML_SUCCESS);
  tinyxml2::XMLPrinter xml_printer;
  xml_doc.RootElement()->SetAttribute("name", kPluginName);

  if (plugin.has_action_configs()) {
    auto* e = xml_doc.NewElement("generic_action");
    std::string generic_action_string;
    static_cast<void>(::google::protobuf::TextFormat::PrintToString(
        plugin.action_configs(),
        &generic_action_string));  // TODO: Handle return value
    e->SetText(generic_action_string.c_str());
    xml_doc.RootElement()->InsertEndChild(e);
  }
  xml_doc.Print(&xml_printer);
  return xml_printer.CStr();
}

}  // namespace simulation
}  // namespace intrinsic
