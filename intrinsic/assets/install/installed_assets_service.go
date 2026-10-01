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

// Package installedassetsservice implements the installed assets gRPC service
package installedassetsservice

import (
	"context"
	"errors"
	"fmt"
	"maps"
	"slices"
	"strings"

	"intrinsic/assets/data/datavalidate"
	"intrinsic/assets/dependencies/graph"
	"intrinsic/assets/dependencies/runtimegraph"
	"intrinsic/assets/deploy/privileges"
	"intrinsic/assets/deploy/render"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/idutils"
	"intrinsic/assets/install/gather"
	"intrinsic/assets/install/localconv"
	"intrinsic/assets/install/references"
	"intrinsic/assets/install/transaction"
	"intrinsic/assets/interfaceutils"
	"intrinsic/assets/orderedmap"
	"intrinsic/assets/scene_objects/sceneobjectvalidate"
	"intrinsic/assets/services/servicevalidate"
	"intrinsic/assets/typeutils"
	"intrinsic/assets/viewutils"
	"intrinsic/kubernetes/acl/clientcontext"
	"intrinsic/longrunning/go/operations"
	"intrinsic/resources/resourcefix"
	"intrinsic/resources/resourcevalidate"
	"intrinsic/resources/service/resourcereader"
	"intrinsic/resources/service/resourceregistryutil"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/util/grpc/statusutil"
	"intrinsic/util/pagination/pagetoken"
	"intrinsic/util/proto/fieldbehavior"
	"intrinsic/util/proto/names"

	log "github.com/golang/glog"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	runtime "intrinsic/assets/conversion/runtime"
	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	iopb "intrinsic/assets/proto/installation_origin_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	mdpb "intrinsic/assets/proto/metadata_go_proto"
	assetpb "intrinsic/assets/proto/v1/asset_go_proto"
	searchpb "intrinsic/assets/proto/v1/search_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	rspb "intrinsic/config/proto/resource_set_go_proto"
	tpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	rcigrpcpb "intrinsic/resources/catalog/proto/resource_catalog_internal_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	svsgrpcpb "intrinsic/solution_versions/proto/v1/solution_version_service_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	statuspb "intrinsic/util/status/extended_status_go_proto"
	owsgrpcpb "intrinsic/world/public/proto/object_world_service_go_proto"
	owspb "intrinsic/world/public/proto/object_world_service_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

const (
	// With the removal of the workcell-spec, this is always constant.  If we
	// wanted to be conservative, we could call "GetWorkcellStatus" on the
	// transfer service and grab the "name" field.
	appChartName = "intrinsic-app-chart"
	// TODO - b/371204053: handle multiple init worlds.
	initWorldID = "init_world"
)

type installedAssetsService struct {
	aciClient           acigrpcpb.AssetCatalogInternalClient
	rciClient           rcigrpcpb.ResourceCatalogInternalClient
	rtrClient           resourcetyperuntime.Client
	svsClient           svsgrpcpb.SolutionVersionServiceClient
	appClient           asgrpcpb.HotSharedStateApplicationServiceClient
	transferService     partialTransferService
	owsClient           owsgrpcpb.ObjectWorldServiceClient
	casClient           caspb.ContentAddressableStorageServiceClient
	runner              *operations.Runner
	onSolutionUpdate    func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error
	clusterParams       render.ClusterParams
	initDataFilesParams render.InitDataFilesParams
	validator           privileges.Validator
}

type paginatedAssets struct {
	*iapb.ListInstalledAssetsRequest
}

func (r paginatedAssets) ClearPagination() {
	r.PageToken = ""
	r.PageSize = 0
}

func withDefaultCode(err error, code codes.Code) error {
	if _, ok := status.FromError(err); !ok {
		return status.Error(code, err.Error())
	}
	return err
}

func isSkill(rt *rtrpb.ResourceTypeRuntime) bool {
	return rt.GetMetadata().GetAssetType() == atpb.AssetType_ASSET_TYPE_SKILL
}

func typesFromResourceSets(sets ...*rspb.ResourceSet) []string {
	var types []string
	for _, rs := range sets {
		for _, ri := range rs.GetResourceInstances() {
			types = append(types, ri.GetTypeIdVersion())
		}
	}
	return types
}

func typesFromApp(app *apb.Application) []string {
	return typesFromResourceSets(app.GetResources())
}

func deploymentDataFrom(rtr *rtrpb.ResourceTypeRuntime) (*iapb.InstalledAsset_DeploymentData, error) {
	switch rtr.GetMetadata().GetAssetType() {
	case atpb.AssetType_ASSET_TYPE_DATA:
		da, err := runtime.RuntimeToData(rtr)
		if err != nil {
			return nil, err
		}
		return &iapb.InstalledAsset_DeploymentData{
			Variant: &iapb.InstalledAsset_DeploymentData_Data{
				Data: &iapb.InstalledAsset_DataDeploymentData{
					Data: da,
				},
			},
		}, nil
	case atpb.AssetType_ASSET_TYPE_PROCESS:
		pa, err := runtime.RuntimeToProcess(rtr)
		if err != nil {
			return nil, err
		}
		return &iapb.InstalledAsset_DeploymentData{
			Variant: &iapb.InstalledAsset_DeploymentData_Process{
				Process: &iapb.InstalledAsset_ProcessDeploymentData{
					Process: pa,
				},
			},
		}, nil
	case atpb.AssetType_ASSET_TYPE_SCENE_OBJECT:
		som, err := runtime.RuntimeToSceneObject(rtr)
		if err != nil {
			return nil, err
		}
		return &iapb.InstalledAsset_DeploymentData{
			Variant: &iapb.InstalledAsset_DeploymentData_SceneObject{
				SceneObject: &iapb.InstalledAsset_SceneObjectDeploymentData{
					Manifest: som,
				},
			},
		}, nil
	default:
		return nil, nil
	}
}

func viewOrDefault(view viewpb.AssetViewType, defaultView viewpb.AssetViewType) (viewpb.AssetViewType, error) {
	switch view {
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED:
		return defaultView, nil
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC,
		viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL,
		viewpb.AssetViewType_ASSET_VIEW_TYPE_VERSIONS,
		viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL_METADATA,
		viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL:
		return view, nil

	case viewpb.AssetViewType_ASSET_VIEW_TYPE_ALL:
		fallthrough // Intended only for use on internal services, not public ones.

	default:
		return viewpb.AssetViewType_ASSET_VIEW_TYPE_UNSPECIFIED, status.Errorf(codes.InvalidArgument, "unsupported view %q", view)
	}
}

func viewIncludesAssetSpecificMetadata(view viewpb.AssetViewType) bool {
	switch view {
	case viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC,
		viewpb.AssetViewType_ASSET_VIEW_TYPE_VERSIONS:
		return false
	default:
		return true
	}
}

func addAssetSpecificMetadata(rtr *rtrpb.ResourceTypeRuntime, installedAsset *iapb.InstalledAsset) error {
	switch rtr.GetMetadata().GetAssetType() {
	case atpb.AssetType_ASSET_TYPE_DATA:
		protoName, err := names.AnyToProtoName(rtr.GetData().GetData())
		if err != nil {
			return err
		}
		installedAsset.AssetSpecificMetadata = &iapb.InstalledAsset_DataSpecificMetadata{
			DataSpecificMetadata: &iapb.InstalledAsset_DataMetadata{
				ProtoName: protoName,
			},
		}
		return nil
	case atpb.AssetType_ASSET_TYPE_SERVICE:
		installedAsset.AssetSpecificMetadata = &iapb.InstalledAsset_ServiceSpecificMetadata{
			ServiceSpecificMetadata: &iapb.InstalledAsset_ServiceMetadata{
				ServiceProtoPrefixes:    rtr.GetServiceDef().GetServiceProtoPrefixes(),
				ServiceInspectionConfig: rtr.GetServiceDef().GetServiceInspectionConfig(),
			},
		}
		return nil
	case atpb.AssetType_ASSET_TYPE_SKILL:
		installedAsset.AssetSpecificMetadata = &iapb.InstalledAsset_SkillSpecificMetadata{
			SkillSpecificMetadata: &iapb.InstalledAsset_SkillMetadata{
				Details: rtr.GetSkill().GetDetails(),
			},
		}
		return nil
	case atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
		installedAsset.AssetSpecificMetadata = &iapb.InstalledAsset_HardwareDeviceSpecificMetadata{
			HardwareDeviceSpecificMetadata: &iapb.InstalledAsset_HardwareDeviceMetadata{
				ServiceProtoPrefixes:    rtr.GetServiceDef().GetServiceProtoPrefixes(),
				ServiceInspectionConfig: rtr.GetServiceDef().GetServiceInspectionConfig(),
			},
		}
		return nil
	default:
		return nil
	}
}

func convertResourceTypeRuntimeToInstalledAssetView(rtr *rtrpb.ResourceTypeRuntime, view viewpb.AssetViewType) (*iapb.InstalledAsset, error) {
	installedAsset := &iapb.InstalledAsset{
		Name: idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId()),
	}
	// This only returns errors related to invalid view type enums.  We do that
	// already in viewOrDefault, so Validation of the view is done upstream, so
	// ignore it here.
	installedAsset.Metadata, _ = viewutils.MetadataToView(rtr.GetMetadata(), view)

	if viewIncludesAssetSpecificMetadata(view) {
		if err := addAssetSpecificMetadata(rtr, installedAsset); err != nil {
			return nil, err
		}
	}

	if asset, err := runtime.RuntimeToAsset(rtr); err != nil {
		// TODO: b/517345754: Remove when all old-style pose estimators have been
		// removed from all solutions and we can enforce this conversion.
		log.Warningf("Leaving asset field unset because runtime.RuntimeToAsset failed for %q: %v", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()), err)
	} else if view == viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL {
		installedAsset.Asset = asset
	} else {
		installedAsset.Asset = viewutils.BasicAssetView(asset)
	}

	if view == viewpb.AssetViewType_ASSET_VIEW_TYPE_FULL {
		dd, err := deploymentDataFrom(rtr)
		if err != nil {
			return nil, err
		}
		installedAsset.DeploymentData = dd
	}

	return installedAsset, nil
}

