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

// Package solutionservice provides a server implementation of the SolutionService.
package solutionservice

import (
	"cmp"
	"context"
	"encoding/base64"
	"fmt"
	"slices"
	"strings"
	"time"

	"intrinsic/assets/idutils"
	"intrinsic/kubernetes/acl/clientcontext"
	"intrinsic/util/pagination/pagesize"
	"intrinsic/util/pagination/pagetoken"

	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	ipb "intrinsic/assets/proto/id_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	ppb "intrinsic/config/proto/process_go_proto"
	btpb "intrinsic/executive/proto/behavior_tree_go_proto"
	svcpb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	spb "intrinsic/frontend/solution_service/proto/status_go_proto"
	"intrinsic/frontend/solution_service/sequencelist"
	sigrpcpb "intrinsic/kubernetes/workcell_spec/proto/solution_internal_go_proto"
	sipb "intrinsic/kubernetes/workcell_spec/proto/solution_internal_go_proto"
	loggerpb "intrinsic/logging/proto/logger_service_go_proto"
	srgrpcpb "intrinsic/skills/proto/skill_registry_go_proto"
	srpb "intrinsic/skills/proto/skill_registry_go_proto"
	aspb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"
	hssagrpcpb "intrinsic/storage/hot_shared_state/proto/application_service_go_proto"

	epb "google.golang.org/protobuf/types/known/emptypb"
	timestamppb "google.golang.org/protobuf/types/known/timestamppb"
)

const (
	maxBatchGetBehaviorTrees    = 50
	maxBatchUpdateBehaviorTrees = 50

	// Maximum number of log items to fetch from the logger in a single request
	// when building a sequence list page. Each log item contains the full
	// Process state and can be large, so while sequence list pages can be large,
	// log item batches must stay small.
	sequenceListLogItemBatchSize = 10

	// Time after which GetSequenceList stops fetching more log items and returns
	// a page with the items found so far. This keeps individual requests short
	// for executions with large log items.
	sequenceListTimeBudget = 5 * time.Second

	executiveOperationEventSource = "executive.operation_state"
	executiveOperationFilterLabel = "executive_operation_name"
)

var behaviorTreesPageSizeValidator = pagesize.Validator{
	// Page size values are based on the API documentation.
	Default: 20,
	Max:     50,
}

// LINT.IfChange(sequence_list_page_size)
var sequenceListPageSizeValidator = pagesize.Validator{
	// Page size values are based on the API documentation.
	Default: 100,
	Max:     1000,
}

// LINT.ThenChange(//intrinsic/frontend/onprem/executive/action_list.ts:sequence_list_page_size)

// SolutionSaver saves the current state of the solution.
type SolutionSaver interface {
	Save(ctx context.Context) error
}

// Server is an implementation of the SolutionService.
type Server struct {
	hss      hssagrpcpb.HotSharedStateApplicationServiceClient
	internal sigrpcpb.SolutionInternalClient
	skillreg srgrpcpb.SkillRegistryClient
	iac      iagrpcpb.InstalledAssetsReaderClient
	logger   loggerpb.DataLoggerClient
	saver    SolutionSaver
	now      func() time.Time
}

// Options specifies the configuration for a [Server].
type Options struct {
	HSS             hssagrpcpb.HotSharedStateApplicationServiceClient
	Internal        sigrpcpb.SolutionInternalClient
	SkillRegistry   srgrpcpb.SkillRegistryClient
	InstalledAssets iagrpcpb.InstalledAssetsReaderClient
	Logger          loggerpb.DataLoggerClient
	Saver           SolutionSaver
}

// New creates a new [Server].
func New(opts *Options) *Server {
	return &Server{
		hss:      opts.HSS,
		internal: opts.Internal,
		skillreg: opts.SkillRegistry,
		iac:      opts.InstalledAssets,
		logger:   opts.Logger,
		saver:    opts.Saver,
		now:      time.Now,
	}
}

