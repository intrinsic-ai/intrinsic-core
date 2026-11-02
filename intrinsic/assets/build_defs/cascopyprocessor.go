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

// Package cascopyprocessor provides a referenceddata.Processor that copies CAS
// objects across GCP projects if missing from the destination project.
package cascopyprocessor

import (
	"context"
	"fmt"
	"io"
	"sync"

	"intrinsic/assets/referenceddata"
	"intrinsic/storage/content_addressable_storage/pkg/crossproject"
	"intrinsic/storage/content_addressable_storage/pkg/primordial"
	"intrinsic/tools/inctl/auth/auth"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	casgrpcpb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

// CASClientFactory creates a CAS client and optional closer for a given GCP project.
type CASClientFactory func(ctx context.Context, project string) (casgrpcpb.ContentAddressableStorageServiceClient, io.Closer, error)

func defaultCASClientFactory(ctx context.Context, project string) (casgrpcpb.ContentAddressableStorageServiceClient, io.Closer, error) {
	conn, err := auth.NewCloudConnection(ctx, auth.WithProject(project))
	if err != nil {
		return nil, nil, err
	}
	return casgrpcpb.NewContentAddressableStorageServiceClient(conn), conn, nil
}

// Option configures a Processor.
type Option func(*Processor)

// WithCASClientFactory overrides the factory used to create CAS clients for source projects.
func WithCASClientFactory(factory CASClientFactory) Option {
	return func(p *Processor) {
		p.clientFactory = factory
	}
}

// Processor is a referenceddata.Processor that ensures CAS references exist in the target project
// by copying them from their source project (or primordial.Project) if missing.
type Processor struct {
	inner           referenceddata.Processor
	targetProject   string
	targetCASClient casgrpcpb.ContentAddressableStorageServiceClient
	clientFactory   CASClientFactory

	mu      sync.Mutex
	clients map[string]casgrpcpb.ContentAddressableStorageServiceClient
	closers []io.Closer
}

// New creates a new CAS copying processor wrapping an inner processor.
func New(inner referenceddata.Processor, targetProject string, targetCASClient casgrpcpb.ContentAddressableStorageServiceClient, opts ...Option) *Processor {
	p := &Processor{
		inner:           inner,
		targetProject:   targetProject,
		targetCASClient: targetCASClient,
		clientFactory:   defaultCASClientFactory,
		clients:         make(map[string]casgrpcpb.ContentAddressableStorageServiceClient),
	}
	for _, opt := range opts {
		opt(p)
	}
	return p
}

// NeedsReaderFor delegates to the inner processor.
func (p *Processor) NeedsReaderFor(rt referenceddata.ReferenceType) bool {
	return p.inner.NeedsReaderFor(rt)
}

func (p *Processor) getSourceClient(ctx context.Context, project string) (casgrpcpb.ContentAddressableStorageServiceClient, error) {
	p.mu.Lock()
	defer p.mu.Unlock()

	if client, ok := p.clients[project]; ok {
		return client, nil
	}
	client, closer, err := p.clientFactory(ctx, project)
	if err != nil {
		return nil, fmt.Errorf("failed to establish connection to source project %q: %w", project, err)
	}
	p.clients[project] = client
	if closer != nil {
		p.closers = append(p.closers, closer)
	}
	return client, nil
}

// Close closes any source CAS client connections opened during processing.
func (p *Processor) Close() {
	p.mu.Lock()
	defer p.mu.Unlock()
	for _, closer := range p.closers {
		closer.Close()
	}
	p.closers = nil
}

// Process checks if a CAS reference exists in the destination project and copies it from
// the source project (defaulting to primordial.Project) if missing.
func (p *Processor) Process(ctx context.Context, rdr *referenceddata.Reader, opts *referenceddata.ProcessOptions) error {
	if rdr.Ref.Type() == referenceddata.CASReferenceType {
		casURI := rdr.Ref.Reference()

		_, err := p.targetCASClient.Stat(ctx, &casgrpcpb.StatRequest{ObjectId: casURI})
		switch code := status.Code(err); code {
		case codes.OK:
			// Already exists in target project CAS.
		case codes.NotFound:
			sourceProject := rdr.Ref.SourceProject()
			if sourceProject == "" {
				sourceProject = primordial.Project
			}
			if sourceProject == p.targetProject {
				return fmt.Errorf("CAS object %q not found in project %q", casURI, p.targetProject)
			}
			srcClient, err := p.getSourceClient(ctx, sourceProject)
			if err != nil {
				return err
			}
			if err := crossproject.Copy(ctx, srcClient, p.targetCASClient, casURI); err != nil {
				return fmt.Errorf("failed to copy CAS object %q from project %q to %q: %w", casURI, sourceProject, p.targetProject, err)
			}
		default:
			return fmt.Errorf("failed to stat CAS object %q in project %q: %w", casURI, p.targetProject, err)
		}

		if rdr.Ref.SourceProject() != p.targetProject {
			rdr.Ref.SetSourceProject(p.targetProject)
		}
	}

	return p.inner.Process(ctx, rdr, opts)
}
