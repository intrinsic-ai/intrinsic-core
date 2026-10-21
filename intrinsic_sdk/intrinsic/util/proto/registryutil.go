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

// Package registryutil defines common functions for working with protoregistry
// types.
package registryutil

import (
	"fmt"

	"intrinsic/util/proto/protoio"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	dynamicpb "google.golang.org/protobuf/types/dynamicpb"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

// LoadFileDescriptorSets loads a list of binary proto files from disk and returns a
// populated FileDescriptorSet proto.  An empty set of paths returns a nil set.
func LoadFileDescriptorSets(paths []string) (*descriptorpb.FileDescriptorSet, error) {
	if len(paths) == 0 {
		return nil, nil
	}
	set := &descriptorpb.FileDescriptorSet{}
	for _, path := range paths {
		if err := protoio.ReadBinaryProto(path, set, protoio.WithMerge(true)); err != nil {
			return nil, fmt.Errorf("failed to read file descriptor set %q: %v", path, err)
		}
	}
	return set, nil
}

// NewFilesFromFileDescriptorSets creates a protoregistry Files object from a
// set of binary proto files on disk.  The set of files is required to be
// complete, as unresolved paths will result in an error.  If the set is nil,
// nil files will be returned.
func NewFilesFromFileDescriptorSets(paths []string) (*protoregistry.Files, error) {
	set, err := LoadFileDescriptorSets(paths)
	if err != nil {
		return nil, err
	}
	if set == nil {
		return nil, nil
	}

	files, err := protodesc.NewFiles(set)
	if err != nil {
		return nil, fmt.Errorf("failed to create a new proto descriptor: %v", err)
	}

	return files, nil
}

// NewTypesFromFileDescriptorSet creates a new protoregistry Types from a
// complete file descriptor set.  Unresolved paths will result in an error.  A
// nil set returns a nil types.
func NewTypesFromFileDescriptorSet(set *descriptorpb.FileDescriptorSet) (*protoregistry.Types, error) {
	if set == nil {
		return new(protoregistry.Types), nil
	}

	files, err := protodesc.NewFiles(set)
	if err != nil {
		return nil, fmt.Errorf("failed to create a new proto descriptor: %v", err)
	}

	types := new(protoregistry.Types)
	if err := PopulateTypesFromFiles(types, files); err != nil {
		return nil, fmt.Errorf("failed to populate the registry: %v", err)
	}

	return types, nil
}

// PopulateTypesFromFiles adds in all Messages, Enums, and Extensions held
// within a Files object into the provided Type.  t may be modified prior to
// returning an error.  Types from f that already exist in t will be ignored.
func PopulateTypesFromFiles(t *protoregistry.Types, f *protoregistry.Files) error {
	var topLevelErr error
	f.RangeFiles(func(f protoreflect.FileDescriptor) bool {
		if err := addFile(t, f); err != nil {
			topLevelErr = err
			return false
		}
		return true
	})
	return topLevelErr
}

func addFile(t *protoregistry.Types, f protoreflect.FileDescriptor) error {
	if err := addMessagesRecursively(t, f.Messages()); err != nil {
		return err
	}
	if err := addEnums(t, f.Enums()); err != nil {
		return err
	}
	if err := addExtensions(t, f.Extensions()); err != nil {
		return err
	}
	return nil
}

func addMessagesRecursively(t *protoregistry.Types, ms protoreflect.MessageDescriptors) error {
	for i := 0; i < ms.Len(); i++ {
		m := ms.Get(i)
		if _, err := t.FindMessageByName(m.FullName()); err == protoregistry.NotFound {
			if err := t.RegisterMessage(dynamicpb.NewMessageType(m)); err != nil {
				return err
			}
			if err := addEnums(t, m.Enums()); err != nil {
				return err
			}
			if err := addExtensions(t, m.Extensions()); err != nil {
				return err
			}
			if err := addMessagesRecursively(t, m.Messages()); err != nil {
				return err
			}
		} else if err != nil {
			return err
		}
	}
	return nil
}

func addEnums(t *protoregistry.Types, enums protoreflect.EnumDescriptors) error {
	for i := 0; i < enums.Len(); i++ {
		enum := enums.Get(i)
		if _, err := t.FindEnumByName(enum.FullName()); err == protoregistry.NotFound {
			t.RegisterEnum(dynamicpb.NewEnumType(enum))
		} else if err != nil {
			return err
		}
	}
	return nil
}

func addExtensions(t *protoregistry.Types, exts protoreflect.ExtensionDescriptors) error {
	for i := 0; i < exts.Len(); i++ {
		ext := exts.Get(i)
		if _, err := t.FindExtensionByName(ext.FullName()); err == protoregistry.NotFound {
			if err := t.RegisterExtension(dynamicpb.NewExtensionType(ext)); err != nil {
				return nil
			}
		} else if err != nil {
			return err
		}
	}
	return nil
}
