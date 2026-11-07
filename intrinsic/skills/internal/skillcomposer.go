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

// Package skillcomposer provides a client to fetch data about running skills. This includes
// additional details that cannot be retrieved from the installed assets service.
package skillcomposer

import (
	"context"
	"fmt"
	"sync"

	"intrinsic/assets/idutils"
	"intrinsic/assets/syncutils"
	"intrinsic/resources/service/resourcetyperuntime"
	"intrinsic/skills/internal/configmapwatcher"
	"intrinsic/skills/internal/skillinfo"
	"intrinsic/util/proto/sourcecodeinfoview"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	iopb "intrinsic/assets/proto/installation_origin_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
	smpb "intrinsic/skills/proto/skill_manifest_go_proto"
	skillspb "intrinsic/skills/proto/skills_go_proto"

	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/descriptorpb"
	corev1 "k8s.io/api/core/v1"
	"k8s.io/client-go/informers"
)

const (
	configMapLabelKey   = "app"
	configMapLabelValue = "skill-registry-config"

	idVersionKey       = "id_version"
	executeHandleKey   = "execute_handle"
	projectHandleKey   = "project_handle"
	skillInfoHandleKey = "skill_info_handle"
	validateHandleKey  = "validate_handle"
)

// options contains options for read operations on skills.
type options struct {
	// Each skill's information service is pinged as a health check.
	useHealthCheck bool
}

// Option is a functional option for reading skills.
type Option func(*options)

// WithHealthCheck configures read operations to perform a health check.
func WithHealthCheck(use bool) Option {
	return func(o *options) {
		o.useHealthCheck = use
	}
}

// GRPCTargets holds the gRPC service addresses of various skill methods. In production, these
// target addresses are always the same value.
type GRPCTargets struct {
	Validate  string
	Project   string
	Execute   string
	SkillInfo string
}

// SkillDetails contains comprehensive information about a Skill, including data not exposed by the
// installed assets service.
type SkillDetails struct {
	Skill     *skillspb.Skill
	Addresses *GRPCTargets
}

// Client can be used to retrieve details about a running Skill Asset.
type Client interface {
	BatchGetSkills(ctx context.Context, ids []string, opts ...Option) ([]*SkillDetails, error)
	GetSkill(ctx context.Context, id string, opts ...Option) (*SkillDetails, error)
	ListSkills(ctx context.Context, opts ...Option) ([]*SkillDetails, error)
}

type productionClient struct {
	rtrClient       resourcetyperuntime.Client
	skillInfoClient skillinfo.Client
	mu              sync.RWMutex
	// map of gRPC targets where the keys are IDVersion strings.
	addresses map[string]*GRPCTargets
	// cache map where the keys are IDVersion strings.
	cache syncutils.SyncMap[string, *skillspb.Skill]
}

func cloneOf[M proto.Message](m M) M {
	return proto.Clone(m).(M)
}

// NewClient returns a new skillcomposer.Client.
func NewClient(rtrClient resourcetyperuntime.Client, factory informers.SharedInformerFactory) Client {
	client := &productionClient{
		addresses:       make(map[string]*GRPCTargets),
		rtrClient:       rtrClient,
		skillInfoClient: skillinfo.NewCachedClient(),
		mu:              sync.RWMutex{},
	}
	configmapwatcher.StartConfigMapWatcher(factory,
		configmapwatcher.WithUpdaterFn(client.updateAddress),
		configmapwatcher.WithDeleterFn(client.deleteAddress),
		configmapwatcher.WithLabelSelector(fmt.Sprintf("%s=%s", configMapLabelKey, configMapLabelValue)),
	)
	return client
}

