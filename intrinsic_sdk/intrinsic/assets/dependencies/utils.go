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

// Package utils provides utility functions for Asset dependencies.
package utils

import (
	"context"
	"errors"
	"fmt"
	"maps"
	"slices"
	"strings"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"

	"intrinsic/assets/dependencies/platform" 
	"intrinsic/assets/instances/connect"

	dasgrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	daspb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	fieldmetadatapb "intrinsic/assets/proto/field_metadata_go_proto"
	gcpb "intrinsic/assets/proto/v1/grpc_connection_go_proto" 
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"
)

const ingressAddress = "istio-ingressgateway.app-ingress.svc.cluster.local:80"

var (
	errMissingInterface = errors.New("interface not found in resolved dependency")
	errNotGRPC          = errors.New("interface is not gRPC or no connection information is available")
	errNotData          = errors.New("interface is not data or no data dependency information is available")
)

// LINT.IfChange(utils)

// Connect creates a gRPC connection for communicating with the provider of the specified interface.
//
// It also returns a new context that includes any needed metadata for communicating with the
// provider.
func Connect(ctx context.Context, dep *rdpb.ResolvedDependency, iface string) (*grpc.ClientConn, context.Context, error) {
	ifaceProto, err := findInterface(dep, iface)
	if err != nil {
		return nil, nil, err
	}
	connection := ifaceProto.GetGrpc().GetConnection()
	if connection == nil {
		return nil, nil, fmt.Errorf("%w: %q", errNotGRPC, iface)
	}

	return connect.Connect(ctx, connection)
}



// ConnectWithRuntimeAssetFallbackForAssetMigrationOnly is an internal-only version of Connect.
//
// If the interface or its connection details are missing from the resolved dependency,
// it falls back to using the runtime asset instance connection details.
func ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(ctx context.Context, dep *rdpb.ResolvedDependency, iface string) (*grpc.ClientConn, context.Context, error) {
	// Proceed with the standard Connect if no error occurred and connection info is populated.
	if ifaceProto, err := findInterface(dep, iface); err == nil && ifaceProto.GetGrpc().GetConnection() != nil {
		return Connect(ctx, dep, iface)
	}

	cloned := proto.Clone(dep).(*rdpb.ResolvedDependency)
	cloned.Name = platform.RuntimeInstanceName
	if cloned.Interfaces == nil {
		cloned.Interfaces = make(map[string]*rdpb.ResolvedDependency_Interface)
	}
	cloned.Interfaces[iface] = &rdpb.ResolvedDependency_Interface{
		Protocol: &rdpb.ResolvedDependency_Interface_Grpc_{
			Grpc: &rdpb.ResolvedDependency_Interface_Grpc{
				Connection: &gcpb.GrpcConnection{
					Address: ingressAddress,
					Metadata: []*gcpb.GrpcConnection_Metadata{
						{
							Key:   "x-resource-instance-name",
							Value: platform.RuntimeInstanceName,
						},
					},
				},
			},
		},
	}
	return Connect(ctx, cloned, iface)
}



type getDataOptions struct {
	dataAssetsClient dasgrpcpb.DataAssetsClient
}

// GetDataOption is an option for configuring the GetData function.
type GetDataOption func(*getDataOptions)

// WithDataAssetsClient sets the DataAssets client to use.
func WithDataAssetsClient(client dasgrpcpb.DataAssetsClient) GetDataOption {
	return func(opts *getDataOptions) {
		opts.dataAssetsClient = client
	}
}

// GetDataPayload returns the DataAsset payload for the specified interface.
//
// If no DataAssets client is provided, an insecure connection to the DataAssets service via the
// ingress gateway will be created. This connection is valid for services running in the same
// cluster as the DataAssets service.
func GetDataPayload(ctx context.Context, dep *rdpb.ResolvedDependency, iface string, options ...GetDataOption) (*anypb.Any, error) {
	opts := &getDataOptions{}
	for _, opt := range options {
		opt(opts)
	}

	ifaceProto, err := findInterface(dep, iface)
	if err != nil {
		return nil, err
	}
	dataDependency := ifaceProto.GetData()
	if dataDependency == nil {
		return nil, fmt.Errorf("%w: %q", errNotData, iface)
	}

	if opts.dataAssetsClient == nil {
		client, conn, err := makeDefaultDataAssetsClient()
		if err != nil {
			return nil, fmt.Errorf("failed to create default DataAssets client: %w", err)
		}
		defer conn.Close()
		opts.dataAssetsClient = client
	}

	// Get the DataAsset proto from the DataAssets service.
	da, err := opts.dataAssetsClient.GetDataAsset(ctx, &daspb.GetDataAssetRequest{
		Id: dataDependency.GetId(),
	})
	if err != nil {
		return nil, fmt.Errorf("failed to get DataAsset proto for %q: %w", dataDependency.GetId(), err)
	}

	return da.GetData(), nil
}

