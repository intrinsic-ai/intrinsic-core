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

// Package descriptor provides utilities for working with proto descriptors.
package descriptor

import (
	"fmt"

	"intrinsic/util/proto/sourcecodeinfoview"

	log "github.com/golang/glog"
	"github.com/kylelemons/godebug/diff"            
	"google.golang.org/protobuf/encoding/prototext" 
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"

	dpb "google.golang.org/protobuf/types/descriptorpb"

	// Register common protobuf extensions to prevent unknown field diffs. MergeFileDescriptorSets
	// uses proto.Equal, which performs a strict byte comparison on unknown fields. This can cause
	// false negatives if identical fields were serialized in a different order.
	_ "cloud.google.com/go/longrunning/autogen/longrunningpb"
	_ "google.golang.org/genproto/googleapis/api/annotations"
)

// ComparisonPreprocessor preprocesses a FileDescriptorSet to a form to use when comparing with
// another FileDescriptorSet.
type ComparisonPreprocessor func(*dpb.FileDescriptorSet) (*dpb.FileDescriptorSet, error)

// mergeFileDescriptorSetsOptions contains options for MergeFileDescriptorSets.
type mergeFileDescriptorSetsOptions struct {
	keys                   []string
	comparisonPreprocessor ComparisonPreprocessor
	skipValidation         bool
}

// MergeFileDescriptorSetsOption is a functional option for MergeFileDescriptorSets.
type MergeFileDescriptorSetsOption func(*mergeFileDescriptorSetsOptions)

// WithKeys is an option for MergeFileDescriptorSets that specifies the keys of the assets to
// merge. These keys are used to provide context in error messages if duplicate FileDescriptorProtos
// are found.
//
// For example, when merging FileDescriptorSets for multiple assets, each asset's id can be used
// as the key.
func WithKeys(keys []string) MergeFileDescriptorSetsOption {
	return func(opts *mergeFileDescriptorSetsOptions) {
		opts.keys = keys
	}
}

// WithComparisonPreprocessor specifies a function to pass input FileDescriptorSets through to
// construct a form to use for equality of FileDescriptorProtos between FileDescriptorSets.
func WithComparisonPreprocessor(preprocessor ComparisonPreprocessor) MergeFileDescriptorSetsOption {
	return func(opts *mergeFileDescriptorSetsOptions) {
		opts.comparisonPreprocessor = preprocessor
	}
}

// WithStrictEqualityComparison specifies that FileDescriptorProtos compare as equal only if they
// are strictly identical.
func WithStrictEqualityComparison() MergeFileDescriptorSetsOption {
	return WithComparisonPreprocessor(func(fds *dpb.FileDescriptorSet) (*dpb.FileDescriptorSet, error) {
		return fds, nil
	})
}

func WithSourceCodePrunedComparison() MergeFileDescriptorSetsOption {
	return WithComparisonPreprocessor(func(fds *dpb.FileDescriptorSet) (*dpb.FileDescriptorSet, error) {
		fdsPruned := proto.Clone(fds).(*dpb.FileDescriptorSet)
		if err := sourcecodeinfoview.PruneSourceCodeInfo(fdsPruned); err != nil {
			return nil, fmt.Errorf("failed to prune source code info: %v", err)
		}
		return fdsPruned, nil
	})
}

// WithSkipValidation specifies whether to skip validation of the merged FileDescriptorSet.
func WithSkipValidation(b bool) MergeFileDescriptorSetsOption {
	return func(opts *mergeFileDescriptorSetsOptions) {
		opts.skipValidation = b
	}
}

func defaultMergedFileDescriptorSetsKeys(n int) []string {
	keys := make([]string, n)
	for i := range keys {
		keys[i] = fmt.Sprintf("FileDescriptorSet %d", i)
	}
	return keys
}

type fileDescriptorAndKey struct {
	f   *dpb.FileDescriptorProto
	key string
}

