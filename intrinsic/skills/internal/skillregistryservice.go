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

// Package skillregistryservice contains a skill registry server. This file
// contains a version of the server that uses a fixed in-memory set of skills.
package skillregistryservice

import (
	"context"
	"fmt"
	"slices"
	"sort"
	"sync"

	"intrinsic/assets/idutils"
	"intrinsic/skills/internal/skillcomposer"
	"intrinsic/skills/internal/skillfilter"
	"intrinsic/skills/internal/skillruntime"
	"intrinsic/util/pagination/pagetoken"
	"intrinsic/util/proto/descriptorcompatibility"

	log "github.com/golang/glog"
	"github.com/pborman/uuid"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	btregistryinternalpb "intrinsic/skills/internal/proto/behavior_tree_registry_internal_go_proto"
	skillregistryinternalpb "intrinsic/skills/internal/proto/skill_registry_internal_go_proto"
	btregistrypb "intrinsic/skills/proto/behavior_tree_registry_go_proto"
	skillregistryconfigpb "intrinsic/skills/proto/skill_registry_config_go_proto"
	skillregistrypb "intrinsic/skills/proto/skill_registry_go_proto"
	skillruntimepb "intrinsic/skills/proto/skill_runtime_go_proto"
	skillspb "intrinsic/skills/proto/skills_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

const (
	executorServiceTimeout    = 30
	listSkillsDefaultPageSize = 20
	listSkillsMaxPageSize     = 200
)

func cloneOf[M proto.Message](m M) M {
	return proto.Clone(m).(M)
}

func cloneProtoSlice[M proto.Message](in []M) []M {
	out := make([]M, 0, len(in))
	for _, m := range in {
		out = append(out, cloneOf(m))
	}
	return out
}

func skillIDEqualFunc(id string) func(*skillregistryconfigpb.SkillRegistration) bool {
	return func(reg *skillregistryconfigpb.SkillRegistration) bool {
		return reg.GetSkill().GetId() == id
	}
}

func behaviorTreeIDEqualFunc(id string) func(*skillregistryconfigpb.BehaviorTreeRegistration) bool {
	return func(reg *skillregistryconfigpb.BehaviorTreeRegistration) bool {
		return reg.GetBehaviorTree().GetDescription().GetId() == id
	}
}

type skillDataHandler interface {
	// ListSkills returns the list of skills served by this handler.
	ListSkills(ctx context.Context) ([]*skillregistryconfigpb.SkillRegistration, error)
	// GetSkill returns the skill given by the ID.
	GetSkill(ctx context.Context, id string) (*skillregistryconfigpb.SkillRegistration, error)
	// AddSkill adds a new skill to this handler. Some handlers do not implement this method.
	AddSkill(bt *skillregistryconfigpb.SkillRegistration) error
	// RemoveSkill removes a skill from this handler. Some handlers do not implement this method.
	RemoveSkill(id string) error
}

type behaviorTreeDataHandler interface {
	// The BehaviorTrees registered with the handler.
	BehaviorTrees(ctx context.Context) ([]*skillregistryconfigpb.BehaviorTreeRegistration, error)
	// Add a Behavior Tree to the config
	AddBehaviorTree(ctx context.Context, bt *skillregistryconfigpb.BehaviorTreeRegistration) error
	// Remove a Behavior Tree from the config
	RemoveBehaviorTree(context.Context, string) error
}

// SkillRegistryServer contains data associated with a skill registry server.
type SkillRegistryServer struct {
	skillDataHandler
	behaviorTreeDataHandler
}

type skillsInMemory struct {
	// config is the configuration proto that contains the skills to be served.
	skills []*skillregistryconfigpb.SkillRegistration
	// mu is a Mutex used for the above proto.
	mu sync.RWMutex
}

// skillRegistryInMemory stores the SkillRegistryConfig proto in memory.
type behaviorTreesInMemory struct {
	// config is the configuration proto that contains the skills to be served.
	behaviorTrees []*skillregistryconfigpb.BehaviorTreeRegistration
	// mu is a Mutex used for the above proto.
	mu sync.RWMutex
}

// skillsInProduction is an implementation of skillDataHandler that retrieves skill data from the
// installed assets service and kubernetes configmaps. It also pings the skill's information
// service as a "health" check.
type skillsInProduction struct {
	// skillComposer is used to construct a skills from multiple sources.
	skillComposer skillcomposer.Client
}

type runtimeDBBehaviorTrees struct {
	client skillruntime.Client
}

var uniqueIDNew = uuid.New // tests can assign it to something more predictable

func (handler *skillsInMemory) ListSkills(ctx context.Context) ([]*skillregistryconfigpb.SkillRegistration, error) {
	handler.mu.RLock()
	defer handler.mu.RUnlock()
	return cloneProtoSlice(handler.skills), nil
}

func (handler *skillsInMemory) GetSkill(ctx context.Context, id string) (*skillregistryconfigpb.SkillRegistration, error) {
	if !idutils.IsID(id) {
		return nil, status.Errorf(codes.InvalidArgument, "invalid skill ID: %q", id)
	}
	handler.mu.RLock()
	defer handler.mu.RUnlock()
	for _, skill := range handler.skills {
		if skill.GetSkill().GetId() == id {
			return cloneOf(skill), nil
		}
	}
	return nil, status.Errorf(codes.NotFound, "skill %q not found", id)
}

func (handler *skillsInMemory) AddSkill(skill *skillregistryconfigpb.SkillRegistration) error {
	handler.mu.Lock() // handler.skills is modified
	defer handler.mu.Unlock()
	handler.skills = slices.DeleteFunc(handler.skills, skillIDEqualFunc(skill.GetSkill().GetId()))
	handler.skills = append(handler.skills, cloneOf(skill))
	return nil
}

func (handler *skillsInMemory) RemoveSkill(id string) error {
	handler.mu.Lock() // handler.skills is modified
	defer handler.mu.Unlock()
	handler.skills = slices.DeleteFunc(handler.skills, skillIDEqualFunc(id))
	return nil
}

func (handler *behaviorTreesInMemory) BehaviorTrees(context.Context) ([]*skillregistryconfigpb.BehaviorTreeRegistration, error) {
	handler.mu.RLock()
	defer handler.mu.RUnlock()
	return cloneProtoSlice(handler.behaviorTrees), nil
}

// Adds a behavior tree to the in-memory registry.
func (handler *behaviorTreesInMemory) AddBehaviorTree(ctx context.Context, btr *skillregistryconfigpb.BehaviorTreeRegistration) error {
	handler.mu.Lock() // handler.behaviorTrees is modified
	defer handler.mu.Unlock()
	id := btr.GetBehaviorTree().GetDescription().GetId()
	handler.behaviorTrees = slices.DeleteFunc(handler.behaviorTrees, behaviorTreeIDEqualFunc(id))
	handler.behaviorTrees = append(handler.behaviorTrees, cloneOf(btr))
	return nil
}

// Removes a behavior tree from the in-memory registry.
func (handler *behaviorTreesInMemory) RemoveBehaviorTree(ctx context.Context, id string) error {
	handler.mu.Lock() // handler.behaviorTrees is modified
	defer handler.mu.Unlock()
	handler.behaviorTrees = slices.DeleteFunc(handler.behaviorTrees, behaviorTreeIDEqualFunc(id))
	return nil
}

func (handler *skillsInProduction) ListSkills(ctx context.Context) ([]*skillregistryconfigpb.SkillRegistration, error) {
	wrappedSkills, err := handler.skillComposer.ListSkills(ctx, skillcomposer.WithHealthCheck(false))
	if err != nil {
		return nil, err
	}
	srs := make([]*skillregistryconfigpb.SkillRegistration, 0, len(wrappedSkills))
	for _, wrappedSkill := range wrappedSkills {
		srs = append(srs, &skillregistryconfigpb.SkillRegistration{
			Skill: wrappedSkill.Skill,
			ValidateHandle: &skillspb.SkillHandle{
				GrpcTarget: wrappedSkill.Addresses.Validate,
			},
			ProjectHandle: &skillspb.SkillHandle{
				GrpcTarget: wrappedSkill.Addresses.Project,
			},
			ExecuteHandle: &skillspb.SkillHandle{
				GrpcTarget: wrappedSkill.Addresses.Execute,
			},
			SkillInfoHandle: &skillspb.SkillHandle{
				GrpcTarget: wrappedSkill.Addresses.SkillInfo,
			},
		})
	}
	return srs, nil
}

func (handler *skillsInProduction) GetSkill(ctx context.Context, id string) (*skillregistryconfigpb.SkillRegistration, error) {
	if !idutils.IsID(id) {
		return nil, status.Errorf(codes.InvalidArgument, "invalid skill ID: %q", id)
	}

	wrappedSkill, err := handler.skillComposer.GetSkill(ctx, id, skillcomposer.WithHealthCheck(true))
	if err != nil {
		return nil, err
	}
	return &skillregistryconfigpb.SkillRegistration{
		Skill: wrappedSkill.Skill,
		ValidateHandle: &skillspb.SkillHandle{
			GrpcTarget: wrappedSkill.Addresses.Validate,
		},
		ProjectHandle: &skillspb.SkillHandle{
			GrpcTarget: wrappedSkill.Addresses.Project,
		},
		ExecuteHandle: &skillspb.SkillHandle{
			GrpcTarget: wrappedSkill.Addresses.Execute,
		},
		SkillInfoHandle: &skillspb.SkillHandle{
			GrpcTarget: wrappedSkill.Addresses.SkillInfo,
		},
	}, nil
}

func (handler *skillsInProduction) AddSkill(skill *skillregistryconfigpb.SkillRegistration) error {
	return status.Error(codes.Unimplemented, "AddSkill requested but it is only supported when using in-memory skill registry")
}

func (handler *skillsInProduction) RemoveSkill(id string) error {
	return status.Error(codes.Unimplemented, "RemoveSkill requested but it is only supported when using in-memory skill registry")
}

func (handler *runtimeDBBehaviorTrees) BehaviorTrees(ctx context.Context) ([]*skillregistryconfigpb.BehaviorTreeRegistration, error) {
	idvs, err := handler.client.List(ctx)
	if err != nil {
		return nil, fmt.Errorf("unable to list behavior trees: %w", err)
	}
	srs, err := handler.client.BatchGet(ctx, idvs)
	if err != nil {
		return nil, fmt.Errorf("unable to get behavior trees: %w", err)
	}
	var behaviorTrees []*skillregistryconfigpb.BehaviorTreeRegistration
	for _, sr := range srs {
		behaviorTrees = append(behaviorTrees, sr.GetRegistration())
	}
	return behaviorTrees, nil
}

func (handler *runtimeDBBehaviorTrees) AddBehaviorTree(ctx context.Context, btr *skillregistryconfigpb.BehaviorTreeRegistration) error {
	id := btr.GetBehaviorTree().GetDescription().GetId()
	if err := handler.RemoveBehaviorTree(ctx, id); err != nil {
		return fmt.Errorf("unable to remove behavior trees of id %q prior to adding a new tree: %v", id, err)
	}
	return handler.client.Add(ctx, &skillruntimepb.SkillRuntime{
		IdVersion: btr.GetBehaviorTree().GetDescription().GetIdVersion(),
		Registration: &skillregistryconfigpb.BehaviorTreeRegistration{
			BehaviorTree: btr.GetBehaviorTree(),
		},
	})
}

func (handler *runtimeDBBehaviorTrees) RemoveBehaviorTree(ctx context.Context, id string) error {
	behaviorTrees, err := handler.BehaviorTrees(ctx)
	if err != nil {
		return err
	}
	matches := behaviorTreeIDEqualFunc(id)
	var toDelete []string
	for _, behaviorTree := range behaviorTrees {
		if matches(behaviorTree) {
			toDelete = append(toDelete, behaviorTree.GetBehaviorTree().GetDescription().GetIdVersion())
		}
	}
	return handler.client.BatchDelete(ctx, toDelete)
}

func reconcileSkill(skill *skillspb.Skill) error {
	if skill == nil {
		return nil
	}
	// Note that Reconcile currently never returns an error, but we check the errors anyway.
	if err := descriptorcompatibility.Reconcile(skill.GetParameterDescription().GetParameterDescriptorFileset()); err != nil {
		return err
	}
	if err := descriptorcompatibility.Reconcile(skill.GetReturnValueDescription().GetDescriptorFileset()); err != nil {
		return err
	}
	return nil
}

// GetSkills returns all skills that have been registered.
func (s *SkillRegistryServer) GetSkills(ctx context.Context, _ *emptypb.Empty) (*skillregistrypb.GetSkillsResponse, error) {
	res := &skillregistrypb.GetSkillsResponse{}
	nextPageToken := ""
	for {
		listRes, err := s.ListSkills(ctx, &skillregistrypb.ListSkillsRequest{
			PageSize:  listSkillsMaxPageSize,
			PageToken: nextPageToken,
		})
		if err != nil {
			return nil, err
		}
		res.Skills = append(res.Skills, listRes.GetSkills()...)
		nextPageToken = listRes.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}
	return res, nil
}

// GetSkill returns a single skill by name.
func (s *SkillRegistryServer) GetSkill(ctx context.Context, request *skillregistrypb.GetSkillRequest) (*skillregistrypb.GetSkillResponse, error) {
	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not retrieve behavior tree information: %v", err)
	}
	for _, r := range behaviorTrees {
		if r.GetBehaviorTree().GetDescription().GetId() == request.GetId() {
			skillInfo := cloneOf(r.GetBehaviorTree().GetDescription())
			skillInfo.BehaviorTreeDescription = &skillspb.BehaviorTreeDescription{}
			return &skillregistrypb.GetSkillResponse{Skill: skillInfo}, nil
		}
	}

	sr, err := s.skillDataHandler.GetSkill(ctx, request.GetId())
	if err != nil {
		return nil, err
	}
	return &skillregistrypb.GetSkillResponse{Skill: sr.Skill}, nil
}