func validateAllSkillsReady(ctx context.Context, skillRegistryClient srgrpcpb.SkillRegistryClient, installedAssetsReaderClient iagrpcpb.InstalledAssetsReaderClient) error {
	var skillIDs []*ipb.Id
	var pageToken string
	for {
		resp, err := installedAssetsReaderClient.ListInstalledAssets(ctx, &iapb.ListInstalledAssetsRequest{
			StrictFilter: &iapb.ListInstalledAssetsRequest_Filter{
				// TODO(b/398184366): Test status of other asset types like services.
				AssetTypes: []atypepb.AssetType{atypepb.AssetType_ASSET_TYPE_SKILL},
			},
			View:      viewpb.AssetViewType_ASSET_VIEW_TYPE_BASIC,
			PageToken: pageToken,
		})
		if err != nil {
			return fmt.Errorf("unable to list installed skills: %w", err)
		}
		for _, s := range resp.GetInstalledAssets() {
			skillIDs = append(skillIDs, s.GetMetadata().GetIdVersion().GetId())
		}
		if resp.GetNextPageToken() == "" {
			break
		}
		pageToken = resp.GetNextPageToken()
	}
	if len(skillIDs) == 0 {
		return nil
	}

	for _, s := range skillIDs {
		id := idutils.IDFromProtoUnchecked(s)
		if _, err := skillRegistryClient.GetSkill(ctx, &srpb.GetSkillRequest{Id: id}); err != nil {
			return fmt.Errorf("skill %q not ready: %w", id, err)
		}
	}

	return nil
}

func behaviorTreeFromRunnables(runnables map[string]*ppb.Process_Runnable, name string) (*btpb.BehaviorTree, error) {
	runnable, ok := runnables[name]
	if !ok {
		return nil, status.Errorf(codes.NotFound, "runnable %q not found in process", name)
	}
	r, ok := runnable.Runnable.(*ppb.Process_Runnable_BehaviorTree)
	if !ok {
		return nil, status.Errorf(codes.FailedPrecondition, "runnable %q exists but does not have a behavior tree", name)
	}
	return ensureConsistentName(name, r.BehaviorTree), nil
}

func (s *Server) GetStatus(ctx context.Context, req *svcpb.GetStatusRequest) (*spb.Status, error) {
	statusResp, err := s.internal.GetWorkcellStatus(ctx, &epb.Empty{})
	resp := &spb.Status{
		ClusterName: statusResp.GetClusterName(),
	}
	if err != nil {
		if status.Code(err) == codes.NotFound {
			// The platform could be ready in this state (PLATFORM_READY). However, we
			// cannot yet determine this reliably because GetWorkcellStatus returns
			// [codes.NotFound] if there is no app chart and does not check the status
			// of platform services. In the future it should check the health of all
			// platform services and return a platform state. We would then determine
			// the state of the solution based on this platform state.
			resp.State = spb.Status_STATE_UNSPECIFIED
			return resp, nil
		}
		return nil, status.Errorf(codes.Internal, "failed to get workcell status: %v", err)
	}

	appResp, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		if status.Code(err) == codes.NotFound {
			// Return [codes.NotFound] for consumers to know that the solution is not
			// available. Once we have a reliable PLATFORM_READY state (see above), we should just return
			// the state without an error.
			return nil, status.Errorf(codes.NotFound, "no solution found: %v", err)
		}
		return nil, status.Errorf(codes.Internal, "failed to get solution information: %v", err)
	}

	app := appResp.GetApplication()
	resp.Name = app.GetMetadata().GetName()
	resp.DisplayName = app.GetMetadata().GetDisplayName()
	resp.PlatformVersion = statusResp.GetVersion()
	switch app.GetOperationMode() {
	case opmodepb.OperationMode_REAL_HARDWARE:
		resp.Simulated = false
	default:
		resp.Simulated = true
	}
	switch statusResp.GetStatus() {
	case sipb.GetWorkcellStatusResponse_HEALTHY:
		resp.State = spb.Status_READY
	case sipb.GetWorkcellStatusResponse_PENDING:
		resp.State = spb.Status_PLATFORM_DEPLOYING
	case sipb.GetWorkcellStatusResponse_ERROR:
		resp.State = spb.Status_ERROR
	case sipb.GetWorkcellStatusResponse_STOPPING:
		resp.State = spb.Status_STOPPING
	default:
		resp.State = spb.Status_STATE_UNSPECIFIED
	}
	if resp.State != spb.Status_PLATFORM_READY && resp.State != spb.Status_READY {
		resp.StateReason = statusResp.GetErrorReason()
	}
	if resp.State != spb.Status_READY {
		return resp, nil
	}

	if err := validateAllSkillsReady(ctx, s.skillreg, s.iac); err != nil {
		resp.State = spb.Status_DEPLOYING
		resp.StateReason = fmt.Sprintf("assets not ready: %v", err)
	}

	return resp, nil
}

