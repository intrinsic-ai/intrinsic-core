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

// Package referenceddata provides utils for working with ReferencedData.
package referenceddata

import (
	"bytes"
	"context"
	"crypto/sha512"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"intrinsic/util/go/seekable"
	"intrinsic/util/proto/walkmessages"

	log "github.com/golang/glog"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/anypb"

	rdpb "intrinsic/assets/data/proto/v1/referenced_data_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// ReferenceType is an enum for the type of reference in a ReferencedData.
type ReferenceType int

const (
	// FileReferenceType is a file reference.
	FileReferenceType ReferenceType = iota
	// CASReferenceType is a CAS reference.
	CASReferenceType
	// InlinedReferenceType is an inlined reference.
	InlinedReferenceType
)

// Stage specifies the current stage of processing a ReferencedData.
type Stage string

const (
	// StageUploadStart indicates that the upload process has started.
	StageUploadStart Stage = "UploadStart"
	// StageUploadProgress indicates that an upload chunk has been sent successfully.
	StageUploadProgress Stage = "UploadProgress"
	// StageUploadFinalize indicates that the upload has finished and is being finalized.
	StageUploadFinalize Stage = "UploadFinalize"
	// StageProcessStart indicates that processing of the reference has started.
	StageProcessStart Stage = "ProcessStart"
	// StageProcessDone indicates that the processing the reference has completed.
	StageProcessDone Stage = "ProcessDone"
)

const (
	// defaultChunkSize is 3 MiB to maximize throughput under request rate limits while staying below
	// the 4 MiB CAS gRPC max receive message size on older servers that do not sub-chunk before
	// forwarding to CAS.
	defaultChunkSize                      = 3 * 1024 * 1024
	InlineReferenceFileSizeThresholdBytes = 1024 * 1024
	maxUploadAttempts                     = 3
)

// ReferencedData represents a reference to data (e.g., in a file or the cloud).
type ReferencedData struct {
	modified bool
	rd       *rdpb.ReferencedData
	refType  ReferenceType
}

// FromProto creates a new ReferencedData from a proto.
func FromProto(rd *rdpb.ReferencedData) *ReferencedData {
	ref := &ReferencedData{
		rd: &rdpb.ReferencedData{
			Digest:        rd.GetDigest(),
			SourceProject: rd.SourceProject,
		},
	}

	if rd.GetReference() != "" {
		var reference string
		ref.refType, reference, ref.modified = parseReference(rd.GetReference())
		ref.rd.Data = &rdpb.ReferencedData_Reference{
			Reference: reference,
		}
	} else {
		ref.refType = InlinedReferenceType
		if rd.GetInlined() != nil {
			ref.rd.Data = &rdpb.ReferencedData_Inlined{
				Inlined: rd.GetInlined(),
			}
		}
	}

	return ref
}

// Digest returns the digest in the ReferencedData.
func (ref *ReferencedData) Digest() string {
	return ref.rd.GetDigest()
}

// Inlined returns the inlined data in the ReferencedData.
func (ref *ReferencedData) Inlined() []byte {
	return ref.rd.GetInlined()
}

// Modified returns whether the ReferencedData has been modified.
func (ref *ReferencedData) Modified() bool {
	return ref.modified
}

// Reference returns the reference in the ReferencedData.
func (ref *ReferencedData) Reference() string {
	return ref.rd.GetReference()
}

// Name returns a name that can be used to refer to the reference (e.g., in logs).
func (ref *ReferencedData) Name() string {
	name := ref.Reference()
	if name == "" {
		name = ref.Digest()
	}
	if name == "" {
		name = "<unknown>"
	}
	return name
}

// SourceProject returns the source project in the ReferencedData.
func (ref *ReferencedData) SourceProject() string {
	return ref.rd.GetSourceProject()
}

// Type returns the type of reference in the ReferencedData.
func (ref *ReferencedData) Type() ReferenceType {
	return ref.refType
}

// Equal returns whether the two ReferencedData are equal.
func (ref *ReferencedData) Equal(other *ReferencedData) bool {
	return proto.Equal(ref.rd, other.rd)
}

// SetBaseDir sets the base directory for relative file references.
func (ref *ReferencedData) SetBaseDir(baseDir string) *ReferencedData {
	if ref.Type() == FileReferenceType && !filepath.IsAbs(ref.rd.GetReference()) {
		ref.rd.Data = &rdpb.ReferencedData_Reference{
			Reference: filepath.Join(baseDir, ref.rd.GetReference()),
		}
		ref.modified = true
	}
	return ref
}

// SetDigest sets the digest in the ReferencedData.
func (ref *ReferencedData) SetDigest(digest string) *ReferencedData {
	ref.rd.Digest = digest
	ref.modified = true
	return ref
}

// SetSourceProject sets the source project in the ReferencedData.
func (ref *ReferencedData) SetSourceProject(sourceProject string) *ReferencedData {
	ref.rd.SourceProject = proto.String(sourceProject)
	ref.modified = true
	return ref
}

// ClearSourceProject clears the source project in the ReferencedData.
func (ref *ReferencedData) ClearSourceProject() *ReferencedData {
	ref.rd.SourceProject = nil
	ref.modified = true
	return ref
}

// SetInlined sets the inlined data in the ReferencedData.
func (ref *ReferencedData) SetInlined(inlined []byte) *ReferencedData {
	ref.rd.Data = &rdpb.ReferencedData_Inlined{
		Inlined: inlined,
	}
	ref.refType = InlinedReferenceType
	ref.modified = true
	return ref
}

// SetReference sets the reference in the ReferencedData.
func (ref *ReferencedData) SetReference(reference string) *ReferencedData {
	ref.refType, reference, _ = parseReference(reference)
	ref.rd.Data = &rdpb.ReferencedData_Reference{
		Reference: reference,
	}
	ref.modified = true
	return ref
}

// Copy returns a shallow copy of the ReferencedData.
//
// The modified flag is reset.
func (ref *ReferencedData) Copy() *ReferencedData {
	return FromProto(ref.rd)
}

// Merge merges the data from another ReferencedData into this one.
func (ref *ReferencedData) Merge(other *ReferencedData) *ReferencedData {
	if other.Digest() != "" && ref.Digest() != other.Digest() {
		ref.SetDigest(other.Digest())
	}
	if other.SourceProject() != "" && ref.SourceProject() != other.SourceProject() {
		ref.SetSourceProject(other.SourceProject())
	}
	switch other.Type() {
	case InlinedReferenceType:
		if ref.Type() != InlinedReferenceType || !bytes.Equal(ref.Inlined(), other.Inlined()) {
			ref.SetInlined(other.Inlined())
		}
	default:
		if ref.Type() != other.Type() || ref.Reference() != other.Reference() {
			ref.SetReference(other.Reference())
		}
	}

	return ref
}

// Replace replaces the data in the ReferencedData with the data from another ReferencedData.
//
// The underlying proto is not cloned, so modifying it will modify both this and the other ref.
func (ref *ReferencedData) Replace(other *ReferencedData) *ReferencedData {
	if !ref.Equal(other) {
		ref.rd = other.rd
		ref.refType = other.refType
		ref.modified = true
	}
	return ref
}

// ToProto converts the ReferencedData to a proto.
func (ref *ReferencedData) ToProto() *rdpb.ReferencedData {
	return proto.Clone(ref.rd).(*rdpb.ReferencedData)
}

// WalkUnique walks through a proto message, calling the specified function on any unique
// ReferencedData it finds.
//
// Note that all inlined ReferencedData are considered unique.
//
// This function does not walk into Any protos.
//
// If the specified function modifies a ReferencedData, we replace all instances of the original
// value in the message that is being walked.
//
// The input message may be mutated, and the processed message is returned.
func WalkUnique(msg proto.Message, f func(*ReferencedData) error) (proto.Message, error) {
	// Records references that were visited.
	type visited struct {
		ref      *ReferencedData
		returned proto.Message
	}
	visitedReferencedData := map[string]visited{}

	return walkmessages.Recursively(msg, func(msg proto.Message) (proto.Message, bool, error) {
		rd, ok, err := ToReferencedData(msg)
		if err != nil {
			return nil, false, fmt.Errorf("failed to convert message to ReferencedData: %w", err)
		}
		if !ok { // Not ReferencedData. Walk into the message.
			return nil, true, nil
		}

		ref := FromProto(rd)

		// If the reference has already been visited, just verify that the digest is the same.
		refKey := ref.Reference()
		if v, ok := visitedReferencedData[refKey]; ok {
			if v.ref.Digest() != "" && ref.Digest() != "" && v.ref.Digest() != ref.Digest() {
				return nil, false, fmt.Errorf("digest mismatch for reference %q: %q != %q", ref.Name(), v.ref.Digest(), ref.Digest())
			}
			return v.returned, false, nil
		}

		if err := f(ref); err != nil {
			return nil, false, fmt.Errorf("failed to walk reference %q: %w", ref.Name(), err)
		}

		var msgOut proto.Message
		if ref.Modified() {
			if msgOut, err = fromReferencedData(ref.rd, msg); err != nil {
				return nil, false, fmt.Errorf("failed to convert reference %q to target message: %w", ref.Name(), err)
			}
		}

		// For non-inlined ReferencedData, record the reference and the returned value.
		if refKey != "" {
			visitedReferencedData[refKey] = visited{
				ref:      ref,
				returned: msgOut,
			}
		}

		return msgOut, false, nil
	})
}

// ToReferencedData tries to convert the input message to ReferencedData, returning whether it
// succeeded.
func ToReferencedData(msg proto.Message) (*rdpb.ReferencedData, bool, error) {
	// Test whether message can be cast to ReferencedData.
	if rd, ok := msg.(*rdpb.ReferencedData); ok {
		return rd, true, nil
	}

	// Test whether message is a dynamicpb version of ReferencedData.
	rd := &rdpb.ReferencedData{}
	if msg.ProtoReflect().Descriptor().FullName() == rd.ProtoReflect().Descriptor().FullName() {
		// Convert to ReferencedData via Any.
		if msgAny, err := anypb.New(msg); err != nil {
			return nil, false, err
		} else if err := msgAny.UnmarshalTo(rd); err != nil {
			return nil, false, err
		}

		return rd, true, nil
	}

	return nil, false, nil
}

// fromReferencedData converts a ReferencedData to a target type.
func fromReferencedData(rd *rdpb.ReferencedData, target proto.Message) (proto.Message, error) {
	switch target.(type) {
	case *rdpb.ReferencedData:
		return rd, nil
	default:
		if rdAny, err := anypb.New(rd); err != nil {
			return nil, err
		} else if err := rdAny.UnmarshalTo(target); err != nil {
			return nil, err
		}
		return target, nil
	}
}

func parseReference(reference string) (ReferenceType, string, bool) {
	if strings.HasPrefix(reference, "intcas://") {
		return CASReferenceType, reference, false
	}
	if strings.HasPrefix(reference, "file://") {
		return FileReferenceType, reference[7:], true
	}
	return FileReferenceType, reference, false
}

// Reader represents ReferencedData and additional fields for reading the data it references.
type Reader struct {
	// Ref is a reference to the data.
	Ref *ReferencedData
	// Reader can be used to read the referenced data.
	Reader io.Reader
	// Size is the size of the referenced data, in bytes.
	Size int64
}

// ProcessOptions contains call-specific options for Processor.Process.
type ProcessOptions struct {
	// InlineThreshold overrides the default inline threshold of the processor.
	InlineThreshold *int64
}

// ProcessOption is an option for Process.
type ProcessOption func(*Reader, *ProcessOptions)

// WithReader specifies a custom io.Reader to provide for the referenced data along with the size
// of the reference data, in bytes.
func WithReader(r io.Reader, sz int64) ProcessOption {
	return func(rdr *Reader, opts *ProcessOptions) {
		rdr.Reader = r
		rdr.Size = sz
	}
}

// WithInlineThresholdOverride overrides the default inline threshold of the processor for this call.
func WithInlineThresholdOverride(threshold int64) ProcessOption {
	return func(rdr *Reader, opts *ProcessOptions) {
		opts.InlineThreshold = &threshold
	}
}

// Process processes a ReferencedData using the specified Processor.
//
// The reference is modified in place to update it to its processed form.
func Process(ctx context.Context, ref *ReferencedData, processor Processor, options ...ProcessOption) error {
	rdr := &Reader{
		Ref: ref,
	}
	opts := &ProcessOptions{}
	for _, opt := range options {
		opt(rdr, opts)
	}

	// Construct a default reader, if possible.
	if rdr.Reader == nil {
		switch ref.Type() {
		case FileReferenceType:
			file, err := os.Open(ref.Reference())
			if err != nil {
				return fmt.Errorf("failed to open data file: %w", err)
			}
			defer file.Close()

			rdr.Reader = file

			fi, err := file.Stat()
			if err != nil {
				return fmt.Errorf("failed to stat data file: %w", err)
			}
			rdr.Size = fi.Size()
		case CASReferenceType:
			if processor.NeedsReaderFor(CASReferenceType) {
				return fmt.Errorf("CAS references cannot be read. got: %v", ref.Reference())
			}
		case InlinedReferenceType:
			rdr.Reader = bytes.NewReader(ref.Inlined())
			rdr.Size = int64(len(ref.Inlined()))
		default:
			return fmt.Errorf("unknown reference type: %d", ref.Type())
		}
	}

	return processor.Process(ctx, rdr, opts)
}

// Progress represents the current progress of processing a ReferencedData.
type Progress struct {
	BytesUploaded int64
	ReferenceName string // e.g. file path
	Stage         Stage
	TotalBytes    int64
}

// ProgressCallback is a function that receives progress updates while processing a ReferencedData.
type ProgressCallback func(*Progress)

func (p Progress) String() string {
	switch p.Stage {
	case StageUploadStart:
		return fmt.Sprintf("Uploading %s (%s)...", p.ReferenceName, formatBytes(p.TotalBytes))
	case StageUploadProgress:
		if p.TotalBytes > 0 {
			percent := p.BytesUploaded * 100 / p.TotalBytes
			return fmt.Sprintf("Uploading: %d%% (%s/%s)", percent, formatBytes(p.BytesUploaded), formatBytes(p.TotalBytes))
		}
		return fmt.Sprintf("Uploading: %s", formatBytes(p.BytesUploaded))
	case StageUploadFinalize:
		return "Finalizing upload..."
	case StageProcessStart:
		return "Processing reference..."
	case StageProcessDone:
		return "Reference processing completed."
	default:
		return ""
	}
}

// Processor is an interface for processing ReferencedData via a Reader.
type Processor interface {
	// NeedsReaderFor returns true if the processor needs an io.Reader for the given reference type.
	NeedsReaderFor(ReferenceType) bool

	// Process processes a ReferencedData. The processor should modify the ReferencedData in place.
	Process(context.Context, *Reader, *ProcessOptions) error
}

type inlineProcessor struct{}

// NeedsReaderFor returns true for all reference types.
func (p *inlineProcessor) NeedsReaderFor(rt ReferenceType) bool {
	return true
}

// Process inlines the data referenced by the given ReferencedData.
func (p *inlineProcessor) Process(ctx context.Context, rdr *Reader, opts *ProcessOptions) error {
	// Nothing to do for already inlined data.
	if rdr.Ref.Reference() == "" {
		return nil
	}

	if rdr.Reader == nil {
		return fmt.Errorf("no reader for referenced data: %v", rdr.Ref)
	}

	b, err := io.ReadAll(rdr.Reader)
	if err != nil {
		return fmt.Errorf("cannot read data: %w", err)
	}

	rdr.Ref.SetInlined(b)

	return nil
}

// InlineProcessor returns a Processor that inlines the data referenced by the given ReferencedData.
//
// NOTE: This processor will read _all_ referenced data into memory and should not be used when
// large data may be referenced.
func InlineProcessor() Processor {
	return &inlineProcessor{}
}

type artifactsProcessor struct {
	aaClient         assetartifactspb.AssetArtifactsClient
	chunkSize        int
	dryRun           bool
	inlineThreshold  int64
	lroClient        lropb.OperationsClient
	progressCallback ProgressCallback

	fallbackError error
	probeAAOnce   sync.Once
}

// ProcessorOption is an option for NewProcessor.
type ProcessorOption func(*artifactsProcessor)

// WithChunkSize sets the chunk size for uploading.
func WithChunkSize(size int) ProcessorOption {
	return func(opts *artifactsProcessor) {
		opts.chunkSize = size
	}
}

// WithDryRun specifies whether to run the processor in dry-run mode.
func WithDryRun(dryRun bool) ProcessorOption {
	return func(opts *artifactsProcessor) {
		opts.dryRun = dryRun
	}
}

// WithInlineThreshold sets the threshold below which files are inlined.
func WithInlineThreshold(threshold int64) ProcessorOption {
	return func(opts *artifactsProcessor) {
		opts.inlineThreshold = threshold
	}
}

// WithProgressCallback sets a callback to receive progress updates during processing.
func WithProgressCallback(cb ProgressCallback) ProcessorOption {
	return func(p *artifactsProcessor) {
		p.progressCallback = cb
	}
}

// WithProgressWriter sets a writer to output progress updates in a default console-friendly format.
func WithProgressWriter(w io.Writer) ProcessorOption {
	return func(p *artifactsProcessor) {
		if w == nil {
			return
		}
		var lastPercent int64 = -1
		p.progressCallback = func(prg *Progress) {
			switch prg.Stage {
			case StageUploadProgress:
				if prg.TotalBytes > 0 {
					// Use integer division to calculate progress percentage (0-100).
					percent := prg.BytesUploaded * 100 / prg.TotalBytes
					// Only write to the console when the integer percentage increases, to reduce console
					// output spam.
					if percent > lastPercent {
						fmt.Fprintf(w, "\r%s", prg)
						lastPercent = percent
					}
				} else {
					// If total size is unknown, print progress updates for every chunk.
					fmt.Fprintf(w, "\r%s", prg)
					lastPercent = 0
				}
			default:
				if lastPercent >= 0 {
					fmt.Fprintln(w)
					lastPercent = -1
				}
				fmt.Fprintln(w, prg)
			}
		}
	}
}

// NeedsReaderFor returns true for file references.
func (p *artifactsProcessor) NeedsReaderFor(rt ReferenceType) bool {
	return rt == FileReferenceType
}

// Process processes the referenced data.
func (p *artifactsProcessor) Process(ctx context.Context, rdr *Reader, opts *ProcessOptions) error {
	if !p.dryRun {
		if p.aaClient == nil {
			return fmt.Errorf("aaClient must be non-nil")
		}
		if p.lroClient == nil {
			return fmt.Errorf("lroClient must be non-nil")
		}

		// We temporarily need to support some fallback behaviors for communicating with remote clusters
		// that do not yet serve AssetArtifacts.
		p.probeAAOnce.Do(func() {
			p.fallbackError = probeAssetArtifacts(ctx, p.aaClient)
		})
	}

	threshold := p.inlineThreshold
	if opts != nil && opts.InlineThreshold != nil {
		threshold = *opts.InlineThreshold
	}

	// Inline small files locally (both dry-run and normal mode).
	if (rdr.Ref.Type() == FileReferenceType || rdr.Ref.Type() == InlinedReferenceType) && rdr.Size <= threshold {
		b, err := io.ReadAll(rdr.Reader)
		if err != nil {
			return fmt.Errorf("failed to read data for inlining: %w", err)
		}
		rdr.Ref.SetInlined(b)

		// Do not set digest for inlined files to maintain backward compatibility with old platforms.
		rdr.Ref.SetDigest("")

		return nil
	}

	if p.dryRun {
		if rdr.Ref.Type() == CASReferenceType {
			return nil
		}

		hasher := sha512.New()
		if _, err := io.Copy(hasher, rdr.Reader); err != nil {
			return fmt.Errorf("failed to hash data for dry-run: %w", err)
		}
		hash := fmt.Sprintf("%x", hasher.Sum(nil))
		rdr.Ref.SetReference("intcas://" + hash)
		rdr.Ref.SetDigest("sha512:" + hash)

		return nil
	}

	// If AssetArtifacts is unavailable, fall back to a basic behavior.
	if p.fallbackError != nil {
		return p.fallbackProcess(rdr)
	}

	origRefName := rdr.Ref.Reference()
	var processedRef *rdpb.ReferencedData
	switch rt := rdr.Ref.Type(); rt {
	case CASReferenceType:
		processedRef = rdr.Ref.ToProto()
	case FileReferenceType, InlinedReferenceType:
		var err error
		processedRef, err = p.uploadWithRetries(ctx, rdr, origRefName)
		if err != nil {
			return fmt.Errorf("failed to upload %q: %w", origRefName, err)
		}
	default:
		return fmt.Errorf("unknown reference type: %v", rt)
	}

	// Process the referenced data.
	p.update(&Progress{
		Stage:         StageProcessStart,
		ReferenceName: origRefName,
	})
	op, err := p.aaClient.Process(ctx, &assetartifactspb.ProcessRequest{
		Artifact: &assetartifactspb.ProcessRequest_ReferencedData{
			ReferencedData: processedRef,
		},
	})
	if err != nil {
		return fmt.Errorf("failed to call Process: %w", err)
	}
	if op == nil {
		return fmt.Errorf("server returned success but nil operation")
	}

	// Wait for the LRO to complete.
	for !op.GetDone() {
		op, err = p.lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
			Name: op.GetName(),
		})
		if err != nil {
			return fmt.Errorf("failed to wait for operation: %w", err)
		}
	}

	if errProto := op.GetError(); errProto != nil {
		return fmt.Errorf("processing operation failed: %v", status.FromProto(errProto).Err())
	}

	resp := &assetartifactspb.ProcessResponse{}
	if err := op.GetResponse().UnmarshalTo(resp); err != nil {
		return fmt.Errorf("failed to unmarshal response: %w", err)
	}

	rdr.Ref.Replace(FromProto(resp.GetReferencedData()))

	p.update(&Progress{
		Stage:         StageProcessDone,
		ReferenceName: origRefName,
	})

	return nil
}

