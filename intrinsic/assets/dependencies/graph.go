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

// Package graph provides data structures and validation functions for Solution dependency validation.
package graph

import (
	"context"
	"fmt"
	"maps"
	"slices"
	"sort"
	"strings"
	"sync"

	dependencyerrors "intrinsic/assets/dependencies/errors"
	"intrinsic/assets/dependencies/traversal"
	deputils "intrinsic/assets/dependencies/utils"
	"intrinsic/assets/errors/report"
	"intrinsic/assets/typeutils"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	dependencypb "intrinsic/assets/proto/v1/dependency_go_proto"
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

// AssetUsage is usage of an Asset within the definition of another Asset.
type AssetUsage struct {
	// SourceDescription will be shown to users (e.g., in errors) as the name for
	// where this reference is located in the Asset definition. For example, an
	// Asset used in a skill node of a behavior tree might use 'Skill node
	// "example_node" in Process "ai.intrinsic.my_process"'.
	SourceDescription string
	// Optional config applied to the reference within the Asset. This must be
	// resolvable using the descriptor set for the Asset being referenced.
	Configuration *anypb.Any
}

// ReferencedAsset is an Asset referenced by another Asset. Referenced Assets
// may be used within the definition of the Asset.
type ReferencedAsset struct {
	// Whether this Asset is explicitly declared by the referencing Asset.
	// Currently, both the `ProcessAsset` and `HardwareDevice` do this in an
	// `assets` field.
	Declared bool
	// The version that is explicitly declared. Only holds a value if `Declared`
	// is true and the Asset is declared with a version (i.e., from the catalog).
	DeclaredVersion string
	// Usages of the referenced Asset within the definition of the Asset.
	Usages []AssetUsage
}

// AssetContext contains a subset of an Asset's metadata and additionally caches capability
// information relevant for dependency validation.
type AssetContext struct {
	// AssetType is the type of the Asset.
	AssetType atypepb.AssetType
	// ConfigMessageName is the name of the config message for the Asset.
	ConfigMessageName string
	// FileDescriptorSet is the file descriptor set for the Asset.
	FileDescriptorSet *descriptorpb.FileDescriptorSet
	// ID is the Asset ID.
	ID string
	// Version is the version of the Asset, if available.
	Version string
	// References contains Assets referenced by another Asset and the usages of
	// those references within the definition. The key is the ID of the Asset
	// being referenced.
	//
	// Only select Assets reference other Assets. Currently supported:
	// - `ProcessAsset` (referenced in behavior tree)
	References map[string]ReferencedAsset
	// ProvidesURIs is a set of interface URIs exposed by the Asset.
	ProvidesURIs map[string]bool
	// hasDepsOnce is a one-time check to infer whether the Asset has any dependencies.
	hasDepsOnce sync.Once
	// cachedHasDeps stores the cached result of the dependency check.
	cachedHasDeps bool
	// hasDepsErr stores the error encountered during the dependency check, if any.
	hasDepsErr error
}

// providesObject returns whether the asset provides an object that can be depended upon.
func (ac *AssetContext) providesObject() bool {
	return slices.Contains(typeutils.AssetTypesWithObjects(), ac.AssetType)
}

// hasDependencies returns whether the asset has or can have dependencies on other Assets or
// instances. It caches the result and any error encountered during the check.
func (ac *AssetContext) hasDependencies() (bool, error) {
	ac.hasDepsOnce.Do(func() {
		ac.cachedHasDeps, ac.hasDepsErr = configSupportsDependencies(ac.ID, ac.FileDescriptorSet, ac.ConfigMessageName)
	})
	return ac.cachedHasDeps, ac.hasDepsErr
}

// configSupportsDependencies checks if the config message specified has fields that support
// dependencies.
func configSupportsDependencies(id string, fds *descriptorpb.FileDescriptorSet, configMessageFullName string) (bool, error) {
	if fds == nil || configMessageFullName == "" {
		return false, nil
	}
	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return false, fmt.Errorf("failed to populate the registry for %q: %w", id, err)
	}
	desc, err := files.FindDescriptorByName(protoreflect.FullName(configMessageFullName))
	if err != nil {
		return false, fmt.Errorf("could not find config message %q in provided descriptors: %w", configMessageFullName, err)
	}
	messageDesc, ok := desc.(protoreflect.MessageDescriptor)
	if !ok {
		return false, fmt.Errorf("config message %q is not a message", configMessageFullName)
	}
	return deputils.HasResolvedDependency(messageDesc, deputils.WithDependencyAnnotation(), deputils.WithTreatAnyAsTrue()), nil
}