type assetInfo struct {
	parts     *idutils.IDVersionParts
	instances []string
}

type solutionState struct {
	// app represents the current application stored in hot shared state.
	app *apb.Application
	// revisionToken marks the state from which app was retrieved, and must be
	// used to update the application.
	revisionToken string
	// installed is a map of id_version to asset information
	installed map[string]*assetInfo
	// id to id_versions
	versions map[string][]string
}

// validateNewOnlyPolicy checks the proposed actions against the add new policy
// and returns a user-surfacable error if it fails.
func validateNewOnlyPolicy(actions *actions) error {
	var idvs []string
	for _, idv := range actions.remove {
		idvs = append(idvs, fmt.Sprintf("%q", idv))
	}
	if len(idvs) == 0 {
		return nil
	}

	return status.Errorf(codes.FailedPrecondition, "add new only policy was selected, but the following versions are installed: %v", strings.Join(idvs, ", "))
}

// validateUpdateUnusedPolicy checks the proposed actions against the update
// unused policy and returns a user-surfacable error if it fails.
func validateUpdateUnusedPolicy(actions *actions, state *solutionState) error {
	var instances []string
	for id, names := range actions.instancesToUpdate {
		idvs := state.versions[id]
		asset := id
		if len(idvs) > 0 {
			asset = idvs[0]
		}
		instances = append(instances, fmt.Sprintf("%s (Asset: %s)", strings.Join(names, ", "), asset))
	}
	if len(instances) == 0 {
		return nil
	}
	return status.Errorf(codes.FailedPrecondition, "update unused policy was selected, but the following instances are in use: %v", strings.Join(instances, "; "))
}

func configMessageFullName(rtr *rtrpb.ResourceTypeRuntime) string {
	switch rtr.GetMetadata().GetAssetType() {
	case atpb.AssetType_ASSET_TYPE_SERVICE, atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
		return rtr.GetServiceDef().GetConfigMessageFullName()
	case atpb.AssetType_ASSET_TYPE_SKILL:
		return rtr.GetSkill().GetDetails().GetParameter().GetMessageFullName()
	default:
		return ""
	}
}

func validateUpdateCompatiblePolicy(actions *actions, state *solutionState, rtrs map[string]*rtrpb.ResourceTypeRuntime) error {
	// Failed precondition is the default code for this function if a failure
	// is detected, but it can be overwritten by a more important error.
	code := codes.FailedPrecondition
	var incompatibilities []string

	for id, names := range actions.instancesToUpdate {
		rtr, ok := actions.addByID.Get(id)
		if !ok {
			return status.Errorf(codes.Internal, "could not find asset id %q when validating update policy", id)
		}
		newType := rtr.GetMetadata().GetAssetType()
		newConfigMsg := configMessageFullName(rtr)
		for _, idv := range state.versions[id] {
			if rtr, ok := rtrs[idv]; !ok {
				return status.Errorf(codes.Internal, "missing type info for %q", idv)
			} else if curType := rtr.GetMetadata().GetAssetType(); curType != newType {
				incompatibilities = append(incompatibilities, fmt.Sprintf("installation of %q (type: %q) would update %v, but the current asset is type %q", id, typeutils.AssetTypeDisplayName(newType), strings.Join(names, ", "), typeutils.AssetTypeDisplayName(curType)))
			} else if curConfigMsg := configMessageFullName(rtr); curConfigMsg != "" && curConfigMsg != newConfigMsg {
				incompatibilities = append(incompatibilities, fmt.Sprintf("installation of %q (config message type: %q) would update %v, but the current asset uses config message type %q", id, newConfigMsg, strings.Join(names, ", "), curConfigMsg))
			}
		}
	}
	// TODO - b/370046738: check that service configurations remain valid with
	// the new asset.
	if len(incompatibilities) == 0 {
		return nil
	}
	return status.Errorf(code, "update compatible policy was selected, but the following incompatibilities were found: %v", strings.Join(incompatibilities, ", "))
}

// validatePolicy checks the proposed actions against the user selected policy.
func validatePolicy(policy iapb.UpdatePolicy, actions *actions, state *solutionState, rtrs map[string]*rtrpb.ResourceTypeRuntime) error {
	switch policy {
	case iapb.UpdatePolicy_UPDATE_POLICY_ADD_NEW_ONLY:
		return validateNewOnlyPolicy(actions)
	case iapb.UpdatePolicy_UPDATE_POLICY_UPDATE_UNUSED:
		return validateUpdateUnusedPolicy(actions, state)
	case iapb.UpdatePolicy_UPDATE_POLICY_UNSPECIFIED:
		// User left this unspecified, so apply some reasonable default.
		fallthrough
	case iapb.UpdatePolicy_UPDATE_POLICY_UPDATE_COMPATIBLE:
		return validateUpdateCompatiblePolicy(actions, state, rtrs)
	default:
		return status.Errorf(codes.Unimplemented, "unrecognized policy: %d", policy)
	}
}

// pruneReferencedAssets removes referenced Assets from `toInstall` if any version of that
// Asset is already installed in the solution.
//
// `toInstall` must contain all Assets, including referenced Assets, that are to be installed.
//
// `referencedAssets` must contain all Assets referenced by Assets in `toInstall`. If an Asset
// was both part of the original request and is also referenced by another Asset in the request,
// the original request must take precedence and it must not be present in `referencedAssets`.
//
// Assets in `referencedAssets` are silently removed from `toInstall` if any version of that Asset
// is already installed in the solution.
func pruneReferencedAssets[V any](s *solutionState, toInstall *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime], referencedAssets map[string]V) {
	for id := range referencedAssets {
		// If it's a referenced asset, we skip it if any version is already installed.
		if _, anyInstalled := s.versions[id]; anyInstalled {
			toInstall.Delete(id)
		}
	}
}

type actions struct {
	// Resources to add to runtimedb
	add []*rtrpb.ResourceTypeRuntime
	// Mapping of ID to resource to add.
	addByID *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime]
	// id_versions to delete from runtimedb
	remove []string
	// id to list of instance names that will be updated
	instancesToUpdate map[string][]string
	// If the Skills chart should be redeployed based on the new application.
	redeploySkills bool
	// If the resources chart should be redeployed based on the new application.
	redeployResources bool
	// If the world should be updated to reflect the new types
	updateWorld bool
	// If the application has been updated and should be sent to HSS
	updateApp bool
	// The updated application, reflecting new id_versions
	app *apb.Application
}