// ListSkills returns all skills in the registry that match the request.
func (s *SkillRegistryServer) ListSkills(ctx context.Context, req *skillregistrypb.ListSkillsRequest) (*skillregistrypb.ListSkillsResponse, error) {
	if req.GetPageSize() < 0 {
		return nil, status.Error(codes.InvalidArgument, "page_size must be non-negative")
	}
	if req.GetPageSize() == 0 {
		req.PageSize = listSkillsDefaultPageSize
	} else if req.GetPageSize() > listSkillsMaxPageSize {
		req.PageSize = listSkillsMaxPageSize
	}

	startAfterID := ""
	// Validate the page token. It must be valid (i.e. we can parse it) and the request within must
	// match the current request (except for the page size and token).
	if req.GetPageToken() != "" {
		pt, err := pagetoken.Deopacify(req.GetPageToken())
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid page token: %v", err)
		}
		ptReq := &skillregistrypb.ListSkillsRequest{}
		if err := pagetoken.RecoverRequest(pt, ptReq); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid page token: %v", err)
		}
		// The request must not be changed during pagination (except for the page size). Validate that
		// the request encoded in the page token is otherwise the same as the current request.
		if ptReq.GetFilter() != req.GetFilter() {
			return nil, status.Error(codes.InvalidArgument, "invalid page token: request changed during pagination")
		}
		startAfterID = pt.GetStartAfterId()
	}

	matcher, err := skillfilter.Parse(req.GetFilter())
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid filter: %v", err)
	}

	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not retrieve behavior tree information: %v", err)
	}
	skills, err := s.skillDataHandler.ListSkills(ctx)
	if err != nil {
		return nil, err
	}

	skillsByID := map[string]*skillspb.Skill{}
	for _, sr := range skills {
		skillsByID[sr.GetSkill().GetId()] = sr.GetSkill()
	}

	for _, br := range behaviorTrees {
		skillInfo := cloneOf(br.GetBehaviorTree().GetDescription())
		skillInfo.BehaviorTreeDescription = &skillspb.BehaviorTreeDescription{}
		skillsByID[br.GetBehaviorTree().GetDescription().GetId()] = skillInfo
	}

	// All matching skills contains all skills (including pBTs pretending to be skills) that match the
	// request without pagination applied.
	var allMatchingSkills []*skillspb.Skill
	for _, skill := range skillsByID {
		// Remove any skills that don't match the filter.
		if !matcher.Match(skill) {
			continue
		}
		allMatchingSkills = append(allMatchingSkills, skill)
	}
	sort.Slice(allMatchingSkills, func(i, j int) bool {
		return allMatchingSkills[i].GetId() < allMatchingSkills[j].GetId()
	})
	// Return early if we filtered out all candidate skills. No need to go through the trouble of
	// dividing an empty slice.
	if len(allMatchingSkills) == 0 {
		return &skillregistrypb.ListSkillsResponse{}, nil
	}

	pageStartIndex := 0
	if startAfterID != "" {
		for i, s := range allMatchingSkills {
			if s.GetId() == startAfterID {
				pageStartIndex = i + 1
				break
			}
		}
	}
	pageEndIndex := pageStartIndex + int(req.GetPageSize())
	pageSkills := allMatchingSkills[pageStartIndex:min(len(allMatchingSkills), pageEndIndex)]

	res := &skillregistrypb.ListSkillsResponse{
		Skills: pageSkills,
	}

	if len(allMatchingSkills) > pageEndIndex {
		// Unset the provided page token so that we don't keep encoding it in the next page token.
		// Otherwise we would be creating a page token russian doll.
		req.PageToken = ""
		lastPageSkill := pageSkills[len(pageSkills)-1]
		npt, err := pagetoken.Opacify(req, lastPageSkill.GetId(), []string{})
		if err != nil {
			return nil, status.Errorf(codes.Internal, "error creating next page token: %v", err)
		}
		res.NextPageToken = npt
	}

	return res, nil
}