// InstanceContext provides dependency-related information about an instance of an Asset.
type InstanceContext struct {
	// Asset is the ID of the Asset that this instance is an instance of.
	Asset string
	// Config is this instance's config proto.
	Config *anypb.Any
	// Name is the instance name.
	Name string
}

// SolutionContext contains a subset of metadata about Assets and Asset instances
// present in a Solution.
//
// It is used as a context to validate the dependency graph of the Solution.
type SolutionContext struct {
	// Assets is a map of Assets present in the Solution in the form of AssetContexts.
	Assets map[string]*AssetContext
	// Instances is a map of instances present in the Solution in the form of InstanceContexts.
	Instances map[string]*InstanceContext
}

// validateComponents validates the components of the solutioncontext by ensuring that non-Process
// Assets do not contain the Process proto and all instances reference known Assets.
func (sc *SolutionContext) validateComponents() error {
	for _, instance := range sc.Instances {
		if _, found := sc.Assets[instance.Asset]; !found {
			return fmt.Errorf("instance %q has unknown asset %q", instance.Name, instance.Asset)
		}
	}
	return nil
}

type validateGraphOptions struct {
	report       *report.Report
	referencesTo map[string]bool
	// hasReferencesFilter is true if WithOnlyReferencesTo was called.
	// This allows us to distinguish between not applying any filter (false,
	// validate all) and applying a filter that matches nothing (true,
	// referencesTo is empty, validate nothing).
	hasReferencesFilter bool
}

// ValidateGraphOption is an option for validating the dependency graph.
type ValidateGraphOption func(*validateGraphOptions)

// WithReport sets the validation report to use for collecting errors.
func WithReport(r *report.Report) ValidateGraphOption {
	return func(o *validateGraphOptions) {
		o.report = r
	}
}

// WithOnlyReferencesTo can be used to throw validation errors only if a dependency interface
// involves one of the nodes provided here. All other dependency validation errors that do not involve
// any of these referenced nodes are ignored. Note that structural validation errors on the solution
// context itself (e.g. from validateComponents) are not ignored. Use this option when it's needed to
// validate all incoming edges to a set of nodes, or references to those nodes.
//
// The arguments here could be Asset instance names, Asset IDs, or both.
//
// For instance, after deleting an Asset instance, it may be required to check if the
// Asset instance, a node that's no longer even in the dependency graph, is still being referenced
// by other dependency nodes. Another use case is when it is needed to validate interfaces for
// all instances of a particular Asset.
//
// This option is distinct from calling [ValidateNode], since that only validates the outgoing
// edges from a node (dependencies).
//
// If called with an empty list of references, all dependency validation errors
// will be ignored (nothing will be validated).
func WithOnlyReferencesTo(referencesTo ...string) ValidateGraphOption {
	return func(o *validateGraphOptions) {
		o.hasReferencesFilter = true
		if o.referencesTo == nil {
			o.referencesTo = make(map[string]bool)
		}
		for _, ref := range referencesTo {
			o.referencesTo[ref] = true
		}
	}
}

// ValidateNode validates the outgoing dependency edges (or dependencies) of a single node in
// the dependency graph.
//
// A node can be any Asset or Asset instance in the SolutionContext.
//
// For Asset instances, the identifier `name` is the instance name. For Assets, the identifier
// `name` is the Asset ID.
//
// The dependency graph is constructed from the Assets and instances present in the provided [SolutionContext].
func ValidateNode(ctx context.Context, name string, sc *SolutionContext, options ...ValidateGraphOption) error {
	opts := &validateGraphOptions{
		report: report.New(),
	}
	for _, opt := range options {
		opt(opts)
	}
	if err := sc.validateComponents(); err != nil {
		return err
	}
	return validateNodesByName(ctx, []string{name}, sc, opts)
}