// planActions aggregates the actions that need to be taken to handle the installation and update.
func planActions(s *solutionState, toInstall *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime]) *actions {
	actions := &actions{
		instancesToUpdate: make(map[string][]string),
		addByID:           toInstall,
		app:               proto.Clone(s.app).(*apb.Application),
	}
	for id, rt := range toInstall.Items() {
		actions.add = append(actions.add, rt)
		if isSkill(rt) {
			actions.redeploySkills = true
		}
		newIDV := idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion())
		for _, idv := range s.versions[id] {
			if idv == newIDV {
				continue
			}
			actions.remove = append(actions.remove, idv)
			if asset, ok := s.installed[idv]; ok && len(asset.instances) > 0 {
				actions.instancesToUpdate[id] = append(actions.instancesToUpdate[id], asset.instances...)
			}
		}
		if len(actions.instancesToUpdate[id]) > 0 {
			switch rt.GetMetadata().GetAssetType() {
			case atpb.AssetType_ASSET_TYPE_SCENE_OBJECT:
				actions.updateWorld = true
			case atpb.AssetType_ASSET_TYPE_SERVICE:
				actions.redeployResources = true
			case atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
				actions.redeployResources = true
				actions.updateWorld = true
			default:
			}
		}
		// Remove anything that starts with the id we're installing from the catalog skills.
		if skills := actions.app.GetSkills(); skills != nil {
			skills.SkillIdVersions = slices.DeleteFunc(skills.SkillIdVersions, func(idv string) bool {
				parts, err := idutils.NewIDVersionParts(idv)
				if err != nil {
					log.Warningf("invalid skill %q, leaving untouched in application proto", idv)
					// Presume that we can't compare equality to an invalid id
					// version, so just leave that there. Realistically, this
					// should trigger a failure in deployment, so we shouldn't
					// see this.
					return false
				}
				return proto.Equal(parts.IDProto(), rt.GetMetadata().GetIdVersion().GetId())
			})
		}
		if rt.GetMetadata().GetAssetType() == atpb.AssetType_ASSET_TYPE_SKILL && rt.GetInstallationOrigin() == iopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG {
			if skills := actions.app.GetSkills(); skills == nil {
				actions.app.Skills = new(apb.Application_Skills)
			}
			actions.app.GetSkills().SkillIdVersions = append(actions.app.GetSkills().SkillIdVersions, idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion()))
		}

		// Remove anything with the same ID from the assets field.
		if assets := actions.app.Assets; assets != nil {
			// The key is a local name, and therefore irrelevant to the comparison.
			maps.DeleteFunc(assets, func(_ string, asset *apb.Application_Asset) bool {
				return proto.Equal(asset.GetCatalog().GetId(), rt.GetMetadata().GetIdVersion().GetId())
			})
		}
		// Add assets to the assets field if installed from catalog. This way, installed assets that
		// do not have instances, can still be represented in the application.
		// Sideloaded assets are represented outside the application proto, in the modified solution.
		if rt.GetInstallationOrigin() == iopb.InstallationOrigin_INSTALLATION_ORIGIN_CATALOG {
			assets := actions.app.GetAssets()
			if assets == nil {
				assets = make(map[string]*apb.Application_Asset)
			}

			assets[id] = &apb.Application_Asset{
				Variant: &apb.Application_Asset_Catalog{
					Catalog: rt.GetMetadata().GetIdVersion(),
				},
			}
			actions.app.Assets = assets
		}
	}
	for _, ri := range actions.app.GetResources().GetResourceInstances() {
		if rt, replace := toInstall.Get(s.installed[ri.GetTypeIdVersion()].parts.ID()); replace {
			ri.TypeIdVersion = idutils.IDVersionFromProtoUnchecked(rt.GetMetadata().GetIdVersion())
		}
	}
	actions.updateApp = len(actions.instancesToUpdate) > 0 || !slices.Equal(actions.app.GetSkills().GetSkillIdVersions(), s.app.GetSkills().GetSkillIdVersions()) || !maps.Equal(actions.app.GetAssets(), s.app.GetAssets())
	return actions
}

func (s *installedAssetsService) ListInstalledAssets(ctx context.Context, req *iapb.ListInstalledAssetsRequest) (*iapb.ListInstalledAssetsResponse, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.ListInstalledAssets")
	defer span.End()

	view, err := viewOrDefault(req.GetView(), viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL)
	if err != nil {
		return nil, err
	}
	req.View = view

	pageSize, err := resourceregistryutil.PageSize(req)
	if err != nil {
		return nil, err
	}

	startAfters, err := resourceregistryutil.StartAfters(paginatedAssets{req})
	if err != nil {
		return nil, err
	}

	rtrs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient)
	if err != nil {
		log.ErrorContextf(ctx, "resourcetyperuntime.GetAll() failed: %v", err)
		return nil, err
	}

	type assetWithRTR struct {
		asset *iapb.InstalledAsset
		rtr   *rtrpb.ResourceTypeRuntime
	}
	var objects []assetWithRTR
	for _, rtr := range rtrs {
		installedAsset, err := convertResourceTypeRuntimeToInstalledAssetView(rtr, view)
		if err != nil {
			log.ErrorContextf(ctx, "convertResourceTypeRuntimeToInstalledAssetView failed: %v", err)
			return nil, err
		}
		// Preserve the runtime object for the filtering below since the converted
		// asset is missing some of the required metadata in the BASIC view.
		objects = append(objects, assetWithRTR{installedAsset, rtr})
	}

	// Key-generating function to use for sorting assets.
	var sortKeyFrom func(o *iapb.InstalledAsset) []string
	switch req.GetOrderBy() {
	case searchpb.OrderBy_ORDER_BY_UNSPECIFIED, searchpb.OrderBy_ORDER_BY_DISPLAY_NAME:
		sortKeyFrom = func(o *iapb.InstalledAsset) []string {
			key := []string{strings.ToLower(o.GetMetadata().GetDisplayName())}
			key = append(key, resourceregistryutil.IDVersionAsKey(o)...)
			return key
		}
	case searchpb.OrderBy_ORDER_BY_ID:
		sortKeyFrom = func(o *iapb.InstalledAsset) []string {
			return resourceregistryutil.IDVersionAsKey(o)
		}
	default:
		return nil, status.Errorf(codes.InvalidArgument, "invalid order by: %v", req.GetOrderBy())
	}

	// Function to compare keys, depending on whether we're sorting ascending or descending.
	var cmpKeys func(lhs, rhs []string) int
	if req.GetSortDescending() {
		cmpKeys = func(lhs, rhs []string) int {
			return slices.Compare(rhs, lhs)
		}
	} else {
		cmpKeys = func(lhs, rhs []string) int {
			return slices.Compare(lhs, rhs)
		}
	}

	// Filter out assets that come before the current page.
	if len(startAfters) > 0 {
		objects = slices.DeleteFunc(objects, func(a assetWithRTR) bool {
			return cmpKeys(sortKeyFrom(a.asset), startAfters) <= 0
		})
	}

	// Filter out assets that don't match the asset type filter.
	assetTypesFilter := req.GetStrictFilter().GetAssetTypes()
	if req.GetStrictFilter().GetAssetType() != atpb.AssetType_ASSET_TYPE_UNSPECIFIED {
		if len(assetTypesFilter) > 0 {
			return nil, status.Errorf(codes.InvalidArgument, "cannot specify both asset_type and asset_types")
		}
		assetTypesFilter = []atpb.AssetType{req.GetStrictFilter().GetAssetType()}
	}
	if len(assetTypesFilter) > 0 {
		objects = slices.DeleteFunc(objects, func(a assetWithRTR) bool {
			return !slices.Contains(assetTypesFilter, a.asset.GetMetadata().GetAssetType())
		})
	}

	var hasProtocolPrefixes bool
	var missingProtocolPrefixes bool
	for _, p := range req.GetStrictFilter().GetProvides() {
		if err := interfaceutils.ValidateInterfaceName(p); err != nil {
			missingProtocolPrefixes = true
		} else {
			hasProtocolPrefixes = true
		}
	}
	if hasProtocolPrefixes && missingProtocolPrefixes {
		return nil, status.Errorf(codes.InvalidArgument, "provides contains both URI and non-URI interfaces")
	}

	for _, p := range req.GetStrictFilter().GetProvides() {
		if hasProtocolPrefixes {
			objects = slices.DeleteFunc(objects, func(a assetWithRTR) bool {
				return !slices.ContainsFunc(a.rtr.GetMetadata().GetProvides(), func(i *mdpb.Interface) bool {
					return i.GetUri() == p
				})
			})
		}

		if missingProtocolPrefixes {
			objects = slices.DeleteFunc(objects, func(a assetWithRTR) bool {
				switch a.asset.GetMetadata().GetAssetType() {
				case atpb.AssetType_ASSET_TYPE_SERVICE, atpb.AssetType_ASSET_TYPE_HARDWARE_DEVICE:
					return !slices.ContainsFunc(a.rtr.GetServiceDef().GetServiceProtoPrefixes(), func(prefix string) bool {
						return strings.Trim(prefix, "/") == p
					})
				case atpb.AssetType_ASSET_TYPE_DATA:
					protoName, err := names.AnyToProtoName(a.rtr.GetData().GetData())
					if err != nil {
						log.ErrorContextf(ctx, "Could not get proto name for data asset %v: %v", a.rtr.GetMetadata().GetIdVersion(), err)
						return true
					}
					return protoName != p
				default:
					return true
				}
			})
		}
	}

	// Filter out assets that don't match the asset tag filter.
	if req.GetStrictFilter().GetAssetTag() != atagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
		objects = slices.DeleteFunc(objects, func(a assetWithRTR) bool {
			// Only the runtime object has the asset tag independently of the
			// requested view.
			return req.GetStrictFilter().GetAssetTag() != a.rtr.GetMetadata().GetAssetTag()
		})
	}

	// Sort the assets.
	slices.SortFunc(objects, func(lhs, rhs assetWithRTR) int {
		return cmpKeys(sortKeyFrom(lhs.asset), sortKeyFrom(rhs.asset))
	})

	installedAssets := make([]*iapb.InstalledAsset, len(objects))
	for i, o := range objects {
		installedAssets[i] = o.asset
	}
	if len(installedAssets) <= int(pageSize) {
		return &iapb.ListInstalledAssetsResponse{
			InstalledAssets: installedAssets,
		}, nil
	}

	installedAssets = installedAssets[:pageSize]
	paginatedAssets{req}.ClearPagination()

	pt, err := pagetoken.Opacify(req, "", sortKeyFrom(installedAssets[pageSize-1]))
	if err != nil {
		return nil, status.Error(codes.Internal, "could not generate next page token")
	}
	return &iapb.ListInstalledAssetsResponse{
		InstalledAssets: installedAssets,
		NextPageToken:   pt,
	}, nil
}

