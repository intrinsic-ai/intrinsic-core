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

// Package resourcewriter contains an internal service to manage
// resource sets and individual resource instances within the context of a
// deployed PPR application.
package resourcewriter

import (
	"context"
	"fmt"
	"slices"

	"intrinsic/assets/deploy/privileges"
	"intrinsic/assets/deploy/resourcefixer"
	"intrinsic/assets/idutils"
	"intrinsic/assets/install/gather"
	"intrinsic/assets/typeutils"
	"intrinsic/kubernetes/workcell_spec/runtimedbtransfer"
	"intrinsic/resources/service/resourcereader"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/util/proto/descriptorcompatibility"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	acigrpcpb "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto"
	idpb "intrinsic/assets/proto/id_go_proto"
	datafilespb "intrinsic/config/proto/data_files_go_proto"
	rspb "intrinsic/config/proto/resource_set_go_proto"
	ppb "intrinsic/math/proto/pose_go_proto"
	icpb "intrinsic/resources/proto/resource_instance_go_proto"
	socpb "intrinsic/scene/proto/v1/scene_object_config_go_proto"
	asgrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	owrpb "intrinsic/world/public/proto/object_world_refs_go_proto"
	grpcowspb "intrinsic/world/public/proto/object_world_service_go_proto"
	owspb "intrinsic/world/public/proto/object_world_service_go_proto"
	owupb "intrinsic/world/public/proto/object_world_updates_go_proto"

	apb "google.golang.org/protobuf/types/known/anypb"
)

var (
	// ErrProductAlreadyExists occurs when trying to create a product that
	// already exists.
	ErrProductAlreadyExists = errors.New("product already exists")
	// ErrInvalidProductPartName occurs when using an invalid product part name.
	// The name should start with a letter and may contain only letters, digits
	// and underscores.
	ErrInvalidProductPartName = errors.New("invalid product part name")
	// ErrMissingGeometry occurs when a product is missing geometry.
	ErrMissingGeometry = errors.New("missing geometry")

	// ErrParentSetMissingWorldFragment occurs when a resource's parent was set
	// but the resource is missing geometry.
	ErrParentSetMissingWorldFragment = errors.New("parent was set but resource type doesn't contain a world fragment")
	// ErrResourceInstanceAlreadyExists occurs when trying to add a resource whose
	// name already exists.
	ErrResourceInstanceAlreadyExists = errors.New("resource instance already exists")
	// ErrResourceNotFound occurs when the specified resource could not be found.
	ErrResourceNotFound = errors.New("resource not found in resource set")
	// ErrResourceTypeNotFound occurs when the specified resource type is not in
	// the catalog.
	ErrResourceTypeNotFound = errors.New("resource type not found in resource catalog")
	// ErrInvalidResourceTypeID occurs when an invalid resource type id was given
	ErrInvalidResourceTypeID = errors.New("invalid resource type ID")
	// ErrInvalidWorldFragmentUpdate occurs when the user attempts to update
	// the world fragment on a type that does not already contain a world
	// fragment.
	ErrInvalidWorldFragmentUpdate = errors.New("world fragment cannot be updated on a resource type that does not contain a world fragment")
	// ErrInvalidSceneObjectUpdate occurs when the user attempts to update
	// the scene object on a type that does not already contain a scene object.
	ErrInvalidSceneObjectUpdate = errors.New("scene object cannot be updated on a resource type that does not contain a scene object")
	// ErrSecurityContext occurs when the user attempts to use a security context
	// that is not allowed on a multi-tenant project.
	ErrSecurityContext = errors.New("security context not allowed")
)

// Client contains data associated with a resource registry server.
type Client struct {
	appClient asgrpcpb.HotSharedStateApplicationServiceClient
	owsClient grpcowspb.ObjectWorldServiceClient
	rtrClient resourcetyperuntime.Client
	aciClient acigrpcpb.AssetCatalogInternalClient
	validator privileges.Validator
}