// MergeFileDescriptorSets merges a list of FileDescriptorSets into a single FileDescriptorSet.
//
// It returns an error if any duplicate FileDescriptorProtos (by name) are not equal according to
// the specified ComparisonPreprocessor (e.g., WithStrictEqualityComparison or
// WithSourceCodePrunedComparison), which defaults to that specified by
// WithSourceCodePrunedComparison.
func MergeFileDescriptorSets(fdss []*dpb.FileDescriptorSet, options ...MergeFileDescriptorSetsOption) (*dpb.FileDescriptorSet, error) {
	opts := mergeFileDescriptorSetsOptions{}
	options = append([]MergeFileDescriptorSetsOption{
		WithSourceCodePrunedComparison(),
	}, options...)
	for _, opt := range options {
		opt(&opts)
	}
	if len(opts.keys) == 0 {
		opts.keys = defaultMergedFileDescriptorSetsKeys(len(fdss))
	}
	if len(opts.keys) != len(fdss) {
		return nil, fmt.Errorf("len(keys) (%d) != len(fdss) (%d)", len(opts.keys), len(fdss))
	}

	// Merge them into a single FileDescriptorSet.
	merged := &dpb.FileDescriptorSet{}
	// `seen` maps FileDescriptorProto.name to (pruned) FileDescriptorProto and the key that
	// referenced it.
	seen := map[string]fileDescriptorAndKey{}
	for i, fds := range fdss {
		if fds == nil {
			continue
		}

		fdsCmp, err := opts.comparisonPreprocessor(fds)
		if err != nil {
			return nil, fmt.Errorf("failed to preprocess FileDescriptorSet for %q: %v", opts.keys[i], err)
		}
		filesCmp := fdsCmp.GetFile()
		for j, f := range fds.GetFile() {
			fCmp := filesCmp[j]
			if existing, ok := seen[f.GetName()]; !ok {
				merged.File = append(merged.File, f)
				seen[f.GetName()] = fileDescriptorAndKey{f: fCmp, key: opts.keys[i]}
				continue
			} else if !proto.Equal(existing.f, fCmp) {
				// We currently require that any duplicate FileDescriptorProtos are equal. We could
				// potentially relax this constraint later by determining which of multiple compatible
				// FileDescriptorProtos we find is the "latest" one and using that.
				var d string

				d = fmt.Sprintf(". diff:\n%v", diff.Diff(prototext.Format(existing.f), prototext.Format(fCmp)))

				log.Errorf("duplicate FileDescriptorProto %q with different contents for %q and %q%s", f.GetName(), existing.key, opts.keys[i], d)
				return nil, fmt.Errorf("duplicate FileDescriptorProto %q with different contents for %q and %q", f.GetName(), existing.key, opts.keys[i])
			}
		}
	}

	if !opts.skipValidation {
		// Check that the merged FileDescriptorSet is valid.
		if _, err := protodesc.NewFiles(merged); err != nil {
			return nil, fmt.Errorf("failed to generate a valid merged FileDescriptorSet (sets were likely built at different times): %w", err)
		}
	}

	return merged, nil
}

// FileDescriptorSetFrom constructs a FileDescriptorSet for the specified message.
func FileDescriptorSetFrom(msg proto.Message) *dpb.FileDescriptorSet {
	return &dpb.FileDescriptorSet{
		File: collectDependencies(msg.ProtoReflect().Descriptor().ParentFile(), make(map[string]bool)),
	}
}

// collectDependencies collects all dependencies of the specified file descriptor.
//
// Only returns the dependencies that are not already in the seen map. Initial callers should pass
// in an empty map.
func collectDependencies(desc protoreflect.FileDescriptor, seen map[string]bool) []*dpb.FileDescriptorProto {
	if desc == nil || seen[desc.Path()] {
		return []*dpb.FileDescriptorProto{}
	}
	seen[desc.Path()] = true
	result := []*dpb.FileDescriptorProto{
		protodesc.ToFileDescriptorProto(desc),
	}

	fileImports := desc.Imports()
	for i := 0; i < fileImports.Len(); i++ {
		for _, dep := range collectDependencies(fileImports.Get(i), seen) {
			result = append(result, dep)
			seen[dep.GetName()] = true
		}
	}

	return result
}