func (p *artifactsProcessor) fallbackProcess(rdr *Reader) error {
	switch rdr.Ref.Type() {
	case CASReferenceType, InlinedReferenceType:
		// Already in CAS or inlined, nothing to do for old servers.
		return nil
	case FileReferenceType:
		// We cannot upload large files to old servers during install.
		return fmt.Errorf("large file references cannot be processed; AssetArtifacts is unavailable: %w", p.fallbackError)
	default:
		return fmt.Errorf("unknown reference type: %v", rdr.Ref.Type())
	}
}

// uploadWithRetries wraps upload with session-level retries to recover from in-memory session loss
// (such as server pod restarts returning NotFound or session aborts returning Aborted).
//
// Unlike transport-level errors which are handled with exponential backoff by the gRPC client
// ServiceConfig in baseclientutils.go (i.e., for transient errors like UNAVAILABLE or
// RESOURCE_EXHAUSTED), session loss indicates that the server has already restarted and is healthy.
// Therefore, this function rewinds the seekable reader and retries upload() immediately.
func (p *artifactsProcessor) uploadWithRetries(ctx context.Context, rdr *Reader, refName string) (*rdpb.ReferencedData, error) {
	// We need to seek back to the beginning of the reader on retries below.
	seeker, cleanup, err := seekable.From(rdr.Reader)
	if err != nil {
		return nil, fmt.Errorf("failed to make reader seekable for %q: %w", refName, err)
	}
	defer cleanup()
	rdr.Reader = seeker

	digest := rdr.Ref.Digest()
	if !strings.HasPrefix(digest, "sha512:") {
		hasher := sha512.New()
		if _, err := io.Copy(hasher, seeker); err != nil {
			return nil, fmt.Errorf("failed to compute digest for %q: %w", refName, err)
		}
		if _, err := seeker.Seek(0, io.SeekStart); err != nil {
			return nil, fmt.Errorf("failed to seek reader to start after computing digest: %w", err)
		}
		digest = fmt.Sprintf("sha512:%x", hasher.Sum(nil))
	}

	var uploadErr error
	for attempt := 1; attempt <= maxUploadAttempts; attempt++ {
		if attempt > 1 {
			log.WarningContextf(ctx, "Retrying upload session for %q (attempt %d/%d) after session error: %v", refName, attempt, maxUploadAttempts, uploadErr)

			if _, err := seeker.Seek(0, io.SeekStart); err != nil {
				return nil, fmt.Errorf("failed to seek reader to start for retry: %w", err)
			}
		}

		var ref *rdpb.ReferencedData
		ref, uploadErr = p.upload(ctx, rdr, refName, digest)
		if uploadErr == nil {
			return ref, nil
		}

		// Abort unless we have a retryable code.
		switch status.Code(uploadErr) {
		case codes.Aborted, codes.NotFound:
		default:
			return nil, fmt.Errorf("non-retryable upload error for %q (attempt %d/%d): %w", refName, attempt, maxUploadAttempts, uploadErr)
		}
	}

	return nil, fmt.Errorf("failed to upload %q after %d attempts: %w", refName, maxUploadAttempts, uploadErr)
}