func containsResourceID(name string, resourceSet *rspb.ResourceSet) bool {
	return slices.ContainsFunc(resourceSet.GetResourceInstances(), func(ri *icpb.ResourceInstance) bool {
		return ri.GetName() == name
	})
}

func findResourceInstance(id string, resourceSet *rspb.ResourceSet) (*icpb.ResourceInstance, error) {
	idx := slices.IndexFunc(resourceSet.GetResourceInstances(), func(ri *icpb.ResourceInstance) bool {
		return ri.GetName() == id
	})
	if idx == -1 {
		return nil, fmt.Errorf("could not find resource instance %q in resource set", id)
	}
	return resourceSet.GetResourceInstances()[idx], nil
}

func makeReparentUpdate(name string, parent *owrpb.ObjectReferenceWithEntityFilter) *owupb.ObjectWorldUpdate {
	return &owupb.ObjectWorldUpdate{
		Update: &owupb.ObjectWorldUpdate_ReparentObject{
			ReparentObject: &owupb.ReparentObjectRequest{
				AttachTo: &owupb.ReparentObjectRequest_ParentObject{
					ParentObject: parent,
				},
				Object: &owrpb.ObjectReference{ObjectReference: &owrpb.ObjectReference_ByName{ByName: &owrpb.ObjectReferenceByName{ObjectName: name}}},
			},
		},
	}
}

func makeTransformUpdate(name string, parent *owrpb.ObjectReferenceWithEntityFilter, parentTThis *ppb.Pose) (*owupb.ObjectWorldUpdate, error) {
	var parentTransformReference *owrpb.TransformNodeReference
	switch x := parent.Reference.ObjectReference.(type) {
	case *owrpb.ObjectReference_Id:
		parentTransformReference = &owrpb.TransformNodeReference{
			TransformNodeReference: &owrpb.TransformNodeReference_Id{Id: x.Id},
		}
	case *owrpb.ObjectReference_ByName:
		parentTransformReference = &owrpb.TransformNodeReference{
			TransformNodeReference: &owrpb.TransformNodeReference_ByName{
				ByName: &owrpb.TransformNodeReferenceByName{
					TransformNodeReferenceByName: &owrpb.TransformNodeReferenceByName_Object{Object: x.ByName},
				},
			},
		}
	default:
		return nil, errors.New(fmt.Errorf("Object reference case is not supported: %T", x).Error())
	}
	return &owupb.ObjectWorldUpdate{
		Update: &owupb.ObjectWorldUpdate_UpdateTransform{
			UpdateTransform: &owupb.UpdateTransformRequest{
				NodeA: parentTransformReference,
				NodeB: &owrpb.TransformNodeReference{
					TransformNodeReference: &owrpb.TransformNodeReference_ByName{
						ByName: &owrpb.TransformNodeReferenceByName{
							TransformNodeReferenceByName: &owrpb.TransformNodeReferenceByName_Object{
								Object: &owrpb.ObjectReferenceByName{ObjectName: name},
							},
						},
					},
				},
				ATB: parentTThis,
			},
		},
	}, nil
}

func makeDeleteUpdate(objectID string, force bool) *owupb.ObjectWorldUpdate {
	return &owupb.ObjectWorldUpdate{
		Update: &owupb.ObjectWorldUpdate_DeleteObject{
			DeleteObject: &owupb.DeleteObjectRequest{
				Object: &owrpb.ObjectReference{
					ObjectReference: &owrpb.ObjectReference_Id{Id: objectID},
				},
				Force: force,
			},
		},
	}
}