func (s *Server) GetBehaviorTree(ctx context.Context, req *svcpb.GetBehaviorTreeRequest) (*btpb.BehaviorTree, error) {
	res, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	behaviorTree, err := behaviorTreeFromRunnables(res.GetApplication().GetProcess().GetRunnables(), req.GetName())
	if err != nil {
		// The errors from behaviorTreeFromRunnables already contain a status code.
		return nil, err
	}
	if req.GetView() == svcpb.BehaviorTreeView_BEHAVIOR_TREE_VIEW_BASIC {
		applyBasicView(behaviorTree)
	}
	return behaviorTree, nil
}

func (s *Server) BatchGetBehaviorTrees(ctx context.Context, req *svcpb.BatchGetBehaviorTreesRequest) (*svcpb.BatchGetBehaviorTreesResponse, error) {
	if len(req.GetNames()) == 0 {
		return nil, status.Errorf(codes.InvalidArgument, "no behavior tree names specified")
	}
	if len(req.GetNames()) > maxBatchGetBehaviorTrees {
		return nil, status.Errorf(codes.InvalidArgument, "too many behavior trees in request, maximum is %d", maxBatchGetBehaviorTrees)
	}
	res, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	behaviorTrees := make([]*btpb.BehaviorTree, len(req.GetNames()))
	for i, name := range req.GetNames() {
		behaviorTree, err := behaviorTreeFromRunnables(res.GetApplication().GetProcess().GetRunnables(), name)
		if err != nil {
			// The errors from behaviorTreeFromRunnables already contain a status code.
			return nil, err
		}
		// Batch defaults to BASIC view, so UNSPECIFIED is the same as BASIC.
		if req.GetView() == svcpb.BehaviorTreeView_BEHAVIOR_TREE_VIEW_UNSPECIFIED ||
			req.GetView() == svcpb.BehaviorTreeView_BEHAVIOR_TREE_VIEW_BASIC {
			applyBasicView(behaviorTree)
		}
		behaviorTrees[i] = behaviorTree
	}
	return &svcpb.BatchGetBehaviorTreesResponse{
		BehaviorTrees: behaviorTrees,
	}, nil
}

func (s *Server) ListBehaviorTrees(ctx context.Context, req *svcpb.ListBehaviorTreesRequest) (*svcpb.ListBehaviorTreesResponse, error) {
	pageSize, err := behaviorTreesPageSizeValidator.Run(int(req.GetPageSize()))
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid page_size: %v", err)
	}

	var startAfterID string
	if req.GetPageToken() != "" {
		pt, err := pagetoken.Deopacify(req.GetPageToken())
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid page_token: %v", err)
		}
		// The page token request validation is simply that it can recovered to the
		// correct proto type. The parameters in the request from the page token
		// usually need to be validated against the current request parameters.
		// However, there are no relevant parameters that cannot change between
		// pagination calls at this time.
		if err := pagetoken.RecoverRequest(pt, &svcpb.ListBehaviorTreesRequest{}); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid request in page_token: %v", err)
		}
		startAfterID = pt.GetStartAfterId()
	}

	res, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	var bts []*btpb.BehaviorTree
	// We need to iterate all the runnables in the map because map order is
	// randomized in Go and we need to be able to pick up pagination
	// deterministically.
	for runnableName, runnable := range res.GetApplication().GetProcess().GetRunnables() {
		r, ok := runnable.Runnable.(*ppb.Process_Runnable_BehaviorTree)
		if !ok {
			continue
		}
		bts = append(bts, ensureConsistentName(runnableName, r.BehaviorTree))
	}
	// Sort the behavior trees alphabetically by name. This is outlined in the API
	// documentation. Sorting is also necessary to ensure that we can paginate
	// consistently.
	slices.SortFunc(bts, func(a, b *btpb.BehaviorTree) int {
		return cmp.Compare(strings.ToLower(a.GetName()), strings.ToLower(b.GetName()))
	})

	pageStart := 0
	for i, bt := range bts {
		if bt.GetName() == startAfterID {
			pageStart = i + 1
			break
		}
	}

	r := &svcpb.ListBehaviorTreesResponse{}
	if len(bts) < pageStart+pageSize {
		r.BehaviorTrees = bts[pageStart:]
	} else {
		r.BehaviorTrees = bts[pageStart:min(pageStart+pageSize, len(bts))]
		// Unset the page token to prevent russian-dolling. This follows guidance
		// from documentation of the [pagetoken.Opacify] method.
		req.PageToken = ""
		npt, err := pagetoken.Opacify(req, r.BehaviorTrees[len(r.BehaviorTrees)-1].GetName(), nil)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to create page token: %v", err)
		}
		r.NextPageToken = npt
	}

	// The default (UNSPECIFIED) and BASIC views behave the same.
	if req.GetView() != svcpb.BehaviorTreeView_BEHAVIOR_TREE_VIEW_FULL {
		for _, bt := range r.BehaviorTrees {
			applyBasicView(bt)
		}
	}

	return r, nil
}