// BatchGetSkills gets a batch of skills by their IDs.
func (c *productionClient) BatchGetSkills(ctx context.Context, ids []string, opts ...Option) ([]*SkillDetails, error) {
	o := &options{}
	for _, opt := range opts {
		opt(o)
	}
	if len(ids) == 0 {
		return nil, nil
	}

	idVersionProtos, _, err := c.rtrClient.List(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list runtime info: %v", err)
	}
	idsToIDVersions := make(map[string]string)
	for _, idVersion := range idVersionProtos {
		skillID, err := idutils.RemoveVersionFrom(idVersion)
		if err != nil {
			log.Warningf("could not parse ID from IDVersion %q: %v", idVersion, err)
			continue
		}
		if _, exists := idsToIDVersions[skillID]; exists {
			return nil, status.Errorf(codes.Internal, "runtime DB contains multiple skills with the same ID %q", skillID)
		}
		idsToIDVersions[skillID] = idVersion
	}

	var idVersions []string
	for _, id := range ids {
		if _, exists := idsToIDVersions[id]; !exists {
			return nil, status.Errorf(codes.NotFound, "skill %q not found", id)
		}
		idVersions = append(idVersions, idsToIDVersions[id])
	}

	return c.toSkillDetails(ctx, idVersions, withHealthCheck(o.useHealthCheck))
}

// GetSkill gets a single skill by its ID.
func (c *productionClient) GetSkill(ctx context.Context, id string, opts ...Option) (*SkillDetails, error) {
	skills, err := c.BatchGetSkills(ctx, []string{id}, opts...)
	if err != nil {
		return nil, err
	}
	if len(skills) == 0 {
		return nil, status.Errorf(codes.NotFound, "skill %q not found", id)
	}
	return skills[0], nil
}

// ListSkills gets the SkillDetails for all skills.
func (c *productionClient) ListSkills(ctx context.Context, opts ...Option) ([]*SkillDetails, error) {
	o := &options{}
	for _, opt := range opts {
		opt(o)
	}

	idVersions, _, err := c.rtrClient.List(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "failed to list runtime info: %v", err)
	}

	// Prune cache entries that are no longer in the provided list of idVersions.
	// Because Kubernetes ConfigMap updates are not guaranteed to arrive in order,
	// this cleanup prevents stale entries from lingering in rare edge cases.
	activeIDVersions := make(map[string]struct{})
	for _, idVersion := range idVersions {
		activeIDVersions[idVersion] = struct{}{}
	}
	c.cache.Range(func(idVersion string, _ *skillspb.Skill) bool {
		if _, ok := activeIDVersions[idVersion]; !ok {
			c.cache.Delete(idVersion)
		}
		return true
	})

	if len(idVersions) == 0 {
		return nil, nil
	}

	return c.toSkillDetails(ctx, idVersions, withSkipNotFound(true), withHealthCheck(o.useHealthCheck))
}

func validateConfigMap(cm *corev1.ConfigMap) error {
	expectedKeys := []string{idVersionKey, executeHandleKey, projectHandleKey, skillInfoHandleKey, validateHandleKey}
	for _, key := range expectedKeys {
		if _, ok := cm.Data[key]; !ok {
			return status.Errorf(codes.InvalidArgument, "configmap in namespace %s missing key %s", cm.GetNamespace(), key)
		}
	}
	return nil
}