// ValidateAll validates all nodes in the dependency graph.
//
// A node can be any Asset or Asset instance in the SolutionContext.
//
// The dependency graph is constructed from the Assets and instances present in the provided [SolutionContext].
//
// This function validates the dependencies of all Assets and Asset instances that are retrieved
// from the [SolutionContext].
func ValidateAll(ctx context.Context, sc *SolutionContext, options ...ValidateGraphOption) error {
	opts := &validateGraphOptions{
		report: report.New(),
	}
	for _, opt := range options {
		opt(opts)
	}

	if err := sc.validateComponents(); err != nil {
		return err
	}

	if _, err := validateNodes(ctx, sc, nil, opts); err != nil {
		return err
	}
	return nil
}

// dependency represents an edge in the dependency graph. It contains
// the interface definition and the node name that it depends on.
type dependency struct {
	// iface defines the interface requirements (e.g., required gRPC services, payload types)
	// that the target node must satisfy.
	iface *dependencypb.Dependency
	// The name of the node that this dependency is on.
	on string
	// sourceDescription is a description of the source node of the dependency.
	// For Service/HWD instances, this would be: "Instance <instance name> of Asset <Asset ID>"
	// For Process Assets, this would be: "Skill node <task node name> in Process <Asset ID>".
	sourceDescription string
	// from contains the identifiers of the source node(s) of this dependency.
	// For Service/HWD instances, this contains the instance name and the Asset ID.
	// For Skill nodes in a Process, this contains the Process ID and the Skill ID.
	// The ordering of the identifiers does not matter.
	from []string
}

// node represents a node in the dependency graph.
//
// A node can be any Asset or Asset instance in the SolutionContext.
//
// Outgoing dependency edges are:
// - Resolved dependency interfaces for instances.
// - Direct referenced Assets (e.g. Skill usages in a Process, or device manifests) for Assets.
//
// Entities with no outgoing dependencies are valid nodes with zero edges.
type node struct {
	// assetID is the ID of the Asset.
	assetID string
	// dependencies is the list of outgoing dependency edges from this node.
	dependencies []*dependency
	// name is the identifier of the node.
	//
	// For instances, this is the instance's name. For Assets, this is the Asset ID.
	name string
}

// validateNodesByName validates the dependencies of a list of nodes. It iterates through
// all Assets and instances from the [SolutionContext], constructs the relevant dependency graph,
// and then validates them.
//
// It returns an error if any node was not found or if validation failed.
func validateNodesByName(ctx context.Context, nodes []string, sc *SolutionContext, opts *validateGraphOptions) error {
	nodeSet := make(map[string]bool)
	for _, node := range nodes {
		nodeSet[node] = true
	}
	found, err := validateNodes(ctx, sc, nodeSet, opts)
	if err != nil {
		return err
	}

	// verify that all nodes were collected and validated.
	if len(found) < len(nodeSet) {
		var missing []string
		for node := range nodeSet {
			if !found[node] {
				missing = append(missing, node)
			}
		}
		sort.Strings(missing)
		return fmt.Errorf("nodes not found: %s", strings.Join(missing, ", "))
	}

	return nil
}