func (s *Server) CreateBehaviorTree(ctx context.Context, req *svcpb.CreateBehaviorTreeRequest) (*btpb.BehaviorTree, error) {
	if req.GetBehaviorTreeId() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "behavior_tree_id must be specified")
	}
	if req.GetBehaviorTree() == nil {
		return nil, status.Errorf(codes.InvalidArgument, "behavior_tree must be specified")
	}
	getRes, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	app := getRes.GetApplication()
	if _, ok := app.GetProcess().GetRunnables()[req.GetBehaviorTreeId()]; ok {
		return nil, status.Errorf(codes.AlreadyExists, "runnable %q already exists", req.GetBehaviorTreeId())
	}

	bt := req.GetBehaviorTree()
	bt.Name = req.GetBehaviorTreeId()

	if err := s.setBehaviorTreeUnsafe(ctx, bt, getRes); err != nil {
		return nil, status.Error(codes.Internal, err.Error())
	}

	if err := s.saveIfAuthenticated(ctx); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to save solution: %v", err)
	}

	return bt, nil
}

func (s *Server) UpdateBehaviorTree(ctx context.Context, req *svcpb.UpdateBehaviorTreeRequest) (*btpb.BehaviorTree, error) {
	name := req.GetBehaviorTree().GetName()
	if name == "" {
		return nil, status.Errorf(codes.InvalidArgument, "behavior_tree.name must be specified")
	}
	getRes, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	app := getRes.GetApplication()

	if !req.GetAllowMissing() {
		if _, ok := app.GetProcess().GetRunnables()[name]; !ok {
			return nil, status.Errorf(codes.NotFound, "runnable %q does not exist", name)
		}
	}

	if err := s.setBehaviorTreeUnsafe(ctx, req.GetBehaviorTree(), getRes); err != nil {
		return nil, status.Error(codes.Internal, err.Error())
	}

	if err := s.saveIfAuthenticated(ctx); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to save solution: %v", err)
	}

	return req.GetBehaviorTree(), nil
}

func (s *Server) BatchUpdateBehaviorTrees(ctx context.Context, req *svcpb.BatchUpdateBehaviorTreesRequest) (*svcpb.BatchUpdateBehaviorTreesResponse, error) {
	if len(req.GetRequests()) == 0 {
		return nil, status.Errorf(codes.InvalidArgument, "no behavior trees specified")
	}
	if len(req.GetRequests()) > maxBatchUpdateBehaviorTrees {
		return nil, status.Errorf(codes.InvalidArgument, "too many behavior trees in request, maximum is %d", maxBatchUpdateBehaviorTrees)
	}

	for _, updateReq := range req.GetRequests() {
		if updateReq.GetBehaviorTree().GetName() == "" {
			return nil, status.Errorf(codes.InvalidArgument, "behavior_tree.name must be specified for all requests")
		}
	}

	appRes, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	app := appRes.GetApplication()
	ensureRunnablesMap(app)

	updatedBehaviorTrees := make([]*btpb.BehaviorTree, len(req.GetRequests()))
	for i, updateReq := range req.GetRequests() {
		if !updateReq.GetAllowMissing() {
			if _, ok := app.GetProcess().GetRunnables()[updateReq.GetBehaviorTree().GetName()]; !ok {
				return nil, status.Errorf(codes.NotFound, "runnable %q does not exist", updateReq.GetBehaviorTree().GetName())
			}
		}

		app.GetProcess().GetRunnables()[updateReq.GetBehaviorTree().GetName()] = &ppb.Process_Runnable{
			Runnable: &ppb.Process_Runnable_BehaviorTree{
				BehaviorTree: updateReq.GetBehaviorTree(),
			},
		}

		updatedBehaviorTrees[i] = updateReq.GetBehaviorTree()
	}

	if _, err := s.hss.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   app,
		RevisionToken: appRes.GetRevisionToken(),
	}); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to update solution: %v", err)
	}

	if err := s.saveIfAuthenticated(ctx); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to save solution: %v", err)
	}

	return &svcpb.BatchUpdateBehaviorTreesResponse{
		BehaviorTrees: updatedBehaviorTrees,
	}, nil
}

