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

package writer

import (
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/containerd/containerd/content"
	log "github.com/golang/glog"
	"github.com/opencontainers/go-digest"
	"go.opencensus.io/trace"
	"go.uber.org/atomic"

	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

const (
	maxEphemeralFileSize int64 = 2 * 1024 * 1024 * 1024 // 2GB
	temporaryDestination       = ".ephemeral"
)

func newFileWriter(ctx context.Context, store ArtifactStore, req *artifactpb.UpdateRequest) (UpdateWriter, error) {
	if req.Ref == "" {
		return nil, fmt.Errorf("%w: reference cannot be empty", ErrInvalidReference)
	}

	cleanRef := filepath.Clean(req.Ref)
	if cleanRef == "." || cleanRef == ".." || filepath.IsAbs(cleanRef) || filepath.Base(cleanRef) != cleanRef || strings.ContainsAny(cleanRef, `/\`) {
		return nil, fmt.Errorf("%w: %q", ErrInvalidReference, req.Ref)
	}

	tempDir := filepath.Join(store.EphemeralFileStore(), temporaryDestination)
	if err := os.MkdirAll(tempDir, 0o700); err != nil {
		return nil, fmt.Errorf("cannot create write destination: %w", err)
	}

	filename := filepath.Join(tempDir, cleanRef)
	relTemp, err := filepath.Rel(tempDir, filename)
	if err != nil || strings.HasPrefix(relTemp, "..") || relTemp == "." {
		return nil, fmt.Errorf("%w: %q", ErrInvalidReference, req.Ref)
	}

	targetName := filepath.Join(store.EphemeralFileStore(), cleanRef)
	relTarget, err := filepath.Rel(store.EphemeralFileStore(), targetName)
	if err != nil || strings.HasPrefix(relTarget, "..") || relTarget == "." {
		return nil, fmt.Errorf("%w: %q", ErrInvalidReference, req.Ref)
	}

	file, err := os.Create(filename)
	if err != nil {
		return nil, fmt.Errorf("cannot allocate target: %w", err)
	}

	writer := &fileWriter{
		store:      store,
		reference:  cleanRef,
		filename:   filename,
		writer:     file,
		finalized:  atomic.NewBool(false),
		startedAt:  time.Now(),
		lastUpdate: time.Time{}, // never updated so far
		ctx:        ctx,
		// we are going to use Canonical digester by default. If caller does not
		// provide req.ExpectedDigest, this may be thrown away, but it is better
		// than re-reading whole file every time.
		digester: digest.Canonical.Digester(),
	}

	if req.ExpectedDigest != nil {
		writer.expectedDigest = digest.Digest(*req.ExpectedDigest)
		alg := writer.expectedDigest.Algorithm()
		if alg.Available() {
			writer.digester = alg.Digester()
		}
	}

	return writer, nil
}

type fileWriter struct {
	// artifact store implementation
	store ArtifactStore
	// object target filename
	reference string
	// ephemeral destination of writer, to ensure atomic move on filesystem
	filename string
	// file writer to ephemeral destination
	writer *os.File
	// written so far
	total int64
	// was content finalized yet?
	finalized *atomic.Bool
	// expected digest if known, could be empty
	expectedDigest digest.Digest
	// when write started
	startedAt time.Time
	// when was last update made to the writer
	lastUpdate time.Time
	ctx        context.Context
	digester   digest.Digester
}

func (f *fileWriter) Write(p []byte) (n int, err error) {
	if (f.total + int64(len(p))) > maxEphemeralFileSize {
		// we are going to write more than max file size
		return 0, ErrMaxContentSizeExceeded
	}
	n, err = f.writer.Write(p)
	if err != nil {
		return
	}

	if _, err = f.digester.Hash().Write(p); err != nil {
		// this should never really happen
		return
	}
	f.lastUpdate = time.Now()
	f.total += int64(n)
	return
}

func (f *fileWriter) Close() error {
	err := f.writer.Close()
	if errors.Is(err, os.ErrClosed) {
		return nil
	}
	return err
}

func (f *fileWriter) Status() (content.Status, error) {
	result := content.Status{
		Ref:       f.reference,
		Offset:    f.total,
		Total:     f.total,
		Expected:  f.expectedDigest,
		StartedAt: f.startedAt,
		UpdatedAt: f.lastUpdate,
	}

	return result, nil
}

func (f *fileWriter) Commit(size int64, expected digest.Digest) error {
	_, span := trace.StartSpan(f.ctx, "fileWriter.Commit")
	defer span.End()

	if !f.finalized.CompareAndSwap(false, true) {
		return fmt.Errorf("content already finalized")
	}
	if size > 0 && size != f.total {
		return fmt.Errorf("unexpected size, got %d, should be %d", f.total, size)
	}
	// force buffer flush so we have whole content
	if err := f.writer.Sync(); err != nil {
		return fmt.Errorf("cannot finalize write: %w", err)
	}
	if err := f.writer.Close(); err != nil {
		return fmt.Errorf("closing writer failed: %w", err)
	}
	if expected != "" {
		if expected.Algorithm() != f.digester.Digest().Algorithm() {
			// digester algorithms does not match, need to recalculate
			log.Warningf("Digest algorithm mismatch, recalculating...")
			open, err := os.Open(f.filename)
			if err != nil {
				return fmt.Errorf("cannot validate expected hash")
			}
			f.digester = expected.Algorithm().Digester()
			if _, err = io.Copy(f.digester.Hash(), open); err != nil {
				return fmt.Errorf("cannot compute content digest: %w", err)
			}
			defer open.Close()
		}
		if expected != f.digester.Digest() {
			return fmt.Errorf("digests does not match; wants %s, but got %s", expected, f.digester.Digest())
		}
	}

	targetName, err := f.getTargetName()
	if err != nil {
		return fmt.Errorf("error determining target name: %w", err)
	}

	if err := os.Rename(f.filename, targetName); err != nil {
		return fmt.Errorf("error materializing file: %w", err)
	}

	return nil
}

func (f *fileWriter) Abort() error {
	if !f.finalized.CompareAndSwap(false, true) {
		return ErrAlreadyFinalized
	}
	f.writer.Sync()  // best effort
	f.writer.Close() // best effort
	return os.Remove(f.filename)
}

func (f *fileWriter) Digest() digest.Digest {
	if f.finalized.Load() {
		return f.digester.Digest()
	}
	return f.expectedDigest
}

func (f *fileWriter) getTargetName() (string, error) {
	cleanRef := filepath.Clean(f.reference)
	if cleanRef == "." || cleanRef == ".." || filepath.IsAbs(cleanRef) || filepath.Base(cleanRef) != cleanRef || strings.ContainsAny(cleanRef, `/\`) {
		return "", fmt.Errorf("%w: %q", ErrInvalidReference, f.reference)
	}
	target := filepath.Join(f.store.EphemeralFileStore(), cleanRef)
	rel, err := filepath.Rel(f.store.EphemeralFileStore(), target)
	if err != nil || strings.HasPrefix(rel, "..") || rel == "." {
		return "", fmt.Errorf("%w: %q", ErrInvalidReference, f.reference)
	}
	return target, nil
}
