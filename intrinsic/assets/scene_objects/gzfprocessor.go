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

// Package gzfprocessor provides utilities to process SceneObject GZF files and upload geometry.
package gzfprocessor

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"

	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzffile"
	"intrinsic/assets/throttle"
	"intrinsic/tools/inctl/util/casgeometryuploader" 

	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"    
	"google.golang.org/grpc/metadata" 
	"google.golang.org/grpc/status"   
	"google.golang.org/protobuf/proto"

	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	gsrpb "intrinsic/geometry/proto/geometry_storage_refs_go_proto"
	gpb "intrinsic/geometry/proto/v1/geometry_go_proto"
	gsrv1pb "intrinsic/geometry/proto/v1/geometry_storage_refs_go_proto"
	tgpb "intrinsic/geometry/proto/v1/transformed_geometry_go_proto"
	sopb "intrinsic/scene/proto/v1/scene_object_go_proto"
	gcpb "intrinsic/world/public/proto/geometry_component_go_proto"
)

// GZFProcessor is an interface for a function that processes a GZF file into a SceneObject.
//
// The provided reader is for a GZF file.
type GZFProcessor func(ctx context.Context, r io.Reader) (*sopb.SceneObject, error)

type processOptions struct {
	concurrencyLimiter *throttle.ConcurrencyLimiter
	rewrite            bool
	uploader           *casgeometryuploader.CASGeometryUploader 
}

// Option is a functional option for [New].
type Option func(*processOptions)

// WithRewrite configures whether the processor should rewrite geometry references in the
// SceneObject.
func WithRewrite(rewrite bool) Option {
	return func(o *processOptions) {
		o.rewrite = rewrite
	}
}



// WithLegacyUploader configures the legacy CAS uploader to use as a fallback.
func WithLegacyUploader(uploader *casgeometryuploader.CASGeometryUploader) Option {
	return func(o *processOptions) {
		o.uploader = uploader
	}
}



// WithConcurrencyLimiter configures a shared ConcurrencyLimiter for geometry processing.
func WithConcurrencyLimiter(limiter *throttle.ConcurrencyLimiter) Option {
	return func(o *processOptions) {
		o.concurrencyLimiter = limiter
	}
}

// New returns a new GZFProcessor.
func New(rdProcessor referenceddata.Processor, options ...Option) GZFProcessor {
	opts := &processOptions{
		concurrencyLimiter: throttle.NewConcurrencyLimiter(1),
		rewrite:            true,
	}
	for _, opt := range options {
		opt(opts)
	}

	return func(ctx context.Context, r io.Reader) (*sopb.SceneObject, error) {
		if rdProcessor == nil {
			return nil, fmt.Errorf("rdProcessor is required and cannot be nil")
		}

		return processGZF(ctx, r, rdProcessor, opts)
	}
}

func processGZF(ctx context.Context, r io.Reader, rdProcessor referenceddata.Processor, opts *processOptions) (*sopb.SceneObject, error) {
	tmpDir := os.TempDir()

	localExportDir, err := os.MkdirTemp(tmpDir, "unpacked_scene_object_geometries_")
	if err != nil {
		return nil, fmt.Errorf("could not create directory %q: %w", localExportDir, err)
	}
	defer func() {
		if err := os.RemoveAll(localExportDir); err != nil {
			log.ErrorContextf(ctx, "Could not remove %q: %v", localExportDir, err)
		}
	}()

	sceneObject, err := unpackGZF(r, tmpDir, localExportDir)
	if err != nil {
		return nil, err
	}
	if sceneObject == nil {
		return nil, nil
	}

	tt, err := processGeometries(ctx, localExportDir, rdProcessor, opts)
	if err != nil {
		return nil, err
	}

	// Rewrite the references in the SceneObject.
	sceneObjectToRewrite := sceneObject
	if !opts.rewrite {
		sceneObjectToRewrite = proto.Clone(sceneObject).(*sopb.SceneObject)
	}
	unused, err := newRewriter(tt).rewriteSceneObject(sceneObjectToRewrite)
	if err != nil {
		return nil, fmt.Errorf("could not rewrite scene object: %w", err)
	}
	if len(unused) > 0 {
		log.WarningContextf(ctx, "Unused geometry fingerprints: %v", unused)
	}

	return sceneObject, nil
}

