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

// Package common contains libraries shared across inctl asset instance.
package common

import (
	"encoding/json"
	"fmt"
	"strings"

	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoregistry"
	"google.golang.org/protobuf/types/known/anypb"
	"google.golang.org/protobuf/types/known/wrapperspb"

	aipb "intrinsic/assets/proto/v1/asset_instances_go_proto"
)

const omittedMessage = "-- omitted, information necessary for decoding is available with --view=full --"

func overwriteAny(m *anypb.Any) {
	if m == nil {
		return
	}
	if err := anypb.MarshalFrom(m, &wrapperspb.StringValue{Value: omittedMessage}, proto.MarshalOptions{}); err != nil {
		m.Reset()
	}
}

// ParseView parses a string into an AssetInstanceView.
func ParseView(viewStr string) (aipb.AssetInstanceView, error) {
	switch strings.ToLower(viewStr) {
	case "basic":
		return aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_BASIC, nil
	case "detail":
		return aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_DETAIL, nil
	case "full":
		return aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_FULL, nil
	case "":
		return aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_UNSPECIFIED, nil
	default:
		return aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_UNSPECIFIED, fmt.Errorf("invalid view %q, must be one of: basic, detail, full", viewStr)
	}
}

// ResolvingMessage represents a proto.Message with a custom resolver for JSON marshaling.
type ResolvingMessage interface {
	proto.Message
	protoio.Resolver
	json.Marshaler
}

// TypedResolvingMessage represents a ResolvingMessage with a typed getter for the underlying message.
type TypedResolvingMessage[T proto.Message] interface {
	ResolvingMessage
	Typed() T
}

type resolvingMessage[T proto.Message] struct {
	proto.Message
	protoio.Resolver
}

func (m resolvingMessage[T]) MarshalJSON() ([]byte, error) {
	return (protojson.MarshalOptions{Resolver: m.Resolver}).Marshal(m.Message)
}

func (m resolvingMessage[T]) Typed() T {
	return m.Message.(T)
}

// NewResolvingMessage creates a ResolvingMessage.
func NewResolvingMessage(resolver protoio.Resolver, message proto.Message) ResolvingMessage {
	return resolvingMessage[proto.Message]{
		Message:  message,
		Resolver: resolver,
	}
}

// NewTypedResolvingMessage creates a TypedResolvingMessage.
func NewTypedResolvingMessage[T proto.Message](resolver protoio.Resolver, message T) TypedResolvingMessage[T] {
	return resolvingMessage[T]{
		Message:  message,
		Resolver: resolver,
	}
}

// DisplayableInstance overwrites or clears outputs that would be too verbose or cannot be marshaled.
func DisplayableInstance(instance *aipb.AssetInstance) TypedResolvingMessage[*aipb.AssetInstance] {
	var resolver protoio.Resolver
	if fds := instance.GetMetadata().GetFileDescriptorSet(); fds != nil {
		if r, err := registryutil.NewTypesFromFileDescriptorSet(fds); err == nil {
			resolver = r
		}
		fds.Reset()
	}
	if resolver == nil {
		resolver = protoregistry.GlobalTypes
		overwriteAny(instance.GetConfig().GetService().GetServiceConfig())
		overwriteAny(instance.GetConfig().GetHardwareDevice().GetService().GetServiceConfig())
	}
	return NewTypedResolvingMessage(resolver, instance)
}
