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

// Package solutionconversion provides conversion functions between Solution
// representations.
package solutionconversion

import (
	"fmt"

	"intrinsic/assets/conversion/applicationasset"
	"intrinsic/assets/conversion/runtime"
	"intrinsic/assets/idutils"
	"intrinsic/assets/instances/instanceconversion"

	log "github.com/golang/glog"

	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	aigrpcpb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	solutiondeploymentpb "intrinsic/assets/proto/v1/solution_deployment_go_proto"
	solutionpb "intrinsic/assets/proto/v1/solution_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	commonpb "intrinsic/config/proto/common_go_proto"
	processpb "intrinsic/config/proto/process_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
)

// AsApplication converts a SolutionDeployment proto into an Application proto.
func AsApplication(sd *solutiondeploymentpb.SolutionDeployment) (*apb.Application, error) {
	assets := make(map[string]*apb.Application_Asset)
	for id, asset := range sd.GetSolution().GetAssets() {
		appAsset, err := applicationasset.AssetToApplicationAsset(asset)
		if err != nil {
			return nil, fmt.Errorf("failed to convert asset %q: %w", id, err)
		}
		assets[id] = appAsset
	}

	instances := make(map[string]*apb.Application_Instance)
	for name, inst := range sd.GetSolution().GetInstances() {
		instances[name] = &apb.Application_Instance{
			Asset:  inst.GetAsset(),
			Config: inst.GetConfig(),
		}
	}

	return &apb.Application{
		Metadata: &commonpb.Metadata{
			Name:                 sd.GetSolutionId(),
			DisplayName:          sd.GetDisplayName(),
			SolutionDeploymentId: sd.GetName(),
		},
		Process:            &processpb.Process{},
		Assets:             assets,
		Instances:          instances,
		ObjectWorldUpdates: sd.GetSolution().GetObjectWorldUpdates(),
		OperationMode:      sd.GetOperationMode(),
	}, nil
}

// asSolution converts an Application proto and its ResourceTypeRuntimes into a Solution proto.
func asSolution(app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) (*solutionpb.Solution, error) {
	rtsByID := map[string]*rtrpb.ResourceTypeRuntime{}
	for _, rt := range rts {
		id := idutils.IDFromProtoUnchecked(rt.GetMetadata().GetIdVersion().GetId())
		rtsByID[id] = rt
	}

	assets := make(map[string]*assetpb.Asset)
	for id, rtr := range rtsByID {
		asset, err := runtime.RuntimeToAsset(rtr)
		if err != nil {
			// TODO: b/517345754: Remove when all old-style pose estimators and
			// resources using world fragments have been removed from all solutions
			// and we can enforce this conversion.
			log.Warningf("Omitting %q from solution because runtime.RuntimeToAsset failed: %v", id, err)
		} else {
			assets[id] = asset
		}
	}

	instances := make(map[string]*aigrpcpb.AssetInstance)
	for _, ri := range app.GetResources().GetResourceInstances() {
		id, err := idutils.RemoveVersionFrom(ri.GetTypeIdVersion())
		if err != nil {
			return nil, fmt.Errorf("invalid type_id_version for instance %q: %w", ri.GetName(), err)
		}
		rtr, ok := rtsByID[id]
		if !ok {
			return nil, fmt.Errorf("missing runtime for instance %q (type %q)", ri.GetName(), id)
		}
		instances[ri.GetName()] = instanceconversion.ConvertResourceInstanceToAssetInstance(ri, rtr)
	}

	return &solutionpb.Solution{
		Assets:             assets,
		Instances:          instances,
		ObjectWorldUpdates: app.GetResources().GetObjectWorldUpdates().GetUpdates(),
	}, nil
}

// AsSolutionDeployment converts an Application proto and its ResourceTypeRuntimes into a SolutionDeployment proto.
func AsSolutionDeployment(app *apb.Application, rts map[string]*rtrpb.ResourceTypeRuntime) (*solutiondeploymentpb.SolutionDeployment, error) {
	solution, err := asSolution(app, rts)
	if err != nil {
		return nil, err
	}

	return &solutiondeploymentpb.SolutionDeployment{
		Name:          app.GetMetadata().GetSolutionDeploymentId(),
		DisplayName:   app.GetMetadata().GetDisplayName(),
		SolutionId:    app.GetMetadata().GetName(),
		OperationMode: app.GetOperationMode(),
		Solution:      solution,
	}, nil
}