// applyWorldUpdates applies the given updates to the given world.
// This exists as a standalone function so that it can be called from different contexts without
// having to create an instance of `Client` first.
// TODO(b/204178012): Replace clone -> edit -> clone (w/replace) -> delete workflow with atomic
// updates to the given world once they are supported.
func applyWorldUpdates(ctx context.Context, owc grpcowspb.ObjectWorldServiceClient, localWorldID string, updates []*owupb.ObjectWorldUpdate) error {
	// Clone the current world and let the world service choose a unique id.
	clonedWorld, err := owc.CloneWorld(ctx, &owspb.CloneWorldRequest{WorldId: localWorldID})
	if err != nil {
		return errors.Wrap(err, "CloneWorld")
	}

	// Delete the cloned world when the function returns since it won't be needed.
	defer func() {
		worldID := clonedWorld.GetId()
		_, delErr := owc.DeleteWorld(ctx, &owspb.DeleteWorldRequest{WorldId: worldID})
		if delErr != nil {
			log.ErrorContextf(ctx, "DeleteWorld for %q failed: %v", worldID, delErr)
			// Propagate the error to the caller.
			// Not sure if propagating deletion error to the caller is the best strategy  :shrug:. But
			// ignoring the error is not great either.
			err = errors.Wrap(delErr, "DeleteWorld")
		}
	}()

	// Apply the required updates to the cloned world.
	if _, err := owc.UpdateWorldResources(ctx, &owspb.UpdateWorldResourcesRequest{
		WorldId:      clonedWorld.GetId(),
		WorldUpdates: &owupb.ObjectWorldUpdates{Updates: updates},
		View:         owupb.ObjectView_BASIC,
	}); err != nil {
		return errors.Wrap(err, "UpdateWorldResources")
	}

	// Finally, replace the current world with the cloned world.
	if _, err := owc.CloneWorld(ctx, &owspb.CloneWorldRequest{
		WorldId:        clonedWorld.GetId(),
		ClonedWorldId:  localWorldID,
		AllowOverwrite: true, // Overwrite localWorldID with clonedWorld.
	}); err != nil {
		return errors.Wrap(err, "CloneWorld")
	}

	return nil
}

// CreateResourceInstanceOpts holds options for CreateResourceInstance.
type CreateResourceInstanceOpts struct {
	Name          string
	TypeIDVersion string
	Parent        *owrpb.ObjectReferenceWithEntityFilter
	ParentTThis   *ppb.Pose
	// Configuration is the configuration for the service component of the resource.
	Configuration *apb.Any
	// SceneObjectConfig is the configuration for the scene object component of the resource.
	SceneObjectConfig *socpb.SceneObjectConfig
	DataFiles         *datafilespb.DataFiles
}