func (s *Server) DeleteBehaviorTree(ctx context.Context, req *svcpb.DeleteBehaviorTreeRequest) (*epb.Empty, error) {
	if req.GetName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "name must be specified")
	}
	getRes, err := s.hss.GetCurrentApplication(ctx, &aspb.GetCurrentApplicationRequest{})
	if err != nil {
		return nil, status.Errorf(codes.FailedPrecondition, "failed to get solution information: %v", err)
	}
	app := getRes.GetApplication()
	if _, ok := app.GetProcess().GetRunnables()[req.GetName()]; !ok {
		return nil, status.Errorf(codes.NotFound, "runnable %q does not exist", req.GetName())
	}

	delete(app.GetProcess().GetRunnables(), req.GetName())

	if _, err := s.hss.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   app,
		RevisionToken: getRes.GetRevisionToken(),
	}); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to update solution: %v", err)
	}

	if err := s.saveIfAuthenticated(ctx); err != nil {
		return nil, status.Errorf(codes.Internal, "failed to save solution: %v", err)
	}

	return &epb.Empty{}, nil
}

func (s *Server) GetSequenceList(ctx context.Context, req *svcpb.GetSequenceListRequest) (*svcpb.GetSequenceListResponse, error) {
	// TODO(b/522759407): Remove conditional once feature option is gone and the
	// logger client is always set.
	if s.logger == nil {
		return nil, status.Errorf(codes.Unimplemented, "GetSequenceList is not available")
	}
	if req.GetExecutiveOperationName() == "" {
		return nil, status.Errorf(codes.InvalidArgument, "executive_operation_name must be specified")
	}
	pageSize, err := sequenceListPageSizeValidator.Run(int(req.GetPageSize()))
	if err != nil {
		return nil, status.Errorf(codes.InvalidArgument, "invalid page size: %v", err)
	}

	loggerReq := &loggerpb.GetLogItemsRequest{}
	if req.GetPageToken() != "" {
		pt, err := pagetoken.Deopacify(req.GetPageToken())
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid page_token: %v", err)
		}
		// Clear the page token after reading to ensure we don't encode it into a
		// new page token if there is one.
		req.PageToken = ""
		ptReq := &svcpb.GetSequenceListRequest{}
		if err := pagetoken.RecoverRequest(pt, ptReq); err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid request in page_token: %v", err)
		}
		if ptReq.GetExecutiveOperationName() != req.GetExecutiveOperationName() ||
			!proto.Equal(ptReq.GetExecutiveOperationStartTime(), req.GetExecutiveOperationStartTime()) {
			return nil, status.Error(codes.InvalidArgument, "page_token is invalid for the current request")
		}
		if pt.GetStartAfterId() == "" {
			return nil, status.Error(codes.InvalidArgument, "missing cursor in page_token")
		}
		cursor, err := base64.StdEncoding.DecodeString(pt.GetStartAfterId())
		if err != nil {
			return nil, status.Errorf(codes.InvalidArgument, "invalid cursor in page_token: %v", err)
		}
		loggerReq.Query = &loggerpb.GetLogItemsRequest_Cursor{
			Cursor: cursor,
		}
	} else {
		loggerReq.Query = executiveOperationLogQuery(req.GetExecutiveOperationName(), req.GetExecutiveOperationStartTime())
	}

	// Read log items in small batches until the page could overflow with the
	// next batch, the time budget is used up, or there are no more log items.
	// Each log item belongs to at most one action, so a batch adds at most as
	// many items as it has log items. Batches keep the same size so that the
	// logger is not queried for single log items near the end of a page. Actions
	// that continue after this page show up again on a later page with their
	// updated state.
	batchSize := min(sequenceListLogItemBatchSize, pageSize)
	loggerReq.MaxNumItems = proto.Int32(int32(batchSize))
	builder := sequencelist.NewBuilder()
	deadline := s.now().Add(sequenceListTimeBudget)
	var nextCursor []byte
	for {
		loggerResp, err := s.logger.GetLogItems(ctx, loggerReq)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to get log items: %v", err)
		}
		builder.Add(ctx, loggerResp.GetLogItems())
		nextCursor = loggerResp.GetNextPageCursor()
		if nextCursor == nil || !s.now().Before(deadline) || pageSize-builder.Len() < batchSize {
			break
		}
		loggerReq.Query = &loggerpb.GetLogItemsRequest_Cursor{
			Cursor: nextCursor,
		}
	}

	resp := &svcpb.GetSequenceListResponse{Items: builder.Items()}
	if nextCursor != nil {
		npt, err := pagetoken.Opacify(req, base64.StdEncoding.EncodeToString(nextCursor), nil)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "failed to create next_page_token: %v", err)
		}
		resp.NextPageToken = npt
	}
	return resp, nil
}