func (p *artifactsProcessor) upload(ctx context.Context, rdr *Reader, refName, digest string) (*rdpb.ReferencedData, error) {
	// Start upload session.
	p.update(&Progress{
		Stage:         StageUploadStart,
		ReferenceName: refName,
		TotalBytes:    rdr.Size,
	})
	startResp, err := p.aaClient.StartUpload(ctx, &assetartifactspb.StartUploadRequest{
		Digest: digest,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to start upload: %w", err)
	}
	uploadID := startResp.GetUploadId()

	if !startResp.GetArtifactExists() {
		// Stream the data chunks.
		buf := make([]byte, p.chunkSize)
		var offset int64 = 0
		for {
			n, err := rdr.Reader.Read(buf)
			if err != io.EOF && err != nil {
				return nil, fmt.Errorf("failed to read data: %w", err)
			}
			if n > 0 {
				_, err := p.aaClient.UploadChunk(ctx, &assetartifactspb.UploadChunkRequest{
					Data:     buf[:n],
					Offset:   offset,
					UploadId: uploadID,
				})
				if err != nil {
					return nil, fmt.Errorf("failed to upload chunk: %w", err)
				}
				offset += int64(n)
				p.update(&Progress{
					BytesUploaded: offset,
					ReferenceName: refName,
					Stage:         StageUploadProgress,
					TotalBytes:    rdr.Size,
				})
			}
			if err == io.EOF {
				break
			}
		}
	}

	// Finalize the upload.
	p.update(&Progress{
		ReferenceName: refName,
		Stage:         StageUploadFinalize,
		TotalBytes:    rdr.Size,
	})
	finalizeResp, err := p.aaClient.FinalizeUpload(ctx, &assetartifactspb.FinalizeUploadRequest{
		ExpectedDigest: digest,
		UploadId:       uploadID,
	})
	if err != nil {
		return nil, fmt.Errorf("failed to finalize upload: %w", err)
	}

	return finalizeResp.GetReferencedData(), nil
}

func (p *artifactsProcessor) update(progress *Progress) {
	if p.progressCallback != nil {
		p.progressCallback(progress)
	}
}

// NewProcessor returns a Processor that prepares ReferencedData using AssetArtifacts.
func NewProcessor(aaClient assetartifactspb.AssetArtifactsClient, lroClient lropb.OperationsClient, options ...ProcessorOption) Processor {
	p := &artifactsProcessor{
		aaClient:        aaClient,
		lroClient:       lroClient,
		inlineThreshold: InlineReferenceFileSizeThresholdBytes,
		chunkSize:       defaultChunkSize,
	}
	for _, opt := range options {
		opt(p)
	}

	return p
}

type solutionReleaseProcessor struct{}

func (p *solutionReleaseProcessor) NeedsReaderFor(rt ReferenceType) bool {
	return rt == FileReferenceType
}

func (p *solutionReleaseProcessor) Process(ctx context.Context, rdr *Reader, opts *ProcessOptions) error {
	switch rdr.Ref.Type() {
	case FileReferenceType:
		// If the file is below the size threshold, inline it. Otherwise, the caller must process the
		// reference on their own manually before releasing the Solution.
		if rdr.Size <= InlineReferenceFileSizeThresholdBytes {
			b, err := io.ReadAll(rdr.Reader)
			if err != nil {
				return fmt.Errorf("failed to read data file: %w", err)
			}
			rdr.Ref.SetInlined(b)
		} else {
			return status.Errorf(codes.Unimplemented, "large file references cannot be processed: %v", rdr.Ref)
		}
	case CASReferenceType:
		// TODO(b/441798373): Validate CAS references for the target project.
	case InlinedReferenceType:
		// Nothing to do.
	default:
		return fmt.Errorf("unknown referenced data: %v", rdr.Ref)
	}

	return nil
}

// NewSolutionReleaseProcessor returns a Processor that prepares ReferencedData for inclusion in a
// Solution template to be released.
//
// File references below a size threshold are inlined. Otherwise, they must be processed manually
// before releasing the Solution.
func NewSolutionReleaseProcessor() Processor {
	return &solutionReleaseProcessor{}
}

// probeAssetArtifacts checks whether the AssetArtifacts service is available.
//
// If the service is not available, it returns the error from the attempt.
func probeAssetArtifacts(ctx context.Context, client assetartifactspb.AssetArtifactsClient) error {
	if client == nil {
		return status.Errorf(codes.Unimplemented, "no AssetArtifacts client provided")
	}
	probeCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	_, err := client.FinalizeUpload(probeCtx, &assetartifactspb.FinalizeUploadRequest{
		UploadId: "dummy",
	})

	code := status.Code(err)
	if code == codes.OK || code == codes.NotFound || code == codes.InvalidArgument {
		return nil
	}

	// Convert raw context errors to gRPC status errors so they wrap and unwrap correctly.
	if errors.Is(err, context.DeadlineExceeded) {
		return status.Error(codes.DeadlineExceeded, err.Error())
	}
	if errors.Is(err, context.Canceled) {
		return status.Error(codes.Canceled, err.Error())
	}

	return err
}

func formatBytes(b int64) string {
	const unit = 1024
	if b < unit {
		return fmt.Sprintf("%d B", b)
	}
	div, exp := int64(unit), 0
	for n := b / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(b)/float64(div), "KMGTPE"[exp])
}
