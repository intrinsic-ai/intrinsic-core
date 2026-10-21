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

// Package sourcecodeinfoview offers tools for working with SourceCodeInfo in proto descriptors.
package sourcecodeinfoview

import (
	"fmt"
	"slices"

	"google.golang.org/protobuf/reflect/protodesc"
	"google.golang.org/protobuf/reflect/protoreflect"
	dpb "google.golang.org/protobuf/types/descriptorpb"
)

type pathInfo struct {
	// The message (or service) that this path is associated with.
	messageName string
	// The full name. This will equal messageName for top-level message or service
	// comments. Message fields or service methods will equal the message name
	// with an added suffix.
	fullName       string
	leadingComment string
}

type pathKey struct {
	// The file that this path comes from.
	file string
	// The path as a string. E.g., .message_type[0].nested_type[1].field[2]
	path string
}

func (k pathKey) String() string {
	return fmt.Sprintf("%s %s", k.file, k.path)
}

func addEdge(from string, to string, graph map[string]map[string]struct{}) {
	if _, exists := graph[from]; !exists {
		graph[from] = map[string]struct{}{}
	}
	graph[from][to] = struct{}{}
}

func addPath(md protoreflect.Descriptor, d protoreflect.Descriptor, paths map[pathKey]pathInfo) {
	filePath := md.ParentFile().Path()
	sourceLocation := md.ParentFile().SourceLocations().ByDescriptor(d)
	// The source location path might be missing if the source code has already been pruned.
	if sourceLocation.Path == nil {
		return
	}
	key := pathKey{
		file: filePath,
		path: sourceLocation.Path.String(),
	}
	paths[key] = pathInfo{
		messageName:    string(md.FullName()),
		fullName:       string(d.FullName()),
		leadingComment: sourceLocation.LeadingComments,
	}
}

func addMessageDependencies(md protoreflect.MessageDescriptor, graph map[string]map[string]struct{}, paths map[pathKey]pathInfo) {
	// This is the comment of a proto message definition.
	addPath(md, md, paths)

	from := string(md.FullName())
	addEdge(from, from, graph)
	for i := 0; i < md.Fields().Len(); i++ {
		fd := md.Fields().Get(i)
		// This is the comment associated with a field in the proto message.
		addPath(md, fd, paths)
		if fd.Kind() == protoreflect.MessageKind {
			to := string(fd.Message().FullName())
			addEdge(from, to, graph)
		}
	}

	// Process nested proto messages.
	for i := 0; i < md.Messages().Len(); i++ {
		nested := md.Messages().Get(i)
		addMessageDependencies(nested, graph, paths)
	}
}

func addServiceDependencies(sd protoreflect.ServiceDescriptor, graph map[string]map[string]struct{}, paths map[pathKey]pathInfo) {
	// This is the comment of a gRPC service definition.
	addPath(sd, sd, paths)

	from := string(sd.FullName())
	addEdge(from, from, graph)
	for i := 0; i < sd.Methods().Len(); i++ {
		md := sd.Methods().Get(i)
		// This is the comment associated with a method of a service.
		addPath(sd, md, paths)

		// Add the method's input proto as a dependency.
		im := md.Input()
		to := string(im.FullName())
		addEdge(from, to, graph)
		// Add the method's output proto as a dependency.
		om := md.Output()
		to = string(om.FullName())
		addEdge(from, to, graph)
	}
}

func dependencyGraph(fds *dpb.FileDescriptorSet) (map[string]map[string]struct{}, map[pathKey]pathInfo, error) {
	// A map between full message names to set of direct dependencies.
	graph := map[string]map[string]struct{}{}
	// A set of descriptor location "paths" that we need to retain. These paths
	// currently hold message, nested messages, and message field descriptors. All
	// other sources will be pruned.
	pathsWithFile := map[pathKey]pathInfo{}

	files, err := protodesc.NewFiles(fds)
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create new files: %w\n\nfds: %v", err, fds)
	}
	files.RangeFiles(func(f protoreflect.FileDescriptor) bool {
		for i := 0; i < f.Messages().Len(); i++ {
			md := f.Messages().Get(i)
			addMessageDependencies(md, graph, pathsWithFile)
		}
		for i := 0; i < f.Services().Len(); i++ {
			sd := f.Services().Get(i)
			addServiceDependencies(sd, graph, pathsWithFile)
		}
		return true
	})
	return graph, pathsWithFile, nil
}

func allDependencies(fullNames []string, graph map[string]map[string]struct{}) map[string]struct{} {
	deps := map[string]struct{}{}
	stack := []string{}
	for _, fullName := range fullNames {
		if _, exists := graph[fullName]; exists {
			stack = append(stack, fullName)
		}
	}
	for len(stack) > 0 {
		popped := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		if _, exists := deps[popped]; !exists {
			deps[popped] = struct{}{}
			neighbors, _ := graph[popped]
			for fn := range neighbors {
				stack = append(stack, fn)
			}
		}
	}
	return deps
}