func (s *installedAssetsService) GetInstalledAsset(ctx context.Context, req *iapb.GetInstalledAssetRequest) (*iapb.InstalledAsset, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.GetInstalledAsset")
	defer span.End()

	reqID, err := idutils.IDFromProto(req.GetId())
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "%v", err)
	}
	view, err := viewOrDefault(req.GetView(), viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL)
	if err != nil {
		return nil, err
	}
	req.View = view

	rtr, err := resourcetyperuntime.GetFromID(ctx, s.rtrClient, reqID)
	if err != nil {
		return nil, err
	}

	return convertResourceTypeRuntimeToInstalledAssetView(rtr, view)
}

func (s *installedAssetsService) BatchGetInstalledAssets(ctx context.Context, req *iapb.BatchGetInstalledAssetsRequest) (*iapb.BatchGetInstalledAssetsResponse, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.BatchGetInstalledAssets")
	defer span.End()

	if len(req.GetIds()) == 0 {
		return nil, status.Error(codes.InvalidArgument, "No installed assets were requested")
	}

	reqIDs := make(map[string]struct{})
	for _, idp := range req.GetIds() {
		id, err := idutils.IDFromProto(idp)
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "%v", err)
		}
		reqIDs[id] = struct{}{}
	}

	view, err := viewOrDefault(req.GetView(), viewpb.AssetViewType_ASSET_VIEW_TYPE_DETAIL)
	if err != nil {
		return nil, err
	}
	req.View = view

	allIDVs, err := s.rtrClient.List(ctx)
	if err != nil {
		return nil, fmt.Errorf("could not list runtime info for assets: %v", err)
	}

	idToMatchingIDV := make(map[string]string)
	for _, idv := range allIDVs {
		id, err := idutils.RemoveVersionFrom(idv)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "invalid asset in database: %q", idv)
		}
		if _, ok := reqIDs[id]; ok {
			if _, ok := idToMatchingIDV[id]; ok {
				return nil, status.Errorf(codes.Internal, "found more than one asset that could match %q", id)
			}
			idToMatchingIDV[id] = idv
		}
	}

	idvsToGet := make([]string, 0, len(idToMatchingIDV))
	for _, idp := range req.GetIds() {
		id := idutils.IDFromProtoUnchecked(idp)
		idv, ok := idToMatchingIDV[id]
		if !ok {
			return nil, status.Errorf(codes.NotFound, "requested id %q not found", id)
		}
		idvsToGet = append(idvsToGet, idv)
	}

	rtrs, err := s.rtrClient.BatchGet(ctx, idvsToGet)
	if err != nil {
		return nil, fmt.Errorf("could not find runtime info for assets %v: %v", idvsToGet, err)
	}

	installedAssets := make([]*iapb.InstalledAsset, 0, len(rtrs))
	for _, rtr := range rtrs {
		asset, err := convertResourceTypeRuntimeToInstalledAssetView(rtr, view)
		if err != nil {
			return nil, err
		}
		installedAssets = append(installedAssets, asset)
	}

	return &iapb.BatchGetInstalledAssetsResponse{
		InstalledAssets: installedAssets,
	}, nil
}

// summarizeState gathers information about the current state of the solution
// against which actions can be generated.
func (s *installedAssetsService) summarizeState(ctx context.Context) (*solutionState, error) {
	idvs, err := s.rtrClient.List(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to list assets: %v", err)
	}

	installed := make(map[string]*assetInfo)
	versions := make(map[string][]string)
	for _, idv := range idvs {
		parts, err := idutils.NewIDVersionParts(idv)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "invalid state in cache: %v", err)
		}
		installed[idv] = &assetInfo{
			parts: parts,
		}
		versions[parts.ID()] = append(versions[parts.ID()], idv)
	}

	resp, err := s.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to get current state of the solution: %v", err)
	}
	app := resp.GetApplication()
	revisionToken := resp.GetRevisionToken()

	for _, ri := range app.GetResources().GetResourceInstances() {
		if ai, ok := installed[ri.GetTypeIdVersion()]; ok {
			ai.instances = append(ai.instances, ri.GetName())
		} else {
			return nil, status.Errorf(codes.Internal, "solution is in an invalid state as %q points to %q which is not installed", ri.GetName(), ri.GetTypeIdVersion())
		}
	}

	return &solutionState{app, revisionToken, installed, versions}, nil
}

// createWorld constructs a temporary new world using the object world updates
// decomposed from the old world and applying them to a new world with the new
// resource set.  The new world id is returned to the caller and must be
// deleted by the caller eventually.
func (s *installedAssetsService) createWorld(ctx context.Context, oldWorldID string, oldRS *rspb.ResourceSet, newRS *rspb.ResourceSet, rtrs map[string]*rtrpb.ResourceTypeRuntime) (string, error) {
	log.InfoContextf(ctx, "Grabbing the current state of the world")
	oldGrid, err := resourcereader.ExtractGeometricResourceInstanceData(oldRS, rtrs)
	if err != nil {
		log.ErrorContextf(ctx, "ExtractGeometricResourceInstanceData failed: %v", err)
		return "", status.Error(codes.Internal, "unable to get geometric information about current instances")
	}

	var curUpdates *owupb.ObjectWorldUpdates
	if resp, err := s.owsClient.ExtractResourceInstances(ctx, &owspb.ExtractResourceInstancesRequest{
		WorldId:              oldWorldID,
		ResourceInstanceData: oldGrid,
	}); err != nil {
		log.ErrorContextf(ctx, "owsClient.ExtractGeometricResourceInstance failed: %v", err)
		return "", status.Error(codes.Internal, "unable to get geometric information about current world from the world service")
	} else {
		// Note that we're not writing back into the resource set.  This helps
		// preserve the frontend's "restore" behavior.  It's unclear if this is
		// the correct approach or not.  Also note that there may be updates in
		// here that could overwrite any changes from the new type, such as
		// system or application limits.  See b/371572021 for more detail.
		curUpdates = resp.GetUpdateWorldResources()
	}

	log.InfoContextf(ctx, "Updating the world to reflect the new assets")
	newGrid, err := resourcereader.ExtractGeometricResourceInstanceData(newRS, rtrs)
	if err != nil {
		log.ErrorContextf(ctx, "ExtractGeometricResourceInstanceData failed: %v", err)
		return "", status.Error(codes.Internal, "unable to get geometric information for updated instances")
	}
	if resp, err := s.owsClient.CreateWorldFromResourceInstances(ctx, &owspb.CreateWorldFromResourceInstancesRequest{
		ResourceInstanceData: newGrid,
		UpdateWorldResources: curUpdates,
	}); err != nil {
		log.ErrorContextf(ctx, "owsClient.CreateWorldFromResourceInstances failed: %v", err)
		return "", statusutil.Wrap(err, "unable to create a world with the updated assets")
	} else {
		return resp.GetId(), nil
	}
}

// redeployCharts sends a workcell spec for a given application to the
// transfer service that will update the resources chart, the skills charts,
// and the associated data files.
func (s *installedAssetsService) redeployCharts(ctx context.Context, app *apb.Application, redeployResources, redeploySkills bool, rtrs map[string]*rtrpb.ResourceTypeRuntime) error {
	// The initial workcell spec, with the application chart is deployed by the
	// deploy service.  We don't want to modify that deployment, however, we
	// need to update the resources chart and add new data files, as well as
	// preserve current ones.  The transfer service will simply update any
	// non-exclusive chart that is provided, however, it will garbage collect
	// any files that haven't been touched.  That is why we send all of the
	// data files in addition to the new resources chart.  If we wanted to be
	// "more correct", we could get the currently deployed workcell spec from
	// the transfer service, however, it does not store the currently deployed
	// spec and it's not actually super clear if it should in multi-chart
	// scenarios.  It's not actually enforcing that an exclusive chart is being
	// deployed every time.
	ws := new(tpb.WorkcellSpec)
	if redeployResources {
		simulated, err := render.IsSimulated(app)
		if err != nil {
			log.ErrorContextf(ctx, "Failed installation when trying to detect simulation state: %v", err)
			return status.Error(codes.Internal, "operation mode in saved solution state is invalid")
		}
		if err := render.AddResourceChart(ws, render.ResourceChartOptions{
			Instances:           app.GetResources().GetResourceInstances(),
			Types:               rtrs,
			Parent:              appChartName,
			Simulated:           simulated,
			ClusterParams:       s.clusterParams,
			InitDataFilesParams: s.initDataFilesParams,
		}); err != nil {
			log.ErrorContextf(ctx, "Failed when adding the resource chart to the workcell spec: %v", err)
			return status.Error(codes.Internal, "unable to prep the services for deployment")
		}
	}

	if redeploySkills {
		var skills []*render.SkillDeploymentRuntime
		for _, rtr := range rtrs {
			if isSkill(rtr) {
				if sdd := render.ResourceTypeRuntimeToSkillDeploymentData(rtr); sdd != nil {
					skills = append(skills, sdd)
				}
			}
		}
		if err := render.AddSkillChart(ws, render.SkillChartOptions{
			Skills:       skills,
			Parent:       appChartName,
			NumSkillPods: s.clusterParams.NumSkillPods,
		}); err != nil {
			log.ErrorContextf(ctx, "AddSkillChart(%v) failed: %v", skills, err)
			return status.Error(codes.Internal, "unable to prep skills for deployment")
		}
	}

	// TODO - b/370333350: Use k8s directly instead of transfer service.
	log.InfoContextf(ctx, "Applying a workcell spec on the transfer service")
	if err := s.transferService.ApplyWorkcellSpec(ctx, ws); err != nil {
		log.ErrorContextf(ctx, "ApplyWorkcellSpec(%v) failed: %v", ws, err)
		return status.Errorf(codes.Internal, "failed to deploy the services")
	}
	return nil
}