func (s *Server) setBehaviorTreeUnsafe(ctx context.Context, bt *btpb.BehaviorTree, appRes *aspb.GetCurrentApplicationResponse) error {
	app := appRes.GetApplication()
	ensureRunnablesMap(app)
	app.GetProcess().GetRunnables()[bt.GetName()] = &ppb.Process_Runnable{
		Runnable: &ppb.Process_Runnable_BehaviorTree{
			BehaviorTree: bt,
		},
	}

	if _, err := s.hss.SetCurrentApplication(ctx, &aspb.SetCurrentApplicationRequest{
		Application:   app,
		RevisionToken: appRes.GetRevisionToken(),
	}); err != nil {
		return errors.Wrap(err, "failed to update solution")
	}

	return nil
}

// saveIfAuthenticated attempts to save the current application. If the context
// does not have authentication information, this function logs a warning and
// returns without error.
func (s *Server) saveIfAuthenticated(ctx context.Context) error {
	authCtx, ok, err := clientcontext.ToContextFromIncomingChecked(ctx)
	if err != nil {
		log.WarningContextf(ctx, "Failed to transform context identity to outgoing context: %v", err)
		return clientcontext.ErrGRPC(err)
	}
	if !ok {
		log.WarningContextf(ctx, "Not saving solution because context has no authentication information")
		return nil
	}
	if err := s.saver.Save(authCtx); err != nil {
		return err
	}
	return nil
}

// ensureConsistentName ensures consistency between the runnable name and the
// behavior tree name. There is no technical guarantee that these are the same,
// but the frontend expects the queried name to match the name stored on the
// resource. The goal will be to always have these match.
func ensureConsistentName(runnableName string, bt *btpb.BehaviorTree) *btpb.BehaviorTree {
	if bt.GetName() != runnableName {
		bt.Name = runnableName
	}
	return bt
}

func ensureRunnablesMap(app *apb.Application) {
	if app.GetProcess() == nil {
		app.Process = &ppb.Process{
			Runnables: make(map[string]*ppb.Process_Runnable),
		}
	} else if app.GetProcess().GetRunnables() == nil {
		app.GetProcess().Runnables = make(map[string]*ppb.Process_Runnable)
	}
}

func applyBasicView(bt *btpb.BehaviorTree) {
	name := bt.Name
	treeID := bt.TreeId
	bt.Reset()
	bt.Name = name
	bt.TreeId = treeID
}

func executiveOperationLogQuery(operationName string, startTime *timestamppb.Timestamp) *loggerpb.GetLogItemsRequest_GetQuery {
	if startTime == nil {
		// Default to the disk retention time of executive log items. This is also
		// how the default is documented on the RPC.
		startTime = timestamppb.New(time.Now().Add(time.Hour * -24))
	}
	return &loggerpb.GetLogItemsRequest_GetQuery{
		GetQuery: &loggerpb.GetLogItemsRequest_Query{
			StartTime:   startTime,
			EventSource: executiveOperationEventSource,
			FilterLabels: map[string]string{
				executiveOperationFilterLabel: operationName,
			},
		},
	}
}