// validateNodes constructs and validates the dependency graph for Assets and
// instances in sc.
//
// Outgoing dependencies are determined by:
// - Asset references for Assets (e.g. skills called by a Process).
// - Resolved dependency configurations for instances.
//
// If filter is non-nil, only nodes with names in the filter are validated.
// It returns the set of node names that were found.
func validateNodes(ctx context.Context, sc *SolutionContext, filter map[string]bool, opts *validateGraphOptions) (map[string]bool, error) {
	found := make(map[string]bool)

	// Sort Asset IDs to ensure deterministic validation order
	assets := sc.Assets
	assetIDs := make([]string, 0, len(assets))
	for id := range assets {
		assetIDs = append(assetIDs, id)
	}
	sort.Strings(assetIDs)

	for _, id := range assetIDs {
		asset := assets[id]

		if filter != nil {
			if _, exists := filter[asset.ID]; !exists {
				continue
			}
			found[asset.ID] = true
		}

		// References _are_ the dependencies of Assets (see above). An Asset without
		// references won't have any dependencies and doesn't need to be validated.
		if len(asset.References) == 0 {
			continue
		}

		if err := validateReferences(sc, asset, opts); err != nil {
			return nil, err
		}

		node, err := nodeFromAsset(ctx, asset.ID, sc)
		if err != nil {
			return nil, err
		}
		if node == nil {
			continue
		}
		if err := validateNode(sc, node, opts); err != nil {
			return nil, err
		}
	}

	// Sort instance names to ensure deterministic validation order
	instances := sc.Instances
	instanceNames := make([]string, 0, len(instances))
	for name := range instances {
		instanceNames = append(instanceNames, name)
	}
	sort.Strings(instanceNames)

	for _, name := range instanceNames {
		instance := instances[name]
		if filter != nil {
			if _, exists := filter[instance.Name]; !exists {
				continue
			}
			found[instance.Name] = true
		}
		node, err := nodeFromInstance(instance.Name, sc)
		if err != nil {
			return nil, err
		}
		if node == nil {
			continue
		}
		if err := validateNode(sc, node, opts); err != nil {
			return nil, err
		}
	}

	return found, nil
}

// validateReferences validates referenced Assets in the given AssetContext
// against the Assets available in the SolutionContext.
//
// Validation checks:
//  1. Whether the Asset being referenced is available in the Solution. If the
//     referenced Asset is used in the definition of the Asset, we return
//     [dependencyerrors.CodeErrMissingUsedAsset]. If the Asset is NOT used but
//     it is explicitly listed in referenced Assets, we return
//     [dependencyerrors.CodeErrMissingReferencedAsset] instead. Both of these
//     codes stem from a referenced Asset missing in the Solution, but they may
//     eventually have different severities as a used Asset missing is more
//     critical and is likely to cause runtime issues.
//  3. Whether an explicitly referenced Asset is available at the specified
//     version. Only applies if the referenced Asset is specified as a catalog
//     reference and available in the Solution. If the version does not match,
//     we return [dependencyerrors.CodeErrReferencedAssetVersionMismatch].
//  4. Whether an explicitly referenced Asset is actually used. This is a low
//     severity "cleanup" type warning. If an Asset is referenced explicitly but
//     not used, it may be installed implicitly with the referencing Asset for
//     no reason. We return [dependencyerrors.CodeErrUnusedReferencedAsset] in
//     this case.
func validateReferences(sc *SolutionContext, asset *AssetContext, opts *validateGraphOptions) error {
	if len(asset.References) == 0 {
		return nil
	}

	// Sort reference IDs to ensure deterministic validation order
	refIDs := slices.Collect(maps.Keys(asset.References))
	slices.Sort(refIDs)

	for _, refID := range refIDs {
		if opts.hasReferencesFilter && !opts.referencesTo[asset.ID] && !opts.referencesTo[refID] {
			continue
		}

		ref := asset.References[refID]
		target, ok := sc.Assets[refID]
		// The Asset isn't available in the Solution.
		if !ok {
			if len(ref.Usages) > 0 {
				if err := opts.report.Add(dependencyerrors.Newf(
					dependencyerrors.CodeErrMissingUsedAsset,
					"Asset %q uses Asset %q, which is not available",
					asset.ID, refID,
				)); err != nil {
					return err
				}
			} else if ref.Declared {
				if err := opts.report.Add(dependencyerrors.Newf(
					dependencyerrors.CodeErrMissingReferencedAsset,
					"Asset %q explicitly references Asset %q, which is not available",
					asset.ID, refID,
				)); err != nil {
					return err
				}
			}
		} else if ref.DeclaredVersion != "" && target.Version != "" && target.Version != ref.DeclaredVersion {
			if err := opts.report.Add(dependencyerrors.Newf(
				dependencyerrors.CodeErrReferencedAssetVersionMismatch,
				"Asset %q references Asset %q at version %q, but version %q is available",
				asset.ID, refID, ref.DeclaredVersion, target.Version,
			)); err != nil {
				return err
			}
		}

		if ref.Declared && len(ref.Usages) == 0 {
			if err := opts.report.Add(dependencyerrors.Newf(
				dependencyerrors.CodeErrUnusedReferencedAsset,
				"Asset %q explicitly references Asset %q, but does not use it",
				asset.ID, refID,
			)); err != nil {
				return err
			}
		}
	}

	return nil
}