// CreateResourceInstance creates a new resource instance within the currently
// deployed app.  Returns whether or not the instance had deploys services.
//
// May return one of the following errors:
//
// ErrInvalidResourceTypeID
// ErrParentSetMissingWorldFragment
// ErrResourceInstanceAlreadyExists
// ErrResourceTypeNotFound
// ErrInvalidWorldFragmentUpdate
// ErrInvalidSceneObjectUpdate
//
// Other errors should be treated as INTERNAL.
func (c *Client) CreateResourceInstance(ctx context.Context, opts *CreateResourceInstanceOpts) (bool, error) {
	var reqIDV *idpb.IdVersion
	if parts, err := idutils.NewIDVersionParts(opts.TypeIDVersion); err != nil {
		return false, ErrInvalidResourceTypeID
	} else {
		reqIDV = parts.IDVersionProto()
	}

	// Enforce a single-version constraint on installation by overriding the
	// user request for a specific version with an installed version of the
	// same id.
	targetIDV := reqIDV
	if idvs, _, err := c.rtrClient.List(ctx); err != nil {
		return false, fmt.Errorf("unable to list id versions: %w", err)
	} else {
		for _, idv := range idvs {
			if parts, err := idutils.NewIDVersionParts(idv); err != nil {
				return false, fmt.Errorf("invalid id_version in cache: %w", err)
			} else {
				if reqIDV.GetId().GetPackage() == parts.Package() && reqIDV.GetId().GetName() == parts.Name() {
					targetIDV = parts.IDVersionProto()
					// There may be multiple versions of this id installed as
					// we still haven't strictly enforced a single version
					// variant yet.  If we find an exact match, stop looking
					// and use it, otherwise take the last one.
					if reqIDV.GetVersion() == parts.Version() {
						break
					}
				}
			}
		}
	}

	typeIDVersion := idutils.IDVersionFromProtoUnchecked(targetIDV)
	rtr, err := c.rtrClient.Get(ctx, typeIDVersion)
	if status.Code(err) == codes.NotFound {
		idvs := []*idpb.IdVersion{targetIDV}
		gatherer := gather.FromAssetCatalog(c.aciClient, gather.WithAllowedAssetTypes(typeutils.AssetTypesWithInstances()))
		rtrs, _, err := gatherer(ctx, idvs)
		if status.Code(err) == codes.NotFound {
			return false, ErrResourceTypeNotFound
		}
		if err != nil {
			return false, errors.Wrap(err, "gatherer")
		}
		if len(rtrs) > 0 {
			rtr = rtrs[0]
		}

		if err := descriptorcompatibility.Reconcile(rtr.GetMetadata().GetFileDescriptorSet()); err != nil {
			return false, errors.Wrap(err, fmt.Sprintf("unable to reconcile the file descriptor set %q", idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())))
		}

		// Add resource type runtime info to the runtime DB.  Do this
		// regardless of whether or not this call fails later.  There's a
		// reasonable chance that this could be called again with the correct
		// set of arguments, and we should just have the information available
		// locally.  This also seems roughly consistent with the idea that we
		// don't clear out runtime information after deleting the last resource
		// instance of that type.  We only garbage collect information on
		// deployment.
		if err := c.rtrClient.Add(ctx, rtr); err != nil {
			return false, errors.Wrap(err, "Add")
		}
	} else if err != nil {
		return false, errors.Wrap(err, "Get")
	}

	if err := c.validator.Validate(rtr); err != nil {
		return false, fmt.Errorf("%w: %v", ErrSecurityContext, err)
	}

	// Load cluster and extract resource set.
	response, err := c.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return false, errors.Wrap(err, "GetCurrentApplication")
	}
	resourceSet := response.GetApplication().GetResources()

	if containsResourceID(opts.Name, resourceSet) {
		return false, ErrResourceInstanceAlreadyExists
	}

	// Check for service configuration, otherwise use the default from the type.
	cfg := opts.Configuration
	if cfg == nil {
		cfg = rtr.GetDefaultConfiguration()
	}
	if err := resourcefixer.FixConfig(cfg); err != nil {
		return false, fmt.Errorf("failed to update config for %q: %w", opts.Name, err)
	}

	// Check for geometric configuration, otherwise use the default from the type.
	sceneObjectConfig := opts.SceneObjectConfig
	if sceneObjectConfig == nil {
		sceneObjectConfig = rtr.GetDefaultSceneObjectConfig()
	}

	// Add resource instance
	ri := &icpb.ResourceInstance{
		Name:              opts.Name,
		TypeIdVersion:     typeIDVersion,
		Configuration:     cfg,
		SceneObjectConfig: sceneObjectConfig,
		DataFiles:         opts.DataFiles,
	}

	resourceSet.ResourceInstances = append(resourceSet.GetResourceInstances(), ri)
	if rtr.GetSceneObject() == nil && rtr.GetWorldFragment() == nil {
		if opts.Parent != nil {
			return false, ErrParentSetMissingWorldFragment
		} else if opts.ParentTThis != nil {
			// TODO(b/331999561) Ignore the field now, but bring back the error once create paths are
			// different for scene objects and services
			log.InfoContextf(ctx, "parent_t_this was set but resource type doesn't contain a world fragment")
		}
	} else {
		parentReference := opts.Parent
		if parentReference == nil {
			// Default to parenting to root.
			parentReference = &owrpb.ObjectReferenceWithEntityFilter{
				Reference:    &owrpb.ObjectReference{ObjectReference: &owrpb.ObjectReference_Id{Id: "root"}},
				EntityFilter: &owrpb.ObjectEntityFilter{IncludeBaseEntity: true},
			}
		}
		objectWorldUpdates := []*owupb.ObjectWorldUpdate{
			makeReparentUpdate(opts.Name, parentReference),
		}
		if pTT := opts.ParentTThis; pTT != nil {
			update, err := makeTransformUpdate(opts.Name, parentReference, pTT)
			if err != nil {
				return false, errors.Wrap(err, "MakeTransformUpdate")
			}
			objectWorldUpdates = append(objectWorldUpdates, update)
		}
		resourceSet.ObjectWorldUpdates = &owupb.ObjectWorldUpdates{
			Updates: append(resourceSet.GetObjectWorldUpdates().GetUpdates(), objectWorldUpdates...),
		}

		// Try to build a world with this ResourceSet, but don't apply it
		// anywhere. If this fails, then we want to unwind our changes now,
		// rather than experiencing failures when updating the world later.
		rtrs, err := runtimedbtransfer.Retrieve(ctx, c.rtrClient, response.GetApplication())
		if err != nil {
			return false, fmt.Errorf("unable to get asset information: %w", err)
		}
		rids, err := resourcereader.ExtractGeometricResourceInstanceData(resourceSet, rtrs)
		if err != nil {
			return false, fmt.Errorf("unable to extract geometric information: %w", err)
		}

		// Compose the new world from resource instances.
		composeRequest := &owspb.CreateWorldFromResourceInstancesRequest{
			ResourceInstanceData: rids,
			UpdateWorldResources: resourceSet.GetObjectWorldUpdates(),
		}
		composeResponse, err := c.owsClient.CreateWorldFromResourceInstances(ctx, composeRequest)
		if err != nil {
			return false, errors.Wrap(err, "CreateWorldFromResourceInstances.")
		}

		// Delete the world if we succeeded above. It's really only interesting if it failed, but we won't have a world to examine in that situation anyway.
		if _, err := c.owsClient.DeleteWorld(ctx, &owspb.DeleteWorldRequest{
			WorldId: composeResponse.GetId(),
		}); err != nil {
			return false, errors.Wrap(err, "DeleteWorld")
		}
	}

	if _, err := c.appClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   response.GetApplication(),
		RevisionToken: response.GetRevisionToken(),
	}); err != nil {
		return false, errors.Wrap(err, "SetCurrentApplication")
	}

	return rtr.GetServiceDef() != nil, nil
}

