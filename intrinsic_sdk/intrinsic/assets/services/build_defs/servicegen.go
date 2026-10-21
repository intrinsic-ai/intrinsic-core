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

// Package servicegen implements creation of a Service Asset bundle.
package servicegen

import (
	"context"
	"fmt"
	"strings"

	"intrinsic/assets/services/servicebundle"
	"intrinsic/assets/services/servicefix"
	"intrinsic/util/proto/protoio"
	"intrinsic/util/proto/registryutil"
	"intrinsic/util/proto/sourcecodeinfoview"

	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"

	dpb "google.golang.org/protobuf/types/descriptorpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

// CreateServiceBundleOptions provides the data needed to create a Service Asset bundle.
type CreateServiceBundleOptions struct {
	DefaultConfigPath      string
	FileDescriptorSetPaths []string
	ImageTarPaths          []string
	ManifestPath           string
	OutputBundlePath       string
}

func pruneSourceCodeInfo(configMessageFullName string, defaultConfig *anypb.Any, fds *dpb.FileDescriptorSet) error {
	if fds == nil {
		return nil
	}

	var fullNames []string
	if defaultConfig != nil {
		typeURLParts := strings.Split(defaultConfig.GetTypeUrl(), "/")
		if len(typeURLParts) < 1 {
			return fmt.Errorf("failed to extract default proto name from type URL: %v", defaultConfig.GetTypeUrl())
		}
		fullNames = append(fullNames, typeURLParts[len(typeURLParts)-1])
	} else if configMessageFullName != "" {
		// No default config is provided, but the manifest declares a config message type.
		fullNames = append(fullNames, configMessageFullName)
	}

	// Note that if neither a default config nor a config message name is provided, all source code
	// info fields will be stripped out.
	return sourcecodeinfoview.PruneSourceCodeInfo(fds,
		sourcecodeinfoview.WithExcludeNames(fullNames),
	)
}

// CreateServiceBundle creates a Service Asset bundle on disk.
func CreateServiceBundle(ctx context.Context, opts *CreateServiceBundleOptions) error {
	m := &smpb.ServiceManifest{}
	if err := protoio.ReadTextProto(opts.ManifestPath, m); err != nil {
		return fmt.Errorf("failed to read manifest: %w", err)
	}

	fds, err := registryutil.LoadFileDescriptorSets(opts.FileDescriptorSetPaths)
	if err != nil {
		return fmt.Errorf("unable to build FileDescriptorSet: %w", err)
	}
	types, err := registryutil.NewTypesFromFileDescriptorSet(fds)
	if err != nil {
		return fmt.Errorf("failed to populate the registry: %w", err)
	}

	var defaultConfig *anypb.Any
	if opts.DefaultConfigPath != "" {
		defaultConfig = &anypb.Any{}
		if err := protoio.ReadTextProto(opts.DefaultConfigPath, defaultConfig, protoio.WithResolver(types)); err != nil {
			return fmt.Errorf("failed to read default config proto: %w", err)
		}
	}

	if err := pruneSourceCodeInfo(m.GetServiceDef().GetConfigMessageFullName(), defaultConfig, fds); err != nil {
		return fmt.Errorf("failed to prune source code info: %w", err)
	}

	if err := servicefix.Manifest(m, servicefix.WithPopulateOldFields(true)); err != nil {
		return fmt.Errorf("unable to make manifest compatible with the latest version of the platform: %v", err)
	}
	if err := servicebundle.WriteFile(ctx, m, opts.OutputBundlePath,
		servicebundle.WithFileDescriptorSet(fds),
		servicebundle.WithDefaultConfig(defaultConfig),
		servicebundle.WithImageTarPaths(opts.ImageTarPaths),
	); err != nil {
		return fmt.Errorf("failed to write Service Asset bundle: %w", err)
	}

	return nil
}