// validateNode validates a single dependency node against the provided Asset and
// instance information in the SolutionContext.
func validateNode(sc *SolutionContext, node *node, opts *validateGraphOptions) error {
	for _, dep := range node.dependencies {
		var targetAsset *AssetContext
		var ok bool

		// Find the dependency target.
		if inst, exists := sc.Instances[dep.on]; exists {
			targetAsset, ok = sc.Assets[inst.Asset]
		} else if asset, exists := sc.Assets[dep.on]; exists {
			// Data Assets are the only Assets that can be depended on directly
			// using the Asset ID. All other Assets must be depended on by their
			// instance names.
			if asset.AssetType == atypepb.AssetType_ASSET_TYPE_DATA {
				targetAsset = asset
				ok = true
			}
		}

		if opts.hasReferencesFilter && !involvesNodes(dep, targetAsset, opts.referencesTo) {
			// Skip validation for this dependency if this dependency and the corresponding node do not match
			// the provided references.
			continue
		}

		if !ok || targetAsset == nil {
			if err := opts.report.Add(dependencyerrors.Newf(
				dependencyerrors.CodeErrUnknownDependency,
				"%s has unknown dependency %q", dep.sourceDescription, dep.on,
			)); err != nil {
				return err
			}
			continue
		}

		if err := opts.report.Add(validateInterface(dep, targetAsset)); err != nil {
			return err
		}
	}
	return nil
}

// involvesNodes returns true if the dependency or its associated nodes/Assets are present in `nodeNames`.
func involvesNodes(dep *dependency, targetAsset *AssetContext, nodeNames map[string]bool) bool {
	result := nodeNames[dep.on]
	if targetAsset != nil {
		result = result || nodeNames[targetAsset.ID]
	}
	for _, f := range dep.from {
		result = result || nodeNames[f]
	}
	return result
}

func formatTarget(instanceOrAssetName string, targetAsset *AssetContext) string {
	if targetAsset.AssetType == atypepb.AssetType_ASSET_TYPE_DATA {
		return fmt.Sprintf("Data %q", targetAsset.ID)
	}
	return fmt.Sprintf("the instance %q (Asset: %s)", instanceOrAssetName, targetAsset.ID)
}

// validateInterface validates that the target Asset satisfies the dependency's interface requirements.
func validateInterface(dependency *dependency, targetAsset *AssetContext) error {
	if dependency.iface.GetRequiresObject() != nil && !targetAsset.providesObject() {
		return dependencyerrors.Newf(
			dependencyerrors.CodeErrInterfaceMismatch,
			"%s requires an object, but %q does not provide one", dependency.sourceDescription, dependency.on,
		)
	}
	for _, req := range dependency.iface.GetRequires() {
		if !targetAsset.ProvidesURIs[req] {
			return dependencyerrors.Newf(
				dependencyerrors.CodeErrInterfaceMismatch,
				"%s requires an Asset that provides the %q interface, but %s does not provide it. Either update %q to a version that provides that interface (if available), or replace it with another Asset that does.",
				dependency.sourceDescription, req, formatTarget(dependency.on, targetAsset), targetAsset.ID,
			)
		}
	}
	return nil
}