func anyMessageInDepSet(file *dpb.FileDescriptorProto, depSet map[string]struct{}) bool {
	pkg := file.GetPackage()
	var checkfn func(path string, msgDesc *dpb.DescriptorProto, depSet map[string]struct{}) bool
	checkfn = func(path string, msgDesc *dpb.DescriptorProto, depSet map[string]struct{}) bool {
		fullName := path + "." + msgDesc.GetName()
		if _, exists := depSet[fullName]; exists {
			return true
		}
		for _, nmt := range msgDesc.GetNestedType() {
			if exists := checkfn(fullName, nmt, depSet); exists {
				return true
			}
		}
		return false
	}
	for _, mt := range file.GetMessageType() {
		if exists := checkfn(pkg, mt, depSet); exists {
			return true
		}
	}
	return false
}

// NestedFieldCommentMap returns a mapping of fully qualified messages and
// fields to the leading comments of those fields. This is useful for turning
// comments in proto files into tooltips in the UI.
func NestedFieldCommentMap(protoDescriptors *dpb.FileDescriptorSet, messageFullName string) (map[string]string, error) {
	if len(protoDescriptors.GetFile()) == 0 {
		return nil, fmt.Errorf("a FileDescriptorSet is required, but %v was given instead", protoDescriptors)
	}

	depGraph, pathsWithFile, err := dependencyGraph(protoDescriptors)
	if err != nil {
		return nil, err
	}
	depSet := allDependencies([]string{messageFullName}, depGraph)
	if _, exists := depSet[messageFullName]; !exists {
		return nil, fmt.Errorf("did not find message %v in given file descriptor set", messageFullName)
	}

	comments := map[string]string{}
	for _, file := range protoDescriptors.GetFile() {
		locations := file.GetSourceCodeInfo().GetLocation()
		for _, l := range locations {
			pi, exists := pathsWithFile[pathKey{file: file.GetName(), path: protoreflect.SourcePath(l.Path).String()}]
			if !exists {
				continue
			}
			if _, exists := depSet[pi.messageName]; exists {
				comments[pi.fullName] = pi.leadingComment
			}
		}
	}
	return comments, nil
}

type pruneSourceCodeInfoOptions struct {
	excludeNames []string
}

// PruneSourceCodeInfoOption is an option for PruneSourceCodeInfo.
type PruneSourceCodeInfoOption func(*pruneSourceCodeInfoOptions)

// WithExcludeNames specifies a list of message type names to partially-exclude from the pruning
// (along with their transitive dependencies).
//
// Leading detached comments are always pruned.
func WithExcludeNames(excludeNames []string) PruneSourceCodeInfoOption {
	return func(opts *pruneSourceCodeInfoOptions) {
		opts.excludeNames = excludeNames
	}
}

// PruneSourceCodeInfo removes comments and other source code information from the specified
// FileDescriptorSet.
func PruneSourceCodeInfo(fds *dpb.FileDescriptorSet, options ...PruneSourceCodeInfoOption) error {
	if fds == nil {
		return nil
	}

	opts := pruneSourceCodeInfoOptions{}
	for _, opt := range options {
		opt(&opts)
	}

	var excludedDepSet map[string]struct{}
	var pathsWithFile map[pathKey]pathInfo
	if len(opts.excludeNames) > 0 {
		var depGraph map[string]map[string]struct{}
		var err error
		depGraph, pathsWithFile, err = dependencyGraph(fds)
		if err != nil {
			return err
		}
		excludedDepSet = allDependencies(opts.excludeNames, depGraph)
	}

	for _, file := range fds.GetFile() {
		// Skip files that lack source code info. This happens when someone asks protoc to generate a
		// file descriptor set without the argument --include_source_info.
		if file.GetSourceCodeInfo() == nil {
			continue
		}

		// We keep comments in any file that contains at least one message that belong to the set of
		// excluded transitive dependencies.
		if !anyMessageInDepSet(file, excludedDepSet) {
			file.SourceCodeInfo = nil
			continue
		}

		// Remove all leading detached comments and spans as we do not need them.
		locations := file.GetSourceCodeInfo().GetLocation()
		for _, l := range locations {
			l.LeadingDetachedComments = nil
		}

		// Remove source locations that don't point to a proto message location.
		file.GetSourceCodeInfo().Location = slices.DeleteFunc(locations, func(l *dpb.SourceCodeInfo_Location) bool {
			_, exists := pathsWithFile[pathKey{file: file.GetName(), path: protoreflect.SourcePath(l.Path).String()}]
			return !exists
		})
	}
	return nil
}