func findInterface(dep *rdpb.ResolvedDependency, iface string) (*rdpb.ResolvedDependency_Interface, error) {
	ifaceProto, ok := dep.GetInterfaces()[iface]
	if !ok {
		var explanation string
		if len(dep.GetInterfaces()) == 0 {
			explanation = "no interfaces provided"
		} else {
			keys := slices.Collect(maps.Keys(dep.GetInterfaces()))
			explanation = fmt.Sprintf("got interfaces: %v", strings.Join(keys, ", "))
		}
		return nil, fmt.Errorf("%w: (want %q, %s)", errMissingInterface, iface, explanation)
	}
	return ifaceProto, nil
}

func makeDefaultDataAssetsClient() (dasgrpcpb.DataAssetsClient, *grpc.ClientConn, error) {
	conn, err := grpc.NewClient(ingressAddress, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create gRPC client for DataAssets service: %w", err)
	}
	return dasgrpcpb.NewDataAssetsClient(conn), conn, nil
}

// resolvedDepsIntrospectionOptions configures the ResolvedDependency introspection.
type resolvedDepsIntrospectionOptions struct {
	checkDependencyAnnotation bool
	checkSkillAnnotations     bool
	treatAnyAsTrue            bool
}

// ResolvedDepsIntrospectionOption is an option for configuring [HasResolvedDependency].
type ResolvedDepsIntrospectionOption func(*resolvedDepsIntrospectionOptions)

// WithDependencyAnnotation returns an option that checks for the dependency field annotation.
//
// If this option is not provided, the [HasResolvedDependency] function will return true if any
// ResolvedDependency message is found, regardless of whether it has a dependency field annotation.
func WithDependencyAnnotation() ResolvedDepsIntrospectionOption {
	return func(opts *resolvedDepsIntrospectionOptions) {
		opts.checkDependencyAnnotation = true
	}
}

// WithSkillAnnotations returns an option that checks for Skill specific annotations within
// dependency field annotations.
//
// Use this option to check if the dependency has Skill specific annotations. With this option
// provided, the [HasResolvedDependency] function will return true only if the dependency has Skill
// specific annotations.
func WithSkillAnnotations() ResolvedDepsIntrospectionOption {
	return func(opts *resolvedDepsIntrospectionOptions) {
		opts.checkSkillAnnotations = true
	}
}

// WithTreatAnyAsTrue returns an option that treats Any as true for the purpose of
// checking if a dependency is found.
//
// If this option is not provided, the [HasResolvedDependency] function will treat Any as
// false for the purpose of checking if a dependency is found.
func WithTreatAnyAsTrue() ResolvedDepsIntrospectionOption {
	return func(opts *resolvedDepsIntrospectionOptions) {
		opts.treatAnyAsTrue = true
	}
}

func (r *resolvedDepsIntrospectionOptions) requiresDependencyAnnotationCheck() bool {
	// Skill annotation check can only be performed after the dependency annotation check.
	return r.checkDependencyAnnotation || r.checkSkillAnnotations
}

