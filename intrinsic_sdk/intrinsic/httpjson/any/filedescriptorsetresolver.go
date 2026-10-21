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

// Package any resolves Any messages on a best-effort basis given only the type_url.
package any

import (
	"fmt"
	"strings"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	"google.golang.org/protobuf/types/descriptorpb"

	"intrinsic/util/proto/registryutil"
)

// FileDescriptorSetResolver resolves types from file descriptor set binary files.
type FileDescriptorSetResolver struct {
	types *protoregistry.Types
}

// combinedResolver resolves dependencies during file descriptor compilation.
// It is designed to work even if the input FileDescriptorSet is not fully
// self-contained (e.g. if standard imports like google/protobuf/any.proto are
// omitted from the set, they will be resolved from globally linked files).
type combinedResolver struct {
	local  *protoregistry.Files
	global *protoregistry.Files
}

func (r combinedResolver) FindFileByPath(path string) (protoreflect.FileDescriptor, error) {
	if f, err := r.local.FindFileByPath(path); err == nil {
		return f, nil
	}
	return r.global.FindFileByPath(path)
}

func (r combinedResolver) FindDescriptorByName(name protoreflect.FullName) (protoreflect.Descriptor, error) {
	if d, err := r.local.FindDescriptorByName(name); err == nil {
		return d, nil
	}
	return r.global.FindDescriptorByName(name)
}

// NewFileDescriptorSetResolver creates a resolver from a set of runfiles paths.
func NewFileDescriptorSetResolver(runfilesPaths []string) (*FileDescriptorSetResolver, error) {
	r, err := runfiles.New()
	if err != nil {
		return nil, fmt.Errorf("failed to initialize runfiles: %w", err)
	}

	var absolutePaths []string
	for _, path := range runfilesPaths {
		absPath, err := r.Rlocation(path)
		if err != nil {
			return nil, fmt.Errorf("failed to locate runfile %q: %w", path, err)
		}
		absolutePaths = append(absolutePaths, absPath)
	}

	set, err := registryutil.LoadFileDescriptorSets(absolutePaths)
	if err != nil {
		return nil, fmt.Errorf("failed to load file descriptor sets: %w", err)
	}

	types, err := createTypesFromDescriptorSet(set)
	if err != nil {
		return nil, fmt.Errorf("failed to create types from descriptor set: %w", err)
	}

	return &FileDescriptorSetResolver{
		types: types,
	}, nil
}

func createTypesFromDescriptorSet(set *descriptorpb.FileDescriptorSet) (*protoregistry.Types, error) {
	if set == nil || len(set.File) == 0 {
		return nil, fmt.Errorf("no file descriptor set loaded")
	}
	files := new(protoregistry.Files)
	resolver := combinedResolver{
		local:  files,
		global: protoregistry.GlobalFiles,
	}

	for _, fileProto := range set.File {
		name := fileProto.GetName()
		if _, err := files.FindFileByPath(name); err == nil {
			continue
		}
		if _, err := protoregistry.GlobalFiles.FindFileByPath(name); err == nil {
			continue
		}

		fd, err := protodesc.NewFile(fileProto, resolver)
		if err != nil {
			return nil, fmt.Errorf("failed to compile descriptor for %s: %w", name, err)
		}

		if err := files.RegisterFile(fd); err != nil {
			return nil, fmt.Errorf("failed to register file %s: %w", name, err)
		}
	}

	types := new(protoregistry.Types)
	if err := registryutil.PopulateTypesFromFiles(types, files); err != nil {
		return nil, fmt.Errorf("failed to populate registry types: %w", err)
	}
	return types, nil
}

// FindMessageByName looks up a message by its full name.
func (r *FileDescriptorSetResolver) FindMessageByName(message protoreflect.FullName) (protoreflect.MessageType, error) {
	return r.types.FindMessageByName(message)
}

// FindMessageByURL looks up a message by a URL identifier.
func (r *FileDescriptorSetResolver) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	name := url
	// Strip everything up to the last `/`, because no official protobuf library uses it.
	// https://github.com/protocolbuffers/protobuf/blob/c6f77c18ed34647b5358d9522e5854637df7bea5/src/google/protobuf/any.proto#L150-L153
	if i := strings.LastIndexByte(url, '/'); i >= 0 {
		name = url[i+1:]
	}
	return r.types.FindMessageByName(protoreflect.FullName(name))
}

// FindExtensionByName looks up an extension field by the field's full name.
func (r *FileDescriptorSetResolver) FindExtensionByName(field protoreflect.FullName) (protoreflect.ExtensionType, error) {
	return r.types.FindExtensionByName(field)
}

// FindExtensionByNumber looks up an extension field by the field number.
func (r *FileDescriptorSetResolver) FindExtensionByNumber(message protoreflect.FullName, field protoreflect.FieldNumber) (protoreflect.ExtensionType, error) {
	return r.types.FindExtensionByNumber(message, field)
}