// setMetadataFn is a callback to propagate metadata (e.g. dependency validation errors)
// back to the LRO. It accepts arguments that will be used to construct the LRO metadata.
type setMetadataFn func(warnings *statuspb.ExtendedStatus)

// installAssets installs the provided Assets according to the given policy.
//
// toInstall is a map of id to the resource type runtime message that should be installed for that
// id.
func (s *installedAssetsService) installAssets(
	ctx context.Context,
	policy iapb.UpdatePolicy,
	toInstall *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime],
	referencedToInstall map[string]*rtrpb.ResourceTypeRuntime,
	mustExist map[string]struct{},
	setMetadata setMetadataFn,
) error {
	currentRTRs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient)
	if err != nil {
		return status.Errorf(codes.Internal, "failed to fetch current runtime types: %v", err)
	}

	// Combine current and new RTRs.
	rtrs := make(map[string]*rtrpb.ResourceTypeRuntime)
	for _, rtr := range currentRTRs {
		rtrs[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtr
	}
	for rtr := range toInstall.Values() {
		rtrs[idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())] = rtr
	}

	state, err := s.summarizeState(ctx)
	if err != nil {
		return err
	}
	for id := range toInstall.Keys() {
		if _, ok := mustExist[id]; ok {
			if _, ok := state.versions[id]; !ok {
				return status.Errorf(codes.NotFound, "requested update of %q which is not installed", id)
			}
		}
	}
	pruneReferencedAssets(state, toInstall, referencedToInstall)
	actions := planActions(state, toInstall)

	if err := validatePolicy(policy, actions, state, rtrs); err != nil {
		return err
	}

	expectedRTRs := maps.Clone(rtrs)
	// Remove RTRs that are being deleted (for example, when updating an Asset version,
	// the previous version is deleted).
	// `actions.add` is not required here since they're already accounted for above
	// when populating `rtrs` from `toInstall`.
	for _, idv := range actions.remove {
		delete(expectedRTRs, idv)
	}
	es, err := s.validateDependenciesAtInstall(ctx, actions.app, expectedRTRs, toInstall)
	if err != nil {
		// TODO(b/536078799): As a temporary measure, we only log the errors here instead of fatally
		// returning them. Once we are reasonably confident that this won't cause breakages for
		// existing users, we should change this to return the error here.
		log.ErrorContextf(ctx, "dependency validation failed during installation: %v", err)
	} else if es != nil {
		setMetadata(es)
	}

	tx := transaction.NewUnordered()
	if actions.updateWorld {
		newWorldID, err := s.createWorld(ctx, initWorldID, state.app.GetResources(), actions.app.GetResources(), rtrs)
		if err != nil {
			return err
		}
		tx.Add(func(ctx context.Context) error {
			if _, err := s.owsClient.SwapWorld(ctx, &owspb.SwapWorldRequest{
				WorldId:       newWorldID,
				TargetWorldId: initWorldID,
			}); err != nil {
				log.ErrorContextf(ctx, "owsClient.SwapWorld failed: %v", err)
				return status.Error(codes.Internal, "unable to set the world based on the new assets")
			}
			return nil
		}, transaction.WithCleanup(func(ctx context.Context) error {
			if _, err := s.owsClient.DeleteWorld(ctx, &owspb.DeleteWorldRequest{
				WorldId: newWorldID,
			}); err != nil {
				// This probably doesn't need to be a fatal error, as the world is in
				// the state that we want and this could get cleaned up manually or
				// handled in some other fashion.
				log.ErrorContextf(ctx, "owsClient.DeleteWorld(%q) failed: %v", newWorldID, err)
				return status.Error(codes.Internal, "unable to clean up after world operations")
			}
			return nil
		}), transaction.WithName("update_world"))
	}

	if actions.redeployResources || actions.redeploySkills {
		tx.Add(func(ctx context.Context) error {
			if err := s.redeployCharts(ctx, actions.app, actions.redeployResources, actions.redeploySkills, expectedRTRs); err != nil {
				return err
			}
			return nil
		}, transaction.WithName("redeploy_charts"))
	}

	if actions.updateApp {
		tx.Add(func(ctx context.Context) error {
			if _, err := s.appClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
				Application:   actions.app,
				RevisionToken: state.revisionToken,
			}); err != nil {
				log.ErrorContextf(ctx, "SetCurrentApplication(%v, %v) failed after deployment: %v", actions.app, state.revisionToken, err)
				return status.Error(codes.Internal, "unable to save application state")
			}
			return nil
		}, transaction.WithName("update_application"))
	}

	if len(actions.add) > 0 {
		tx.Add(func(ctx context.Context) error {
			var idvsToPut []string
			for _, rtr := range actions.add {
				idvsToPut = append(idvsToPut, idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()))
			}
			log.InfoContextf(ctx, "installing %v", strings.Join(idvsToPut, ","))
			if err := s.rtrClient.BatchPut(ctx, actions.add); err != nil {
				log.ErrorContextf(ctx, "BatchPut(ctx, [%v]) failed after deployment: %v", strings.Join(idvsToPut, ","), err)
				return status.Errorf(codes.Internal, "could not add runtime info for assets: %v", err)
			}
			return nil
		}, transaction.WithName("runtimedb_add"))
	}

	if len(actions.remove) > 0 {
		tx.Add(func(ctx context.Context) error {
			log.InfoContextf(ctx, "uninstalling %v", strings.Join(actions.remove, ","))
			if err := s.rtrClient.BatchDelete(ctx, actions.remove); err != nil {
				return status.Errorf(codes.Internal, "could not remove runtime info for assets: %v", err)
			}
			return nil
		}, transaction.WithName("runtimedb_delete"))
	}

	if err := tx.Commit(ctx); err != nil {
		return err
	}

	if currentRTRs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient); err != nil {
		log.WarningContextf(ctx, "failed to get runtime types for onSolutionUpdate: %v", err)
	} else if err := s.onSolutionUpdate(ctx, actions.app, currentRTRs); err != nil {
		log.WarningContextf(ctx, "onSolutionUpdate failed during install: %v", err)
	}

	return nil
}

func (s *installedAssetsService) validateDependenciesAtInstall(ctx context.Context, app *apb.Application, rtrs map[string]*rtrpb.ResourceTypeRuntime, toInstall *orderedmap.OrderedMap[string, *rtrpb.ResourceTypeRuntime]) (*statuspb.ExtendedStatus, error) {
	sc, err := runtimegraph.NewSolutionContext(
		ctx,
		app,
		slices.Collect(maps.Values(rtrs)),
		runtimegraph.WithPlatformRuntime(),
	)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to build solution context for validation: %v", err)
	}

	var installedIDs []string
	for id := range toInstall.Keys() {
		installedIDs = append(installedIDs, id)
	}

	r := report.New(report.AsWarningIfType[error]())
	valOpts := []graph.ValidateGraphOption{
		graph.WithReport(r),
	}
	if len(installedIDs) > 0 {
		valOpts = append(valOpts, graph.WithOnlyReferencesTo(installedIDs...))
	}

	if err := graph.ValidateAll(ctx, sc, valOpts...); err != nil {
		return nil, fmt.Errorf("dependency validation failed: %w", err)
	}
	var es *statuspb.ExtendedStatus
	if len(r.Warnings()) > 0 {
		log.WarningContextf(ctx, "dependencies validation warnings: %v", r.Warnings())
		es = r.ToExtendedStatus(
			report.WithTitlePrefix("Dependency validation errors during Asset installation"),
		).Proto()
	}
	return es, nil
}