func isDependencyWithConditionsFound(md protoreflect.MessageDescriptor, r *resolvedDepsIntrospectionOptions) bool {
	resolvedDependencyDescriptor := (&rdpb.ResolvedDependency{}).ProtoReflect().Descriptor()

	if md.FullName() == resolvedDependencyDescriptor.FullName() && !r.requiresDependencyAnnotationCheck() {
		return true
	}

	anyMessageDescriptor := (&anypb.Any{}).ProtoReflect().Descriptor()
	if r.treatAnyAsTrue && md.FullName() == anyMessageDescriptor.FullName() {
		return true
	}

	if r.requiresDependencyAnnotationCheck() {
		for i := 0; i < md.Fields().Len(); i++ {
			field := md.Fields().Get(i)
			if field.Kind() != protoreflect.MessageKind {
				continue
			}
			// Get the message descriptor of the field. If the field is a map, get the message
			// descriptor of the map value.
			var fieldMd protoreflect.MessageDescriptor
			if field.IsMap() {
				if field.MapValue().Kind() != protoreflect.MessageKind {
					continue
				}
				fieldMd = field.MapValue().Message()
			} else {
				fieldMd = field.Message()
			}

			if fieldMd.FullName() != resolvedDependencyDescriptor.FullName() {
				continue
			}
			options := field.Options().(*descriptorpb.FieldOptions)
			fieldMetadata := proto.GetExtension(options, fieldmetadatapb.E_FieldMetadata).(*fieldmetadatapb.FieldMetadata)
			if fieldMetadata.GetDependency() == nil {
				continue
			}
			// At this point, the dependency annotation check is complete. If the Skill annotations check
			// is not required, we can return true.
			if !r.checkSkillAnnotations {
				return true
			}
			return fieldMetadata.GetDependency().GetSkillAnnotations() != nil
		}
	}
	return false
}

// HasResolvedDependency checks if the given proto descriptor has any ResolvedDependency fields.
//
// If additional introspection options are provided, the method returns true only if all of the
// options are satisfied.
func HasResolvedDependency(descriptor protoreflect.MessageDescriptor, options ...ResolvedDepsIntrospectionOption) bool {
	r := &resolvedDepsIntrospectionOptions{}
	for _, opt := range options {
		opt(r)
	}
	var hasDependencies bool
	visited := make(map[protoreflect.MessageDescriptor]struct{})

	walkProtoMessageDescriptors(descriptor, func(md protoreflect.MessageDescriptor) bool {
		if isDependencyWithConditionsFound(md, r) {
			hasDependencies = true
			// Stop the recursion if we already found a dependency.
			return false
		}
		return true
	}, visited)
	return hasDependencies
}



// FallbackManifestKeys returns a slice of strings containing all of the
// fallback_manifest_dependency_key values in the descriptor.
func FallbackManifestKeys(descriptor protoreflect.MessageDescriptor) []string {
	var keys []string
	visited := make(map[protoreflect.MessageDescriptor]struct{})

	walkProtoMessageDescriptors(descriptor, func(md protoreflect.MessageDescriptor) bool {
		for i := 0; i < md.Fields().Len(); i++ {
			field := md.Fields().Get(i)
			options := field.Options().(*descriptorpb.FieldOptions)
			if !proto.HasExtension(options, fieldmetadatapb.E_FieldMetadata) {
				continue
			}
			fieldMetadata := proto.GetExtension(options, fieldmetadatapb.E_FieldMetadata).(*fieldmetadatapb.FieldMetadata)
			if fieldMetadata.GetDependency() == nil {
				continue
			}
			dep := fieldMetadata.GetDependency()
			if skillAnnos := dep.GetSkillAnnotations(); skillAnnos != nil {
				if key := skillAnnos.GetFallbackManifestDependencyKey(); key != "" {
					keys = append(keys, key)
				}
			}
		}
		return true
	}, visited)

	return keys
}



// LINT.ThenChange(
//  //intrinsic_sdk/intrinsic/assets/dependencies/utils.py:utils,
//  //intrinsic_sdk/intrinsic/assets/dependencies/utils.h:utils
// )

// walkProtoMessageDescriptors walks through a proto message descriptor, executing a function for
// each message descriptor it finds.
//
// The function returns whether to enter into the message descriptor recursively.
func walkProtoMessageDescriptors(md protoreflect.MessageDescriptor, f func(protoreflect.MessageDescriptor) bool, visited map[protoreflect.MessageDescriptor]struct{}) {
	visited[md] = struct{}{}
	shouldEnter := f(md)
	if !shouldEnter {
		return
	}

	for i := 0; i < md.Fields().Len(); i++ {
		field := md.Fields().Get(i)

		// Skip non-message/group types.
		if field.Kind() != protoreflect.MessageKind && field.Kind() != protoreflect.GroupKind {
			continue
		}
		// Skip already visited messages.
		if _, ok := visited[field.Message()]; ok {
			continue
		}

		if field.IsMap() { // Walk through value descriptors.
			md := field.MapValue().Message()
			if md == nil {
				continue
			}
			if _, ok := visited[md]; ok {
				continue
			}
			walkProtoMessageDescriptors(md, f, visited)
		} else {
			walkProtoMessageDescriptors(field.Message(), f, visited)
		}
	}
}
