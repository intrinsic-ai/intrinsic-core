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

// assetlocalinfogen creates an AssetLocalInfo proto which contains info about
// the built asset bundle file.
package main

import (
	"flag"

	"intrinsic/assets/typeutils"
	"intrinsic/production/intrinsic"
	intrinsicflag "intrinsic/util/flag"
	"intrinsic/util/proto/descriptor"
	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"

	log "github.com/golang/glog"

	assetpb "intrinsic/assets/build_defs/asset_go_proto"
	dmpb "intrinsic/assets/data/proto/v1/data_manifest_go_proto"
	hdmpb "intrinsic/assets/hardware_devices/proto/v1/hardware_device_manifest_go_proto"
	pmpb "intrinsic/assets/processes/proto/process_manifest_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	sompb "intrinsic/assets/scene_objects/proto/scene_object_manifest_go_proto"
	sempb "intrinsic/assets/services/proto/service_manifest_go_proto"
	skmpb "intrinsic/skills/proto/skill_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
)

var (
	assetType          = flag.String("asset_type", "", "The type of asset.")
	manifest           = flag.String("manifest", "", "The asset's manifest.")
	fileDescriptorSets = intrinsicflag.MultiString("file_descriptor_set", nil, "Path to a binary file descriptor set proto to be used to resolve the data payload. Can be repeated. Passing only empty files has the same effect as passing no files at all.")
	mergeFDS           = flag.Bool("merge_fds", false, "Merge files within the asset's file descriptor set.  This should only enabled for HardwareDevices as other assets are expected to provide a single, self-consistent set of proto dependencies.")
	outputAssetInfo    = flag.String("output_asset_info", "", "Output AssetInfo proto path.")
)

func writeAsset(fds *dpb.FileDescriptorSet) {
	var id *idpb.Id
	atype := typeutils.AssetTypeFromName(*assetType)
	if atype == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
		log.Exitf("unknown asset type %q", *assetType)
	}

	switch atype {
	case atypepb.AssetType_ASSET_TYPE_DATA:
		m := new(dmpb.DataManifest)
		types, err := registryutil.NewTypesFromFileDescriptorSet(fds)
		if err != nil {
			log.Exitf("cannot parse file descriptor set protos: %v", err)
		}
		if err := protoio.ReadTextProto(*manifest, m, protoio.WithResolver(types)); err != nil {
			log.Exitf("failed to read data manifest: %v", err)
		}
		id = m.GetMetadata().GetId()
	case atypepb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
		m := new(hdmpb.HardwareDeviceManifest)
		if err := protoio.ReadTextProto(*manifest, m); err != nil {
			log.Exitf("failed to read hardware device manifest: %v", err)
		}
		id = m.GetMetadata().GetId()
	case atypepb.AssetType_ASSET_TYPE_SCENE_OBJECT:
		m := new(sompb.SceneObjectManifest)
		if err := protoio.ReadTextProto(*manifest, m); err != nil {
			log.Exitf("failed to read scene object manifest: %v", err)
		}
		id = m.GetMetadata().GetId()
	case atypepb.AssetType_ASSET_TYPE_SERVICE:
		m := new(sempb.ServiceManifest)
		if err := protoio.ReadTextProto(*manifest, m); err != nil {
			log.Exitf("failed to read service manifest: %v", err)
		}
		id = m.GetMetadata().GetId()
	case atypepb.AssetType_ASSET_TYPE_SKILL:
		m := new(skmpb.SkillManifest)
		if err := protoio.ReadBinaryProto(*manifest, m); err != nil {
			log.Exitf("failed to read skill manifest: %v", err)
		}
		id = m.GetId()
	case atypepb.AssetType_ASSET_TYPE_PROCESS:
		m := new(pmpb.ProcessManifest)
		if err := protoio.ReadBinaryProto(*manifest, m); err != nil {
			log.Exitf("failed to read process manifest: %v", err)
		}
		id = m.GetMetadata().GetId()
	default:
		log.Exitf("unsupported asset type %q", *assetType)
	}
	if err := protoio.WriteBinaryProto(*outputAssetInfo, &assetpb.AssetInfo{
		AssetType:         atype,
		Id:                id,
		FileDescriptorSet: fds,
	}, protoio.WithDeterministic(true)); err != nil {
		log.Exitf("Could not write asset info: %v", err)
	}
}

func main() {
	intrinsic.Init()

	fds, err := registryutil.LoadFileDescriptorSets(*fileDescriptorSets)
	if err != nil {
		log.Exitf("cannot build file descriptor set for asset: %v", err)
	}
	if *mergeFDS {
		fds, err = descriptor.MergeFileDescriptorSets([]*dpb.FileDescriptorSet{fds})
		if err != nil {
			log.Exitf("cannot merge file descriptor set for asset: %v", err)
		}
	}
	// Passing only empty files has the same effect as passing no files at all.
	// This behavior makes it easier to handle assets for which the file
	// descriptor set is optional (e.g., Process assets).
	if len(fds.GetFile()) == 0 {
		fds = nil
	}

	writeAsset(fds)
}