func appendAssetsToInstall(toInstall map[string]*rtrpb.ResourceTypeRuntime, rtrs []*rtrpb.ResourceTypeRuntime) error {
	for _, rtr := range rtrs {
		id := idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId())
		if rtrExisting, exists := toInstall[id]; exists {
			versionExisting := rtrExisting.GetMetadata().GetIdVersion().GetVersion()
			version := rtr.GetMetadata().GetIdVersion().GetVersion()
			return status.Errorf(codes.InvalidArgument, "multiple versions provided for %q (%s and %s)", id, versionExisting, version)
		}
		toInstall[id] = rtr
	}

	return nil
}

type createOpts struct {
	// metadataFrom returns the metadata message of the LRO from its contents -- extended status
	// in this case, for representing dependency validation errors.
	metadataFrom     func(warnings *statuspb.ExtendedStatus) proto.Message
	returnConversion func(*iapb.CreateInstalledAssetsResponse) proto.Message
}

// createInstalledAssets is an internal handler for asset creation functions.  This supports singular and batch methods.  Normally the singular method could call the batch method internally and the convert before returning, but structure is slightly different as the conversion function must be delayed until the long running operation is complete.
func (s *installedAssetsService) createInstalledAssets(ctx context.Context, req *iapb.CreateInstalledAssetsRequest, opts createOpts) (*lropb.Operation, error) {
	// Keep track of the order of Assets as provided in the request in a slice.
	var toInstallIDs []string

	var local []*rtrpb.ResourceTypeRuntime
	pullFromCatalog := make(map[string]*idpb.IdVersion)
	// Map from Solution IDs to map from IDVersion strings to IDVersion protos.
	pullFromSolutions := make(map[string]map[string]*idpb.IdVersion)
	for _, a := range req.GetAssets() {
		switch v := a.GetVariant().(type) {
		case *iapb.CreateInstalledAssetsRequest_Asset_Catalog:
			idv, err := idutils.IDVersionFromProto(v.Catalog)
			if err != nil {
				return nil, status.Errorf(codes.InvalidArgument, "invalid Asset id_version for catalog request: %v", err)
			}
			if _, exists := pullFromCatalog[idv]; exists {
				return nil, status.Errorf(codes.InvalidArgument, "duplicate catalog requests for %q", idv)
			}
			pullFromCatalog[idv] = v.Catalog
			toInstallIDs = append(toInstallIDs, idutils.IDFromProtoUnchecked(v.Catalog.GetId()))
		case *iapb.CreateInstalledAssetsRequest_Asset_SolutionAsset:
			id, err := idutils.IDProtoFromString(v.SolutionAsset.GetName())
			if err != nil {
				return nil, status.Errorf(codes.InvalidArgument, "invalid Asset name %q in Solution %q (it should be an Asset ID): %v", v.SolutionAsset.GetName(), v.SolutionAsset.GetBranchId(), err)
			}
			pullFromSolution, ok := pullFromSolutions[v.SolutionAsset.GetBranchId()]
			if !ok {
				pullFromSolution = make(map[string]*idpb.IdVersion)
				pullFromSolutions[v.SolutionAsset.GetBranchId()] = pullFromSolution
			}
			if _, exists := pullFromSolution[v.SolutionAsset.GetName()]; exists {
				return nil, status.Errorf(codes.InvalidArgument, "duplicate Solution %q requests for %q", v.SolutionAsset.GetBranchId(), v.SolutionAsset.GetName())
			}
			pullFromSolution[v.SolutionAsset.GetName()] = &idpb.IdVersion{
				Id: id,
			}
			toInstallIDs = append(toInstallIDs, v.SolutionAsset.GetName())
		default:
			rtr, err := localconv.LocalInstalledAssetToRuntime(ctx, a,
				runtime.WithACIClient(s.aciClient),
				runtime.WithSkipValidation(), // We'll validate all RTRs below.
			)
			if err != nil {
				return nil, err
			}
			local = append(local, rtr)
			toInstallIDs = append(toInstallIDs, idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId()))
		}
	}

	return s.gatherAndScheduleInstall(ctx, installScheduleOpts{
		opPrefix:          "install",
		policy:            req.GetPolicy(),
		toInstallIDs:      toInstallIDs,
		mustExist:         nil, // Should only be set for update operations.
		local:             local,
		pullFromCatalog:   pullFromCatalog,
		pullFromSolutions: pullFromSolutions,
		metadataFrom:      opts.metadataFrom,
		returnConversion: func(installedAssets []*iapb.InstalledAsset) proto.Message {
			return opts.returnConversion(&iapb.CreateInstalledAssetsResponse{
				InstalledAssets: installedAssets,
			})
		},
	})
}

type installScheduleOpts struct {
	opPrefix          string
	policy            iapb.UpdatePolicy
	toInstallIDs      []string
	mustExist         map[string]struct{}
	local             []*rtrpb.ResourceTypeRuntime
	pullFromCatalog   map[string]*idpb.IdVersion
	pullFromSolutions map[string]map[string]*idpb.IdVersion
	// metadataFrom returns the metadata message of the LRO from its contents -- extended status
	// in this case, for representing dependency validation errors.
	metadataFrom     func(warnings *statuspb.ExtendedStatus) proto.Message
	returnConversion func([]*iapb.InstalledAsset) proto.Message
}

func (s *installedAssetsService) gatherAndScheduleInstall(ctx context.Context, opts installScheduleOpts) (*lropb.Operation, error) {
	// Gather local Assets.
	toInstall := make(map[string]*rtrpb.ResourceTypeRuntime)
	if err := appendAssetsToInstall(toInstall, opts.local); err != nil {
		return nil, err
	}

	// Gather catalog Assets.
	catalogGatherer := gather.FromAssetCatalog(s.aciClient)
	if catalogRTRs, _, err := catalogGatherer(ctx, slices.Collect(maps.Values(opts.pullFromCatalog))); err != nil {
		return nil, err
	} else if err := appendAssetsToInstall(toInstall, catalogRTRs); err != nil {
		return nil, err
	}

	// Gather Assets from Solutions.
	for branchID, pullFromSolution := range opts.pullFromSolutions {
		solutionGatherer := gather.FromSolution(
			gather.WithSolutionACIClient(s.aciClient),
			gather.WithSolutionBranchID(branchID),
			gather.WithSolutionSVSClient(s.svsClient),
		)
		if solutionRTRs, _, err := solutionGatherer(ctx, slices.Collect(maps.Values(pullFromSolution))); err != nil {
			return nil, err
		} else if err := appendAssetsToInstall(toInstall, solutionRTRs); err != nil {
			return nil, err
		}
	}

	// Create an ordered map from the toInstall map to preserve the order of the Assets as provided
	// in the request. When dealing with referenced Assets, if multiple references to the same Asset
	// ID are found with different versions or definitions, the first encountered referenced version
	// for that Asset ID takes precedence.
	orderedToInstall, err := orderedmap.FromMap(toInstall, opts.toInstallIDs)
	if err != nil {
		return nil, err
	}
	referencedAssets, err := references.AddToInstall(ctx, orderedToInstall, s.aciClient)
	if err != nil {
		if errors.Is(err, references.ErrInvalidDependencyResolution) {
			return nil, status.Error(codes.FailedPrecondition, err.Error())
		}
		return nil, err
	}

	// Fix and validate all ResourceTypeRuntimes.
	for id, rtr := range orderedToInstall.Items() {
		// Clear output-only data from behavior trees in Process Assets. Processes
		// can have a significant amount of output-only data (e.g. execution states)
		// that can be multiple times the size of the actual Process definition. We
		// never want this to end up in RuntimeDB.
		//
		// Clearing is compliant with go/aip/203#output-only: "the server must clear
		// out any value in this field and must not throw an error as a result of
		// the presence of a value in this field on input".
		if rtr.GetMetadata().GetAssetType() == atpb.AssetType_ASSET_TYPE_PROCESS {
			if bt := rtr.GetProcess().GetBehaviorTree(); bt != nil {
				if err := fieldbehavior.ClearOutputOnly(bt); err != nil {
					return nil, status.Errorf(codes.Internal, "failed to clear output-only fields: %v", err)
				}
			}
		}

		// Ensure that the RTR meets the requirements of this platform version.
		if err := resourcefix.ResourceTypeRuntime(rtr,
			resourcefix.WithPopulateOldFields(true),
		); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "cannot make %q compatible with the platform: %v", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()), err)
		}

		if err != nil {
			return nil, clientcontext.ErrGRPC(err)
		}
		if err := resourcevalidate.ResourceTypeRuntime(ctx, rtr,
			resourcevalidate.WithDataOptions(
				datavalidate.WithReferencedDataOptions(
					datavalidate.WithCASClient(s.casClient),
					datavalidate.WithDisallowFileReferences(true),
				),
			),
			resourcevalidate.WithSceneObjectOptions(
				sceneobjectvalidate.WithCASClient(s.casClient),
			),
			resourcevalidate.WithServiceOptions(
				// TODO(b/493314391): Remove this once clients are providing Platform-provided services.
				servicevalidate.WithSkipPlatformServicesCheckInFDS(true),
			),
		); err != nil {
			return nil, fmt.Errorf("Asset is invalid: %w", withDefaultCode(err, codes.InvalidArgument))
		}

		// Check the Asset against the permission it or the cluster has to run it.
		if err := s.validator.Validate(rtr); err != nil {
			if _, ok := referencedAssets[id]; ok {
				log.WarningContextf(ctx, "Privilege validation failed: unable to install referenced Asset %q: %v", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()), err)
				orderedToInstall.Delete(id)
				continue
			}
			return nil, status.Errorf(codes.PermissionDenied, "unable to install %q: %v", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion()), err)
		}
	}

	// At this point the logic becomes dependent on the state of the cluster, so handle this within
	// the queue.  There's a single worker, so requests will be handled sequentially.
	op := operations.NewWithPrefix(opts.opPrefix)
	err = s.runner.Schedule(ctx, op, func(ctx context.Context) (proto.Message, error) {
		var setMetadata setMetadataFn = func(es *statuspb.ExtendedStatus) {
			if opts.metadataFrom != nil {
				meta := opts.metadataFrom(es)
				if err := op.SetMetadata(meta); err != nil {
					log.ErrorContextf(ctx, "failed to set metadata: %v", err)
				}
			}
		}
		if err := s.installAssets(ctx, opts.policy, orderedToInstall, referencedAssets, opts.mustExist, setMetadata); err != nil {
			return nil, err
		}

		var installedAssets []*iapb.InstalledAsset
		for rtr := range orderedToInstall.Values() {
			installedAssets = append(installedAssets, &iapb.InstalledAsset{
				Name:     idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId()),
				Metadata: rtr.GetMetadata(),
			})
		}
		return opts.returnConversion(installedAssets), nil
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op.Proto(), nil
}