func processGeometries(ctx context.Context, localExportDir string, rdProcessor referenceddata.Processor, opts *processOptions) (map[string]string, error) {
	entries, err := os.ReadDir(localExportDir)
	if err != nil {
		return nil, fmt.Errorf("could not read directory %q: %w", localExportDir, err)
	}

	var mu sync.Mutex
	tt := make(map[string]string)

	var fns []func(context.Context) error
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		filePath := filepath.Join(localExportDir, name)

		fns = append(fns, func(ctx context.Context) error {
			ref := referenceddata.FromProto(&rdpb.ReferencedData{
				Data: &rdpb.ReferencedData_Reference{
					Reference: "file://" + filePath,
				},
			})
			// Force upload by setting threshold to -1
			if err := referenceddata.Process(ctx, ref, rdProcessor,
				referenceddata.WithInlineThresholdOverride(-1),
			); err != nil {
				return err
			}

			mu.Lock()
			defer mu.Unlock()
			tt[name] = ref.Reference()

			return nil
		})
	}

	if err := opts.concurrencyLimiter.Do(ctx, fns...); err != nil {

		// Fallback to the legacy uploader if the Processor is unavailable.
		code := status.Code(err)
		if opts.uploader != nil && (code == codes.Unimplemented || code == codes.Unavailable) {
			log.WarningContextf(ctx, "AssetArtifacts service unavailable; falling back to legacy CAS uploader: %v", err)
			// Strip auth header ONLY for the legacy CAS uploader call.
			return opts.uploader.ScanAndUpload(stripAuthHeader(ctx), localExportDir)
		}

		return nil, err
	}

	return tt, nil
}

func unpackGZF(r io.Reader, tmpDir string, localExportDir string) (*sopb.SceneObject, error) {
	// We write out the SceneObject file to disk, since the cgo-wrapped library expects the GZF file
	// to be on disk. os.CreateTemp adds a random suffix to the given filename.
	f, err := os.CreateTemp(tmpDir, "scene_object.gzf")
	if err != nil {
		return nil, fmt.Errorf("could not create temporary file %q: %w", f.Name(), err)
	}
	defer os.Remove(f.Name())
	defer f.Close()

	numBytes, err := io.Copy(f, r)
	if err != nil {
		return nil, fmt.Errorf("could not write temporary scene object file %q: %w", f.Name(), err)
	}
	if err := f.Sync(); err != nil {
		return nil, fmt.Errorf("could not sync temporary scene object file %q: %w", f.Name(), err)
	}
	if numBytes == 0 {
		return nil, nil
	}

	return gzffile.ExtractSceneObject(f.Name(), localExportDir)
}



// stripAuthHeader removes the "authorization" header from the context's metadata.
//
// TODO(b/441763619): This helper is temporary. It is used to prevent the cluster's authentication
// token (injected into the context by clientutils.AuthInsecureConn) from being propagated to cloud
// services (like Cloud CAS), which would cause authentication conflicts with the cloud connection's
// own GCP credentials. This can be removed once SceneObject geometry upload is refactored to use
// referenceddata.Processor.
func stripAuthHeader(ctx context.Context) context.Context {
	md, ok := metadata.FromOutgoingContext(ctx)
	if !ok {
		return ctx
	}
	newMd := md.Copy()
	delete(newMd, "authorization")
	return metadata.NewOutgoingContext(ctx, newMd)
}



// rewriter contains a translation table and tracks which IDs have been rewritten.
type rewriter struct {
	tt   map[string]string
	used map[string]bool
}

func newRewriter(tt map[string]string) *rewriter {
	if tt == nil {
		tt = make(map[string]string)
	}
	used := make(map[string]bool, len(tt))
	for k := range tt {
		used[k] = false
	}
	return &rewriter{tt, used}
}