// nodeFromInstance constructs a dependency node representing the Asset instance.
func nodeFromInstance(instanceName string, sc *SolutionContext) (*node, error) {
	instance, ok := sc.Instances[instanceName]
	if !ok {
		return nil, fmt.Errorf("instance %q not found", instanceName)
	}

	asset, ok := sc.Assets[instance.Asset]
	if !ok {
		return nil, fmt.Errorf("installed Asset %q for instance %q not found", instance.Asset, instance.Name)
	}
	fds := asset.FileDescriptorSet
	if fds == nil {
		return nil, nil
	}
	hasDeps, err := asset.hasDependencies()
	if err != nil {
		return nil, err
	}
	if !hasDeps {
		return nil, nil
	}

	deps, err := collectDependencies(instance.Config, fds)
	if err != nil {
		return nil, fmt.Errorf("failed to traverse resolved dependencies for instance %q: %w", instance.Name, err)
	}
	for _, dep := range deps {
		dep.sourceDescription = fmt.Sprintf("Instance %q of Asset %q", instance.Name, instance.Asset)
		dep.from = []string{instance.Asset, instance.Name}
	}
	return &node{
		name:         instance.Name,
		assetID:      instance.Asset,
		dependencies: deps,
	}, nil
}

// nodeFromAsset returns a graph node representing the given Asset. It collects all dependencies
// based on the references. Returns `nil` and NOT error if the Asset does not have references.
func nodeFromAsset(ctx context.Context, assetID string, sc *SolutionContext) (*node, error) {
	asset, ok := sc.Assets[assetID]
	if !ok {
		return nil, fmt.Errorf("Asset %q not found in the solution context", assetID)
	}
	references := asset.References
	if len(references) == 0 {
		return nil, nil
	}
	deps, err := extractDependenciesFromReferences(ctx, references, sc, asset)
	if err != nil {
		return nil, err
	}
	return &node{
		name:         asset.ID,
		assetID:      asset.ID,
		dependencies: deps,
	}, nil
}

// collectDependencies collects dependencies for an Asset or Asset instance from the provided
// configuration and file descriptor set.
//
// It traverses through the config message and finds dependencies configured through
// the `ResolvedDependency` fields.
func collectDependencies(config *anypb.Any, fds *descriptorpb.FileDescriptorSet) ([]*dependency, error) {
	var deps []*dependency
	err := traversal.ForEachResolvedDependency(config, fds, func(dep *dependencypb.Dependency, msg *rdpb.ResolvedDependency) (*rdpb.ResolvedDependency, error) {
		if msg == nil {
			return nil, nil
		}
		if msg.GetName() == "" {
			return msg, nil
		}
		deps = append(deps, &dependency{
			iface: dep,
			on:    msg.GetName(),
		})
		return msg, nil
	})
	if err != nil {
		return nil, err
	}
	return deps, nil
}

// extractDependenciesFromReferences collects dependent Assets from references.
func extractDependenciesFromReferences(ctx context.Context, references map[string]ReferencedAsset, sc *SolutionContext, asset *AssetContext) ([]*dependency, error) {
	if len(references) == 0 {
		return nil, nil
	}

	var dependencies []*dependency
	// Iterate the map deterministically so that dependencies always get ordered
	// the same. This ensures downstream validation is predictable.
	refIDs := slices.Collect(maps.Keys(references))
	slices.Sort(refIDs)
	for _, refID := range refIDs {
		reference := references[refID]
		for _, usage := range reference.Usages {
			deps, err := extractDependenciesFromConfiguration(refID, usage.Configuration, sc)
			if err != nil {
				return nil, fmt.Errorf("failed to extract dependencies: %w", err)
			}

			if len(deps) > 0 {
				assetTypeName := typeutils.AssetTypeDisplayName(asset.AssetType)
				for _, dep := range deps {
					dep.sourceDescription = fmt.Sprintf("%s in %s %q", usage.SourceDescription, assetTypeName, asset.ID)
					dep.from = []string{asset.ID, refID}
				}

				dependencies = append(dependencies, deps...)
			}
		}
	}

	return dependencies, nil
}

func extractDependenciesFromConfiguration(referenceID string, usageConfiguration *anypb.Any, sc *SolutionContext) ([]*dependency, error) {
	info, ok := sc.Assets[referenceID]
	if !ok || info.FileDescriptorSet == nil || usageConfiguration == nil {
		return nil, nil
	}
	hasDeps, err := info.hasDependencies()
	if err != nil {
		return nil, err
	}
	if !hasDeps {
		return nil, nil
	}
	return collectDependencies(usageConfiguration, info.FileDescriptorSet)
}