func (s *installedAssetsService) CreateInstalledAssets(ctx context.Context, req *iapb.CreateInstalledAssetsRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.CreateInstalledAssets")
	defer span.End()

	return s.createInstalledAssets(ctx, req, createOpts{
		metadataFrom: func(es *statuspb.ExtendedStatus) proto.Message {
			return &iapb.CreateInstalledAssetsMetadata{
				Warnings: es,
			}
		},
		returnConversion: func(resp *iapb.CreateInstalledAssetsResponse) proto.Message {
			// Identity function since this is already what we need.
			return resp
		},
	})
}

func (s *installedAssetsService) CreateInstalledAsset(ctx context.Context, req *iapb.CreateInstalledAssetRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.CreateInstalledAsset")
	defer span.End()

	// This conversion is a bit messy because the variant types are different.
	// TODO - b/364732263: change the plural create to a batch create that
	// takes in many singular requests.
	var asset *iapb.CreateInstalledAssetsRequest_Asset
	switch v := req.GetAsset().GetVariant().(type) {
	case *iapb.CreateInstalledAssetRequest_Asset_Catalog:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Catalog{
				Catalog: v.Catalog,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_SolutionAsset:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_SolutionAsset{
				SolutionAsset: v.SolutionAsset,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_Data:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Data{
				Data: v.Data,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_Service:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Service{
				Service: v.Service,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_SceneObject:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_SceneObject{
				SceneObject: v.SceneObject,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_Skill:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Skill{
				Skill: v.Skill,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_HardwareDevice:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_HardwareDevice{
				HardwareDevice: v.HardwareDevice,
			},
		}
	case *iapb.CreateInstalledAssetRequest_Asset_Process:
		asset = &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Process{
				Process: v.Process,
			},
		}
	// Return errors here for unspecified and unsupported so we don't collapse
	// those into the same error in the conversion process.
	case nil:
		return nil, status.Errorf(codes.InvalidArgument, "unspecified variant type")
	default:
		return nil, status.Errorf(codes.InvalidArgument, "unsupported variant type: %T", v)
	}

	return s.createInstalledAssets(ctx, &iapb.CreateInstalledAssetsRequest{
		Assets: []*iapb.CreateInstalledAssetsRequest_Asset{asset},
		Policy: req.Policy,
	}, createOpts{
		metadataFrom: func(es *statuspb.ExtendedStatus) proto.Message {
			return &iapb.CreateInstalledAssetMetadata{
				Warnings: es,
			}
		},
		returnConversion: func(resp *iapb.CreateInstalledAssetsResponse) proto.Message {
			// This function can assume success of the method, so just return
			// the first asset.
			return resp.GetInstalledAssets()[0]
		},
	})
}

type updateOpts struct {
	metadataFrom     func(warnings *statuspb.ExtendedStatus) proto.Message
	returnConversion func(*iapb.BatchUpdateInstalledAssetsResponse) proto.Message
}

func (s *installedAssetsService) batchUpdateInstalledAssets(ctx context.Context, req *iapb.BatchUpdateInstalledAssetsRequest, opts updateOpts) (*lropb.Operation, error) {
	policy := req.GetPolicy()
	if policy == iapb.UpdatePolicy_UPDATE_POLICY_ADD_NEW_ONLY {
		return nil, status.Error(codes.InvalidArgument, "add new only policy is not supported for update")
	}

	var toInstallIDs []string
	mustExist := make(map[string]struct{})
	var local []*rtrpb.ResourceTypeRuntime
	pullFromCatalog := make(map[string]*idpb.IdVersion)

	for _, r := range req.GetRequests() {
		if r.GetPolicy() == iapb.UpdatePolicy_UPDATE_POLICY_ADD_NEW_ONLY {
			return nil, status.Error(codes.InvalidArgument, "add new only policy is not supported for update")
		}
		if r.GetPolicy() != iapb.UpdatePolicy_UPDATE_POLICY_UNSPECIFIED {
			if policy != iapb.UpdatePolicy_UPDATE_POLICY_UNSPECIFIED && policy != r.GetPolicy() {
				return nil, status.Errorf(codes.InvalidArgument, "conflicting update policies specified: %v and %v", policy, r.GetPolicy())
			}
			policy = r.GetPolicy()
		}

		ia := r.GetInstalledAsset()
		if ia == nil {
			return nil, status.Error(codes.InvalidArgument, "installed_asset must be specified")
		}

		var id string
		switch v := ia.GetAsset().GetSource().(type) {
		case *assetpb.Asset_Catalog:
			idv, err := idutils.IDVersionFromProto(v.Catalog.GetIdVersion())
			if err != nil {
				return nil, status.Errorf(codes.InvalidArgument, "invalid Asset id_version for catalog request: %v", err)
			}
			if _, exists := pullFromCatalog[idv]; exists {
				return nil, status.Errorf(codes.InvalidArgument, "duplicate catalog requests for %q", idv)
			}
			pullFromCatalog[idv] = v.Catalog.GetIdVersion()
			id = idutils.IDFromProtoUnchecked(v.Catalog.GetIdVersion().GetId())
		case *assetpb.Asset_Local:
			rtr, err := localconv.ProcessedAssetToRuntime(ctx, v.Local,
				runtime.WithACIClient(s.aciClient),
				runtime.WithSkipValidation(), // We'll validate all RTRs below.
			)
			if err != nil {
				return nil, err
			}
			local = append(local, rtr)
			id = idutils.IDFromProtoUnchecked(rtr.GetMetadata().GetIdVersion().GetId())
		case nil:
			return nil, status.Error(codes.InvalidArgument, "unspecified asset source")
		default:
			return nil, status.Errorf(codes.InvalidArgument, "unsupported asset source: %T", v)
		}

		if ia.GetName() != "" && ia.GetName() != id {
			return nil, status.Errorf(codes.InvalidArgument, "installed_asset.name %q does not match asset ID %q", ia.GetName(), id)
		}
		toInstallIDs = append(toInstallIDs, id)
		if !(req.GetAllowMissing() || r.GetAllowMissing()) {
			mustExist[id] = struct{}{}
		}
	}

	return s.gatherAndScheduleInstall(ctx, installScheduleOpts{
		opPrefix:        "update",
		policy:          policy,
		toInstallIDs:    toInstallIDs,
		mustExist:       mustExist,
		local:           local,
		pullFromCatalog: pullFromCatalog,
		metadataFrom:    opts.metadataFrom,
		returnConversion: func(installedAssets []*iapb.InstalledAsset) proto.Message {
			return opts.returnConversion(&iapb.BatchUpdateInstalledAssetsResponse{
				InstalledAssets: installedAssets,
			})
		},
	})
}

func (s *installedAssetsService) BatchUpdateInstalledAssets(ctx context.Context, req *iapb.BatchUpdateInstalledAssetsRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.BatchUpdateInstalledAssets")
	defer span.End()

	return s.batchUpdateInstalledAssets(ctx, req, updateOpts{
		metadataFrom: func(es *statuspb.ExtendedStatus) proto.Message {
			return &iapb.BatchUpdateInstalledAssetsMetadata{
				Warnings: es,
			}
		},
		returnConversion: func(resp *iapb.BatchUpdateInstalledAssetsResponse) proto.Message {
			return resp
		},
	})
}

func (s *installedAssetsService) UpdateInstalledAsset(ctx context.Context, req *iapb.UpdateInstalledAssetRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.UpdateInstalledAsset")
	defer span.End()

	return s.batchUpdateInstalledAssets(ctx, &iapb.BatchUpdateInstalledAssetsRequest{
		Requests:     []*iapb.UpdateInstalledAssetRequest{req},
		Policy:       req.GetPolicy(),
		AllowMissing: req.GetAllowMissing(),
	}, updateOpts{
		metadataFrom: func(es *statuspb.ExtendedStatus) proto.Message {
			return &iapb.UpdateInstalledAssetMetadata{
				Warnings: es,
			}
		},
		returnConversion: func(resp *iapb.BatchUpdateInstalledAssetsResponse) proto.Message {
			return resp.GetInstalledAssets()[0]
		},
	})
}

// uninstallAssets will remove the assets specified in the idsToDeleteSet from
// the solution based on the provided policy.
func (s *installedAssetsService) uninstallAssets(ctx context.Context, policy iapb.DeleteInstalledAssetsRequest_Policy, idsToDelete map[string]struct{}) error {
	installed, err := s.rtrClient.List(ctx)
	if err != nil {
		return status.Errorf(codes.Internal, "unable to list assets: %v", err)
	}

	installedByID := make(map[string][]*idutils.IDVersionParts)
	for _, idv := range installed {
		parts, err := idutils.NewIDVersionParts(idv)
		if err != nil {
			return status.Errorf(codes.Internal, "invalid state in cache: %v", err)
		}
		installedByID[parts.ID()] = append(installedByID[parts.ID()], parts)
	}

	var idVersionsToDelete []string
	for id := range idsToDelete {
		idvs, ok := installedByID[id]
		if !ok {
			return status.Errorf(codes.NotFound, "requested delete of %q which is not installed", id)
		}
		for _, idv := range idvs {
			idVersionsToDelete = append(idVersionsToDelete, idv.IDVersion())
		}
	}

	app, err := s.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return status.Errorf(codes.Internal, "unable to get current state of the solution: %v", err)
	}
	usedInstalled := make(map[string]*idutils.IDVersionParts)
	for _, ri := range app.GetApplication().GetResources().GetResourceInstances() {
		parts, err := idutils.NewIDVersionParts(ri.GetTypeIdVersion())
		if err != nil {
			return status.Errorf(codes.Internal, "invalid state in cache: %v", err)
		}
		if _, ok := idsToDelete[parts.ID()]; ok {
			usedInstalled[ri.GetName()] = parts
		}
	}

	var newApp *apb.Application
	if skills := app.GetApplication().GetSkills(); skills != nil {
		var updateApp bool
		var idvs []string
		for _, idv := range skills.GetSkillIdVersions() {
			parts, err := idutils.NewIDVersionParts(idv)
			if err != nil {
				log.WarningContextf(ctx, "invalid skill %q, leaving untouched in application proto", idv)
				// Presume that we can't compare equality to an invalid id
				// version, so just leave that there. Realistically, this
				// should trigger a failure in deployment, so we shouldn't
				// see this.
				continue
			}
			if _, ok := idsToDelete[parts.ID()]; ok {
				updateApp = true
			} else {
				idvs = append(idvs, idv)
			}
		}
		if updateApp {
			newApp = proto.Clone(app.GetApplication()).(*apb.Application)
			newApp.GetSkills().SkillIdVersions = idvs
		}
	}

	if assets := app.GetApplication().GetAssets(); assets != nil {
		for id := range idsToDelete {
			delete(assets, id)
		}
		if newApp == nil {
			newApp = proto.Clone(app.GetApplication()).(*apb.Application)
		}
		newApp.Assets = assets
	}

	switch policy {
	case iapb.DeleteInstalledAssetsRequest_POLICY_UNSPECIFIED:
		// User left this unspecified, so apply some reasonable default.
		fallthrough
	case iapb.DeleteInstalledAssetsRequest_POLICY_REJECT_USED:
		if len(usedInstalled) > 0 {
			id2names := make(map[string][]string)
			for name, parts := range usedInstalled {
				id2names[parts.IDVersion()] = append(id2names[parts.ID()], name)
			}
			var instances []string
			for id, names := range id2names {
				instances = append(instances, fmt.Sprintf("%q (Asset: %q)", strings.Join(names, ", "), id))
			}
			return status.Errorf(codes.FailedPrecondition, "update unused policy was selected, but the following instances are in use: %v", strings.Join(instances, "; "))
		}
	default:
		return status.Errorf(codes.Unimplemented, "unrecognized policy: %d", policy)
	}

	if newApp != nil {
		if _, err := s.appClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
			Application:   newApp,
			RevisionToken: app.GetRevisionToken(),
		}); err != nil {
			log.ErrorContextf(ctx, "SetCurrentApplication(%v, %v) failed after deployment: %v", newApp, app.GetRevisionToken(), err)
			return status.Error(codes.Internal, "unable to save application state")
		}
	}

	log.InfoContextf(ctx, "uninstalling %v", strings.Join(idVersionsToDelete, ","))
	if err := s.rtrClient.BatchDelete(ctx, idVersionsToDelete); err != nil {
		return status.Errorf(codes.Internal, "could not remove runtime info for assets: %v", err)
	}

	currentApp := app.GetApplication()
	if newApp != nil {
		currentApp = newApp
	}
	remainingRTRs, err := resourcetyperuntime.GetAll(ctx, s.rtrClient)
	if err != nil {
		log.WarningContextf(ctx, "failed to get remaining runtime types for onSolutionUpdate: %v", err)
	} else {
		if err := s.onSolutionUpdate(ctx, currentApp, remainingRTRs); err != nil {
			log.WarningContextf(ctx, "onSolutionUpdate failed during uninstall: %v", err)
		}
	}

	return nil
}

func (s *installedAssetsService) DeleteInstalledAssets(ctx context.Context, req *iapb.DeleteInstalledAssetsRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.DeleteInstalledAssets")
	defer span.End()

	idsToDelete := make(map[string]struct{})
	for _, idp := range req.GetAssets() {
		id, err := idutils.IDFromProto(idp)
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid id: %v", err)
		}
		if _, ok := idsToDelete[id]; ok {
			return nil, status.Errorf(codes.InvalidArgument, "%q specified multiple times", id)
		}
		idsToDelete[id] = struct{}{}
	}

	// At this point the logic becomes dependent on the state of the cluster,
	// so handle this within the queue.  There's a single worker, so requests
	// will be handled sequentially.
	op, err := s.runner.ScheduleNew(ctx, "delete", func(ctx context.Context) (proto.Message, error) {
		if err := s.uninstallAssets(ctx, req.GetPolicy(), idsToDelete); err != nil {
			return nil, err
		}
		return &emptypb.Empty{}, nil
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "unable to enqueue operation: %v", err)
	}
	return op.Proto(), nil
}

