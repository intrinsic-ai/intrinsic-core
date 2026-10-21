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

package any

import (
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
)

// GreedyResolver implements Resolver, returning the first result found from its list of resolvers.
type GreedyResolver struct {
	resolvers []Resolver
}

// NewGreedyResolver returns a new GreedyResolver instance.
func NewGreedyResolver(resolvers []Resolver) *GreedyResolver {
	return &GreedyResolver{
		resolvers: resolvers,
	}
}

// FindMessageByName looks up a message by its full name.
// E.g., "google.protobuf.Any"
//
// This returns (nil, NotFound) if not found.
func (r *GreedyResolver) FindMessageByName(message protoreflect.FullName) (protoreflect.MessageType, error) {
	for _, resolver := range r.resolvers {
		msg, err := resolver.FindMessageByName(message)
		if err == nil {
			return msg, nil
		}
	}
	return nil, protoregistry.NotFound
}

// FindMessageByURL looks up a message by a URL identifier.
// See documentation on google.protobuf.Any.type_url for the URL format.
//
// This returns (nil, NotFound) if not found.
func (r *GreedyResolver) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	for _, resolver := range r.resolvers {
		msg, err := resolver.FindMessageByURL(url)
		if err == nil {
			return msg, nil
		}
	}
	return nil, protoregistry.NotFound
}

// FindExtensionByName looks up an extension field by the field's full name.
func (r *GreedyResolver) FindExtensionByName(field protoreflect.FullName) (protoreflect.ExtensionType, error) {
	for _, resolver := range r.resolvers {
		ext, err := resolver.FindExtensionByName(field)
		if err == nil {
			return ext, nil
		}
	}
	return nil, protoregistry.NotFound
}

// FindExtensionByNumber looks up an extension field by the field number.
func (r *GreedyResolver) FindExtensionByNumber(message protoreflect.FullName, field protoreflect.FieldNumber) (protoreflect.ExtensionType, error) {
	for _, resolver := range r.resolvers {
		ext, err := resolver.FindExtensionByNumber(message, field)
		if err == nil {
			return ext, nil
		}
	}
	return nil, protoregistry.NotFound
}