// GetBehaviorTree returns a single parameterizable behavior tree by name.
func (s *SkillRegistryServer) GetBehaviorTree(ctx context.Context, request *btregistryinternalpb.GetBehaviorTreeRequest) (*btregistryinternalpb.GetBehaviorTreeResponse, error) {
	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not retrieve behavior tree information: %v", err)
	}
	for _, bt := range behaviorTrees {
		if bt.GetBehaviorTree().GetDescription().GetId() == request.GetId() {
			return &btregistryinternalpb.GetBehaviorTreeResponse{BehaviorTree: bt.GetBehaviorTree()}, nil
		}
	}

	return nil, status.Errorf(codes.NotFound, "BehaviorTree %q not found", request.GetId())
}

// RegisterOrUpdateBehaviorTree adds a parameterizable Behavior Tree to the registry.
func (s *SkillRegistryServer) RegisterOrUpdateBehaviorTree(ctx context.Context, request *btregistrypb.RegisterOrUpdateBehaviorTreeRequest) (*btregistrypb.RegisterOrUpdateBehaviorTreeResponse, error) {
	if request.GetRegistration().GetBehaviorTree().Description == nil {
		return nil, status.Errorf(codes.InvalidArgument, "Behavior tree has no skill description")
	}

	skillID := request.GetRegistration().GetBehaviorTree().GetDescription().GetId()
	if len(skillID) == 0 {
		return nil, status.Errorf(codes.FailedPrecondition, "trying to register a behavior tree with an empty id")
	}
	// See b/382250177 for discussion on why we need to reconcile file descriptor sets.
	if err := reconcileSkill(request.GetRegistration().GetBehaviorTree().GetDescription()); err != nil {
		return nil, err
	}

	err := s.behaviorTreeDataHandler.AddBehaviorTree(ctx, request.GetRegistration())
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "failed to add behavior tree: %v", err)
	}

	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.NotFound, "could not retrieve behavior tree information: %v", err)
	}

	var btNames []string
	for _, bt := range behaviorTrees {
		btNames = append(btNames, bt.GetBehaviorTree().GetDescription().GetId())
	}

	log.InfoContextf(ctx, "RegisterOrUpdateBehaviorTree(): registered behavior trees: %s", btNames)
	return &btregistrypb.RegisterOrUpdateBehaviorTreeResponse{}, nil
}