// rewriteSceneObject replaces geometry fingerprints in a SceneObject with the references according
// to the translation table.
//
// Returns error if an ID is in the SceneObject but not in the translation table.
// Also returns a set of the unused entries in the translation table. This is useful for debugging.
// However, notice that the state of the translation table is carried over between the invocations
// of this and other functions. Please construct a new [rewriter] for each document you
// want to rewrite.
func (r *rewriter) rewriteSceneObject(wf *sopb.SceneObject) (map[string]struct{}, error) {
	for _, e := range wf.GetEntities() {
		if e.GetLink() != nil {
			gc := e.GetLink().GetGeometryComponent()
			if err := r.rewriteGeometryComponent(gc); err != nil {
				return nil, fmt.Errorf("rewriting entity %q: %w", e.GetName(), err)
			}
		}
	}
	return r.unusedLeft(), nil
}

func (r *rewriter) unusedLeft() map[string]struct{} {
	unused := make(map[string]struct{})
	for k, v := range r.used {
		if !v {
			unused[k] = struct{}{}
		}
	}
	return unused
}

func (r *rewriter) rewriteGeometryComponent(gc *gcpb.GeometryComponent) error {
	for n, ng := range gc.GetNamedGeometries() {
		// TODO(b/447006911): Remove v0 geometries rewrite once assets with only v0 geometries are not supported.
		for i, g := range ng.GetGeometries() {
			if err := r.rewriteGeometryModel(g); err != nil {
				return fmt.Errorf("rewriting named geometry %q, geometry idx=%d: %w", n, i, err)
			}
		}
		for nn, tg := range ng.GetNamedGeometries() {
			if err := r.rewriteTransformedGeometry(tg); err != nil {
				return fmt.Errorf("rewriting named geometry %q, geometry name=%q: %w", n, nn, err)
			}
		}
	}
	return nil
}

func (r *rewriter) rewriteTransformedGeometry(tg *tgpb.TransformedGeometry) error {
	if tg.Geometry.GetGeoRef() == nil {
		return nil
	}
	oldExactGeometryRef := tg.Geometry.GetGeoRef().GetExactGeometryRef()

	if _, ok := r.tt[oldExactGeometryRef]; !ok {
		return fmt.Errorf("geometry's exact geometry ref %q is not in the translation table", oldExactGeometryRef)
	}

	r.used[oldExactGeometryRef] = true

	newRenderableRef := ""
	if tg.Geometry.GetGeoRef().GetRenderableRef() != "" {
		oldRenderableRef := tg.Geometry.GetGeoRef().GetRenderableRef()
		if _, ok := r.tt[oldRenderableRef]; !ok {
			return fmt.Errorf("geometry's renderable ref %q is not in the translation table", oldRenderableRef)
		}
		newRenderableRef = r.tt[oldRenderableRef]
		r.used[oldRenderableRef] = true
	}
	tg.Geometry.Data = &gpb.Geometry_GeoRef{
		GeoRef: &gsrv1pb.GeometryStorageRefs{
			ExactGeometryRef: r.tt[oldExactGeometryRef],
			RenderableRef:    newRenderableRef,
			KeepRenderable:   tg.Geometry.GetGeoRef().GetKeepRenderable(),
		},
	}
	return nil
}

func (r *rewriter) rewriteGeometryModel(m *gcpb.GeometryComponent_Geometry) error {
	oldGeometryRef := m.GetGeometryStorageRefs().GetGeometryRef()
	oldRenderableRef := m.GetGeometryStorageRefs().GetRenderableRef()

	if _, ok := r.tt[oldGeometryRef]; !ok {
		return fmt.Errorf("geometry's geometry ref %q is not in the translation table", oldGeometryRef)
	}
	if _, ok := r.tt[oldRenderableRef]; !ok {
		return fmt.Errorf("geometry's renderable ref %q is not in the translation table", oldRenderableRef)
	}

	// Don't assign pointers to protos, copy values!
	m.GeometryStorageRefs = &gsrpb.GeometryStorageRefs{
		Fingerprint:   m.GetGeometryStorageRefs().GetFingerprint(),
		GeometryRef:   r.tt[oldGeometryRef],
		RenderableRef: r.tt[oldRenderableRef],
	}

	r.used[oldGeometryRef] = true
	r.used[oldRenderableRef] = true
	return nil
}
