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

// Package platform contains utilities that list the interfaces an Asset provides to the
// platform. These interfaces can be used to determine whether an Asset is compatible with
// a given platform version.
package platform

import (
	"intrinsic/assets/hardware_devices/graph" 
	"intrinsic/assets/idutils"
	"intrinsic/assets/interfaceutils"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	sspb "intrinsic/assets/services/proto/v1/service_state_go_proto"
	pskmpb "intrinsic/skills/proto/processed_skill_manifest_go_proto"
	skmpb "intrinsic/skills/proto/skill_manifest_go_proto"
)

const (
	// RuntimeAssetID is the Asset ID of the "Platform as Asset".
	RuntimeAssetID = "ai.intrinsic.runtime"
	// RuntimeInstanceName is the Asset instance name of the "Platform as Asset".
	RuntimeInstanceName = "intrinsic_runtime"

	// DynamicReconfigurationV1Name is the proto name of the v1 DynamicReconfiguration service.
	DynamicReconfigurationV1Name = "intrinsic_proto.services.v1.DynamicReconfiguration"
	// ServiceStateV1Name is the proto name of the v1 ServiceState service.
	ServiceStateV1Name = "intrinsic_proto.services.v1.ServiceState"

	// ProjectorName is the proto name of the Projector service.
	ProjectorName = "intrinsic_proto.skills.Projector"
	// ExecutorName is the proto name of the Executor service.
	ExecutorName = "intrinsic_proto.skills.Executor"
	// SkillInformationName is the proto name of the SkillInformation service.
	SkillInformationName = "intrinsic_proto.skills.SkillInformation"
)

// SkillRequiredProvidedToPlatformInterfaces lists the interfaces that a Skill must provide to be
// compatible with the current platform version.
var SkillRequiredProvidedToPlatformInterfaces = []string{
	interfaceutils.GRPCURIPrefix + ProjectorName,
	interfaceutils.GRPCURIPrefix + ExecutorName,
	interfaceutils.GRPCURIPrefix + SkillInformationName,
}

// ProvidedByProcessedSkillManifest lists the interfaces the Skill provides to the platform.
func ProvidedByProcessedSkillManifest(manifest *pskmpb.ProcessedSkillManifest) []*metadatapb.Interface {
	if manifest == nil {
		return nil
	}
	return providedBySkillOptions(manifest.GetDetails().GetOptions())
}

// ProvidedBySkillManifest lists the interfaces the Skill provides to the platform.
func ProvidedBySkillManifest(manifest *skmpb.SkillManifest) []*metadatapb.Interface {
	if manifest == nil {
		return nil
	}
	return providedBySkillOptions(manifest.GetOptions())
}

func providedBySkillOptions(options *skmpb.Options) []*metadatapb.Interface {
	if options == nil {
		return nil
	}
	var interfaces []*metadatapb.Interface
	for _, v := range options.GetSkillServicesConfig().GetServiceVersions() {
		var name string
		switch v {
		case skmpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_PROJECTOR:
			name = interfaceutils.GRPCURIPrefix + ProjectorName
		case skmpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_EXECUTOR:
			name = interfaceutils.GRPCURIPrefix + ExecutorName
		case skmpb.SkillServicesConfig_INTRINSIC_PROTO_SKILLS_SKILL_INFORMATION:
			name = interfaceutils.GRPCURIPrefix + SkillInformationName
		default:
			continue
		}
		interfaces = append(interfaces, &metadatapb.Interface{
			Uri: name,
		})
	}
	return interfaces
}

// ProvidedByProcessedServiceManifest lists the interfaces the Service provides to the platform.
func ProvidedByProcessedServiceManifest(manifest *smpb.ProcessedServiceManifest) []*metadatapb.Interface {
	if manifest == nil {
		return nil
	}
	return providedByServiceDef(manifest.GetServiceDef())
}

// ProvidedByServiceManifest lists the interfaces the Service provides to the platform.
func ProvidedByServiceManifest(manifest *smpb.ServiceManifest) []*metadatapb.Interface {
	if manifest == nil {
		return nil
	}
	return providedByServiceDef(manifest.GetServiceDef())
}

func providedByServiceDef(serviceDef *smpb.ServiceDef) []*metadatapb.Interface {
	if serviceDef == nil {
		return nil
	}
	var interfaces []*metadatapb.Interface
	for _, v := range serviceDef.GetDynamicReconfigurationConfig().GetServiceVersions() {
		if v == drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION {
			interfaces = append(interfaces, &metadatapb.Interface{
				Uri: interfaceutils.GRPCURIPrefix + DynamicReconfigurationV1Name,
			})
		}
	}
	for _, v := range serviceDef.GetServiceStateConfig().GetServiceVersions() {
		if v == sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE {
			interfaces = append(interfaces, &metadatapb.Interface{
				Uri: interfaceutils.GRPCURIPrefix + ServiceStateV1Name,
			})
		}
	}
	return interfaces
}

// ProvidedByProcessedHardwareDeviceManifest lists the interfaces a Hardware Device provides to the
// platform. Note that interfaces from non-inlined service Assets are excluded. Prefer
// ProvidedByCollectedAssets when possible.
func ProvidedByProcessedHardwareDeviceManifest(manifest *hdmpb.ProcessedHardwareDeviceManifest) []*metadatapb.Interface {
	if manifest == nil {
		return nil
	}
	var interfaces []*metadatapb.Interface
	for _, pa := range manifest.GetAssets() {
		// Note that non-inlined Services that are stored in the catalog will not have their interfaces
		// included in the output. Please use ProvidedByCollectedAssets instead.
		if m := pa.GetService(); m != nil {
			interfaces = append(interfaces, ProvidedByProcessedServiceManifest(m)...)
		}
	}
	return interfaces
}



// ProvidedByCollectedAssets lists the interfaces that this collection of Assets provides
// to the platform.
func ProvidedByCollectedAssets(assets *graph.CollectedAssets) []*metadatapb.Interface {
	if assets == nil {
		return nil
	}
	var interfaces []*metadatapb.Interface
	for _, service := range assets.Services() {
		interfaces = append(interfaces, ProvidedByProcessedServiceManifest(service)...)
	}
	return interfaces
}



// ValidateIDNotReserved validates that the given Asset ID is not reserved.
func ValidateIDNotReserved(id *idpb.Id) error {
	idStr, err := idutils.IDFromProto(id)
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "invalid id: %v", err)
	}
	// Verify that the Asset ID is not reserved.
	if idStr == RuntimeAssetID {
		return status.Errorf(codes.InvalidArgument, "invalid id %q is reserved", RuntimeAssetID)
	}
	return nil
}

// ValidateInstanceNameNotReserved validates that the given Asset instance name is not reserved.
func ValidateInstanceNameNotReserved(instanceName string) error {
	// Verify that the Asset instance name is not reserved.
	if instanceName == RuntimeInstanceName {
		return status.Errorf(codes.InvalidArgument, "invalid instance name %q is reserved", RuntimeInstanceName)
	}
	return nil
}