func (c *productionClient) updateAddress(cm *corev1.ConfigMap) {
	if err := validateConfigMap(cm); err != nil {
		log.Errorf("configmap is invalid so its entry will be ignored: %v", err)
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.addresses[cm.Data[idVersionKey]] = &GRPCTargets{
		Validate:  cm.Data[validateHandleKey],
		Project:   cm.Data[projectHandleKey],
		Execute:   cm.Data[executeHandleKey],
		SkillInfo: cm.Data[skillInfoHandleKey],
	}
}

func (c *productionClient) deleteAddress(cm *corev1.ConfigMap) {
	if err := validateConfigMap(cm); err != nil {
		log.Errorf("configmap is invalid so its entry will be ignored: %v", err)
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	delete(c.addresses, cm.Data[idVersionKey])
	c.skillInfoClient.ReleaseFromCache(cm.Data[idVersionKey])
	c.cache.Delete(cm.Data[idVersionKey])
}

func isSideloaded(origin iopb.InstallationOrigin) bool {
	return origin == iopb.InstallationOrigin_INSTALLATION_ORIGIN_SIDELOADED ||
		origin == iopb.InstallationOrigin_INSTALLATION_ORIGIN_INLINED ||
		// Consider skills loaded from save data as sideloaded. These skills were
		// sideloaded and then saved, which changes their installation origin to
		// SAVE_DATA. They are sideloaded skills from the user's perspective.
		origin == iopb.InstallationOrigin_INSTALLATION_ORIGIN_SAVE_DATA
}

func processParameterDescription(pm *smpb.ParameterMetadata, fds *descriptorpb.FileDescriptorSet, strippedFDS *descriptorpb.FileDescriptorSet) (*skillspb.ParameterDescription, error) {
	if pm == nil || pm.GetMessageFullName() == "" {
		return nil, nil
	}
	comments, err := sourcecodeinfoview.NestedFieldCommentMap(fds, pm.GetMessageFullName())
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not extract comments from file descriptor set: %v", err)
	}
	return &skillspb.ParameterDescription{
		DefaultValue:               pm.DefaultValue,
		ParameterDescriptorFileset: strippedFDS,
		ParameterMessageFullName:   pm.GetMessageFullName(),
		ParameterFieldComments:     comments,
	}, nil
}

func processReturnValueDescription(rm *smpb.ReturnMetadata, fds *descriptorpb.FileDescriptorSet, strippedFDS *descriptorpb.FileDescriptorSet) (*skillspb.ReturnValueDescription, error) {
	if rm == nil || rm.GetMessageFullName() == "" {
		return nil, nil
	}
	comments, err := sourcecodeinfoview.NestedFieldCommentMap(fds, rm.GetMessageFullName())
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not extract comments from file descriptor set: %v", err)
	}
	return &skillspb.ReturnValueDescription{
		DescriptorFileset:          strippedFDS,
		ReturnValueMessageFullName: rm.GetMessageFullName(),
		ReturnValueFieldComments:   comments,
	}, nil
}

func skillFromRTR(rtr *rtrpb.ResourceTypeRuntime) (*skillspb.Skill, error) {
	idvp, err := idutils.NewIDVersionPartsFromProto(rtr.GetMetadata().GetIdVersion())
	if err != nil {
		return nil, status.Errorf(codes.Internal, "invalid skill IDVersion: %v", err)
	}

	if rtr.GetMetadata().GetAssetType() != atypepb.AssetType_ASSET_TYPE_SKILL || rtr.GetSkill() == nil {
		return nil, status.Errorf(codes.Internal, "%q is not a skill", idvp.IDVersion())
	}

	skill := &skillspb.Skill{
		SkillName:   rtr.GetMetadata().GetIdVersion().GetId().GetName(),
		PackageName: rtr.GetMetadata().GetIdVersion().GetId().GetPackage(),
		Id:          idvp.ID(),
		IdVersion:   idvp.IDVersion(),
		Description: rtr.GetMetadata().GetDocumentation().GetDescription(),
		DisplayName: rtr.GetMetadata().GetDisplayName(),
		Sideloaded:  isSideloaded(rtr.GetInstallationOrigin()),
	}

	details := rtr.GetSkill().GetDetails()
	if details != nil {
		if deps := details.GetDependencies(); deps != nil {
			skill.ResourceSelectors = deps.GetRequiredEquipment()
		}
		if opts := details.GetOptions(); opts != nil && opts.GetSupportsCancellation() {
			skill.ExecutionOptions = &skillspb.ExecutionOptions{
				SupportsCancellation: true,
			}
		}

		if fds := rtr.GetMetadata().GetFileDescriptorSet(); fds != nil {
			strippedFDS := cloneOf(fds)
			for _, file := range strippedFDS.GetFile() {
				file.SourceCodeInfo = nil
			}

			paramDesc, err := processParameterDescription(details.GetParameter(), fds, strippedFDS)
			if err != nil {
				return nil, err
			}
			skill.ParameterDescription = paramDesc

			returnDesc, err := processReturnValueDescription(details.GetExecuteResult(), fds, strippedFDS)
			if err != nil {
				return nil, err
			}
			skill.ReturnValueDescription = returnDesc
		}
	}
	return skill, nil
}