func (s *installedAssetsService) DeleteInstalledAsset(ctx context.Context, req *iapb.DeleteInstalledAssetRequest) (*lropb.Operation, error) {
	ctx, span := trace.StartSpan(ctx, "installed_assets_service.DeleteInstalledAsset")
	defer span.End()

	// Unlike the create function, we don't need to do any conversion on the
	// result, since delete methods always return an Empty proto.
	return s.DeleteInstalledAssets(ctx, &iapb.DeleteInstalledAssetsRequest{
		Assets: []*idpb.Id{
			req.GetAsset(),
		},
		// We're just forcing the conversion, since the enums mirror each other.
		// TODO - b/364732263: use DeletePolicy in a BatchDelete method.
		Policy: iapb.DeleteInstalledAssetsRequest_Policy(int(req.GetPolicy())),
	})
}

type partialTransferService interface {
	ApplyWorkcellSpec(context.Context, *tpb.WorkcellSpec) error
}

// Options are the values required for creating a new installed assets server.
type Options struct {
	ACIClient       acigrpcpb.AssetCatalogInternalClient
	RTRClient       resourcetyperuntime.Client
	AppClient       asgrpcpb.HotSharedStateApplicationServiceClient
	SVSClient       svsgrpcpb.SolutionVersionServiceClient
	OWSClient       owsgrpcpb.ObjectWorldServiceClient
	CASClient       caspb.ContentAddressableStorageServiceClient
	TransferService partialTransferService
	Runner          *operations.Runner
	render.ClusterParams
	render.InitDataFilesParams
	Validator        privileges.Validator
	OnSolutionUpdate func(context.Context, *apb.Application, []*rtrpb.ResourceTypeRuntime) error
}

// New creates a new implementation of an installed assets server using opts.
func New(opts Options) iagrpcpb.InstalledAssetsServer {
	return &installedAssetsService{
		aciClient:           opts.ACIClient,
		rtrClient:           opts.RTRClient,
		appClient:           opts.AppClient,
		svsClient:           opts.SVSClient,
		owsClient:           opts.OWSClient,
		casClient:           opts.CASClient,
		transferService:     opts.TransferService,
		runner:              opts.Runner,
		onSolutionUpdate:    opts.OnSolutionUpdate,
		clusterParams:       opts.ClusterParams,
		initDataFilesParams: opts.InitDataFilesParams,
		validator:           opts.Validator,
	}
}