// DeleteResourceInstance removes the resource instance from the currently
// deployed app including committing changes to the world.  Returns whether or
// not the instance had services deployed.
//
// May return one of the following errors:
//
//	ErrCannotModifySideloadedResource
//	ErrResourceNotFound.
//
// Other errors should be treated as INTERNAL.
func (c *Client) DeleteResourceInstance(ctx context.Context, name string, worldID string) (bool, error) {
	// Load cluster and extract resource set.
	response, err := c.appClient.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return false, errors.Wrap(err, "GetCurrentApplication")
	}
	resourceSet := response.GetApplication().GetResources()

	// Check that the resource instance specified in the request exists.
	var hasServices bool
	var hasObject bool
	if ri, err := findResourceInstance(name, resourceSet); err != nil {
		return false, ErrResourceNotFound
	} else if rtr, err := c.rtrClient.Get(ctx, ri.GetTypeIdVersion()); err != nil {
		return false, errors.Wrapf(err, "Get(%q)", ri.GetTypeIdVersion())
	} else {
		hasObject = rtr.GetSceneObject() != nil || rtr.GetWorldFragment() != nil
		hasServices = rtr.GetServiceDef() != nil
	}

	// High level algorithm to delete resource instance `name`:
	// - Delete the object corresponding to `name` from the world (including reparenting its children).
	// - Commit the changes to the resource set:
	//   - Remove `name` from the list of resource instances.
	//   - Extract the latest updates from the world and make that the new world updates. Deleting the
	//     object from the world means we no longer have to filter the updates -- we can simply use
	//     them as-is.
	//
	// The above algorithm avoids the following extremely slow process:
	// - First save the world to resource_set
	// - Then make changes to the resource_set
	// - Finally load the world from the resource_set.

	// Delete world object corresponding to the resource (if it exists).
	if hasObject {
		var object *owspb.Object
		if o, err := c.owsClient.GetObject(ctx, &owspb.GetObjectRequest{
			WorldId:     worldID,
			ObjectQuery: &owspb.GetObjectRequest_ResourceHandleName{ResourceHandleName: name},
			View:        owupb.ObjectView_FULL,
		}); err != nil {
			return false, errors.Wrap(err, "GetObject")
		} else {
			object = o
		}
		// Before deleting the object from the world, let's first reparent its children to root.
		var updates []*owupb.ObjectWorldUpdate
		for _, child := range object.GetChildren() {
			newParent := &owrpb.ObjectReferenceWithEntityFilter{
				Reference: &owrpb.ObjectReference{ObjectReference: &owrpb.ObjectReference_Id{Id: "root"}},
			}
			update := makeReparentUpdate(child.GetName(), newParent)
			updates = append(updates, update)
		}

		// Add an update that deletes the object.
		// Not deleting forcefully since the object should not have children attached to it.
		delUpdate := makeDeleteUpdate(object.GetId(), false)
		updates = append(updates, delUpdate)

		// Apply batch updates.
		if err := applyWorldUpdates(ctx, c.owsClient, worldID, updates); err != nil {
			return false, errors.Wrap(err, "applyWorldUpdates")
		}
	}

	// Remove resource instance from the resource set.
	resourceSet.ResourceInstances = slices.DeleteFunc(resourceSet.GetResourceInstances(), func(ri *icpb.ResourceInstance) bool {
		return ri.GetName() == name
	})

	// Extract the latest updates from the world.
	if hasObject {
		rtrs, err := runtimedbtransfer.Retrieve(ctx, c.rtrClient, response.GetApplication())
		if err != nil {
			return false, fmt.Errorf("unable to get asset information: %w", err)
		}
		rids, err := resourcereader.ExtractGeometricResourceInstanceData(resourceSet, rtrs)
		if err != nil {
			return false, fmt.Errorf("unable to extract geometric information: %w", err)
		}

		extractResponse, err := c.owsClient.ExtractResourceInstances(ctx, &owspb.ExtractResourceInstancesRequest{
			WorldId:              worldID,
			ResourceInstanceData: rids,
		})
		if err != nil {
			return false, errors.Wrap(err, "ExtractResourceInstances")
		}
		resourceSet.ObjectWorldUpdates = extractResponse.GetUpdateWorldResources()
	}

	if _, err := c.appClient.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   response.GetApplication(),
		RevisionToken: response.GetRevisionToken(),
	}); err != nil {
		return false, errors.Wrap(err, "SetCurrentApplication")
	}
	return hasServices, nil
}

// NewClientOpts holds options for NewClient.
type NewClientOpts struct {
	AppClient asgrpcpb.HotSharedStateApplicationServiceClient
	OWSClient grpcowspb.ObjectWorldServiceClient
	RTRClient resourcetyperuntime.Client
	ACIClient acigrpcpb.AssetCatalogInternalClient
	Validator privileges.Validator
}

// NewClient returns a new resource writer client with the given options.
func NewClient(opts NewClientOpts) *Client {
	return &Client{
		appClient: opts.AppClient,
		owsClient: opts.OWSClient,
		rtrClient: opts.RTRClient,
		aciClient: opts.ACIClient,
		validator: opts.Validator,
	}
}