// UnregisterBehaviorTree removes a parameterizable Behavior Tree from the skill registry.
func (s *SkillRegistryServer) UnregisterBehaviorTree(ctx context.Context, request *btregistrypb.UnregisterBehaviorTreeRequest) (*emptypb.Empty, error) {
	skillID := request.GetId()
	if len(skillID) == 0 {
		return nil, status.Errorf(codes.InvalidArgument, "UnregisterBehaviorTreeRequest must contain a behavior tree id")
	}

	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not retrieve skill registry: %v", err)
	}

	// Check that the behavior tree exists in the registry and return an error if it does not. This is
	// done to avoid removing a skill with the same id that is not registered as a behavior tree.
	found := false
	for _, bt := range behaviorTrees {
		if bt.GetBehaviorTree().GetDescription().GetId() == skillID {
			found = true
			break
		}
	}
	if !found {
		return nil, status.Errorf(codes.NotFound, "BehaviorTree %q not found", skillID)
	}

	if _, err := s.RemoveSkill(ctx, &skillregistryinternalpb.RemoveSkillRequest{Id: skillID}); err != nil {
		return nil, status.Errorf(codes.Internal, "Failed to remove behavior tree and skill: %v", err)
	}

	return &emptypb.Empty{}, nil
}

// TODO(b/459765477): Remove this method once all legacy pBTs have been migrated to Process assets.
//
// ListBehaviorTreeSkills returns the skill descriptions for all registered
// legacy pBTs.
func (s *SkillRegistryServer) ListBehaviorTreeSkills(ctx context.Context, _ *btregistrypb.ListBehaviorTreeSkillsRequest) (*btregistrypb.ListBehaviorTreeSkillsResponse, error) {
	behaviorTrees, err := s.behaviorTreeDataHandler.BehaviorTrees(ctx)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "could not retrieve behavior tree information: %v", err)
	}

	skills := make([]*skillspb.Skill, 0, len(behaviorTrees))
	for _, br := range behaviorTrees {
		skill := br.GetBehaviorTree().GetDescription()
		skill.BehaviorTreeDescription = &skillspb.BehaviorTreeDescription{}
		skills = append(skills, skill)
	}

	sort.Slice(skills, func(i, j int) bool {
		return skills[i].GetId() < skills[j].GetId()
	})

	return &btregistrypb.ListBehaviorTreeSkillsResponse{
		Skills: skills,
	}, nil
}