// validateHealth pings the skill information service to determine if the skill is up and running. It
// returns an error if the skill is unhealthy.
func (c *productionClient) validateHealth(ctx context.Context, address string, idVersion string) error {
	_, err := c.skillInfoClient.Get(ctx, idVersion, address)
	return err
}

type toSkillDetailsOptions struct {
	// Skills that are not found do not generate an error.
	skipNotFound bool
	// Each skill's information service is pinged as a health check.
	useHealthCheck bool
}

type toSkillDetailsOption func(*toSkillDetailsOptions)

// WithSkipNotFound configures toSkillDetails to skip skills that are not found.
func withSkipNotFound(skip bool) toSkillDetailsOption {
	return func(o *toSkillDetailsOptions) {
		o.skipNotFound = skip
	}
}

// withHealthCheck configures toSkillDetails to perform a health check.
func withHealthCheck(use bool) toSkillDetailsOption {
	return func(o *toSkillDetailsOptions) {
		o.useHealthCheck = use
	}
}

func (c *productionClient) getSnapshotTargets(idVersions []string, skipNotFound bool) (map[string]*GRPCTargets, error) {
	c.mu.RLock()
	defer c.mu.RUnlock()
	targets := make(map[string]*GRPCTargets)
	for _, id := range idVersions {
		addr, exists := c.addresses[id]
		if !exists {
			if skipNotFound {
				continue
			}
			return nil, status.Errorf(codes.NotFound, "skill %q not found", id)
		}
		targets[id] = addr
	}
	return targets, nil
}

func (c *productionClient) toSkillDetails(ctx context.Context, idVersions []string, opts ...toSkillDetailsOption) ([]*SkillDetails, error) {
	options := &toSkillDetailsOptions{}
	for _, opt := range opts {
		opt(options)
	}
	if len(idVersions) == 0 {
		return nil, nil
	}

	targets, err := c.getSnapshotTargets(idVersions, options.skipNotFound)
	if err != nil {
		return nil, err
	}

	// A valid skill must have:
	// - A ConfigMap picked up by the Kubernetes watcher.
	// - An entry in the cache or resource type runtime DB.
	skillDetailsMap := make(map[string]*SkillDetails)
	var toFetch []string

	// Check if any entries are already present in the cache.
	for idVersion, addresses := range targets {
		if skill, ok := c.cache.Load(idVersion); ok {
			skillDetailsMap[idVersion] = &SkillDetails{
				Skill:     proto.Clone(skill).(*skillspb.Skill),
				Addresses: addresses,
			}
			continue
		}
		toFetch = append(toFetch, idVersion)
	}

	// Fetch any items missing from the cache from the resource type runtime DB.
	if len(toFetch) > 0 {
		rtrs, err := c.rtrClient.BatchGet(ctx, toFetch)
		if err != nil {
			return nil, status.Errorf(status.Code(err), "failed to retrieve runtime info for skills: %v", err)
		}

		for _, rtr := range rtrs {
			// Note that NotFound and health check logic is performed at the bottom of this function.
			if rtr.GetMetadata().GetAssetType() != atypepb.AssetType_ASSET_TYPE_SKILL || rtr.GetSkill() == nil {
				continue
			}

			skill, err := skillFromRTR(rtr)
			if err != nil {
				return nil, err
			}

			idVersion := idutils.IDVersionFromProtoUnchecked(rtr.GetMetadata().GetIdVersion())
			c.cache.Store(idVersion, skill)
			skillDetailsMap[idVersion] = &SkillDetails{
				Skill:     proto.Clone(skill).(*skillspb.Skill),
				Addresses: targets[idVersion],
			}
		}
	}

	// Construct the final results in order and perform health checks if requested.
	var skillDetails []*SkillDetails
	for _, idVersion := range idVersions {
		sd, exists := skillDetailsMap[idVersion]
		if !exists {
			if options.skipNotFound {
				continue
			}
			return nil, status.Errorf(codes.NotFound, "skill %q not found", idVersion)
		}

		if options.useHealthCheck {
			if err := c.validateHealth(ctx, sd.Addresses.SkillInfo, idVersion); err != nil {
				return nil, err
			}
		}
		skillDetails = append(skillDetails, sd)
	}

	return skillDetails, nil
}