// RegisterOrUpdateSkill registers (or updates) a skill.
func (s *SkillRegistryServer) RegisterOrUpdateSkill(ctx context.Context, request *skillregistryinternalpb.RegisterOrUpdateSkillRequest) (*emptypb.Empty, error) {
	if len(request.GetSkillRegistration().GetSkill().GetId()) == 0 {
		return nil, status.Errorf(codes.FailedPrecondition, "trying to register a skill with an empty id")
	}

	if err := reconcileSkill(request.GetSkillRegistration().GetSkill()); err != nil {
		return nil, err
	}

	if err := s.skillDataHandler.AddSkill(request.GetSkillRegistration()); err != nil {
		return nil, err
	}
	return &emptypb.Empty{}, nil
}

func (s *SkillRegistryServer) RemoveSkill(ctx context.Context, request *skillregistryinternalpb.RemoveSkillRequest) (*skillregistryinternalpb.RemoveSkillResponse, error) {
	// Keep going if the error is UNIMPLEMENTED. The production data handler does
	// not support [RemoveSkill], but if there's a behavior tree registered it
	// should still be removed. No implementation of [RemoveSkill] will fail if
	// the ID is not found. This enables moving through to [RemoveBehaviorTree]
	// below even if there isn't a skill by the same ID.
	if err := s.skillDataHandler.RemoveSkill(request.GetId()); err != nil && status.Code(err) != codes.Unimplemented {
		return nil, err
	}
	if err := s.behaviorTreeDataHandler.RemoveBehaviorTree(ctx, request.GetId()); err != nil {
		return nil, err
	}
	return &skillregistryinternalpb.RemoveSkillResponse{}, nil
}

// NewInMemory creates a new SkillRegistryServer from the passed-in slices.
// This object can then be registered in a GRPC server using
// RegisterSkillRegistryServer.
func NewInMemory(skills []*skillregistryconfigpb.SkillRegistration, behaviorTrees []*skillregistryconfigpb.BehaviorTreeRegistration) *SkillRegistryServer {
	return &SkillRegistryServer{
		skillDataHandler: &skillsInMemory{
			skills: cloneProtoSlice(skills),
		},
		behaviorTreeDataHandler: &behaviorTreesInMemory{
			behaviorTrees: cloneProtoSlice(behaviorTrees),
		},
	}
}

// NewCombined creates a new SkillRegistryServer where the data for Skills is sourced from
// the merge of newSkillsInProduction and runtimeDBBehaviorTrees handlers.
func NewCombined(skillRuntimeClient skillruntime.Client, skillComposerClient skillcomposer.Client) *SkillRegistryServer {
	return &SkillRegistryServer{
		behaviorTreeDataHandler: &runtimeDBBehaviorTrees{skillRuntimeClient},
		skillDataHandler: &skillsInProduction{
			skillComposer: skillComposerClient,
		},
	}
}
