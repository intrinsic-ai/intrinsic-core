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

// Package servicefix contains utils that adapt Services to meet the requirements of the latest
// platform version.
package servicefix

import (
	"fmt" 
	"slices"

	"intrinsic/util/proto/descriptorcompatibility" 

	smpb "intrinsic/assets/services/proto/service_manifest_go_proto"
	drpb "intrinsic/assets/services/proto/v1/dynamic_reconfiguration_go_proto"
	sspb "intrinsic/assets/services/proto/v1/service_state_go_proto"
)

// fixOpts contains options for fixing a manifest.
type fixOpts struct {
	populateOldFields   bool
	clearObsoleteFields bool
}

// FixOption is an option for fixing a manifest.
type FixOption func(*fixOpts)

// WithPopulateOldFields specifies whether to backfill old deprecated fields if empty.
func WithPopulateOldFields(populate bool) FixOption {
	return func(opts *fixOpts) {
		opts.populateOldFields = populate
	}
}

// WithClearObsoleteFields specifies whether to clear obsolete manifest fields. A field is
// considered obsolete if the platform no longer uses it.
func WithClearObsoleteFields(clear bool) FixOption {
	return func(opts *fixOpts) {
		opts.clearObsoleteFields = clear
	}
}

// Manifest updates a ServiceManifest to meet the requirements of the latest platform version.
func Manifest(manifest *smpb.ServiceManifest, options ...FixOption) error {
	opts := &fixOpts{}
	for _, opt := range options {
		opt(opts)
	}
	if manifest == nil {
		return nil
	}
	backfillServiceDef(manifest.GetServiceDef(), opts)
	return nil
}

// ProcessedManifest updates a ProcessedServiceManifest to meet the requirements of the latest
// platform version.
func ProcessedManifest(manifest *smpb.ProcessedServiceManifest, options ...FixOption) error {
	opts := &fixOpts{}
	for _, opt := range options {
		opt(opts)
	}
	if manifest == nil {
		return nil
	}
	backfillServiceDef(manifest.GetServiceDef(), opts)

	if err := descriptorcompatibility.Reconcile(manifest.GetAssets().GetFileDescriptorSet()); err != nil {
		return fmt.Errorf("unable to reconcile the file descriptor set for Service Asset: %w", err)
	}

	return nil
}

func backfillServiceDef(sd *smpb.ServiceDef, opts *fixOpts) {
	if sd == nil {
		return
	}

	// Populate the dynamic reconfiguration platform gRPC interface if only the deprecated boolean
	// setting is present and true.
	if conf := sd.GetDynamicReconfigurationConfig(); conf == nil && sd.GetSupportsDynamicReconfiguration() {
		sd.DynamicReconfigurationConfig = &drpb.DynamicReconfigurationConfig{
			ServiceVersions: []drpb.DynamicReconfigurationConfig_ServiceVersion{
				drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION,
			},
		}
	}

	// Populate the service state platform gRPC interface if only the deprecated boolean setting is
	// present and true.
	if conf := sd.GetServiceStateConfig(); conf == nil && sd.GetSupportsServiceState() {
		sd.ServiceStateConfig = &sspb.ServiceStateConfig{
			ServiceVersions: []sspb.ServiceStateConfig_ServiceVersion{
				sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE,
			},
		}
	}

	if opts.populateOldFields {
		// Backfill the deprecated SupportsDynamicReconfiguration field if the new config is present.
		if conf := sd.GetDynamicReconfigurationConfig(); conf != nil {
			if slices.Contains(conf.GetServiceVersions(), drpb.DynamicReconfigurationConfig_INTRINSIC_PROTO_SERVICES_V1_DYNAMIC_RECONFIGURATION) {
				sd.SupportsDynamicReconfiguration = true
			}
		}

		// Backfill the deprecated SupportsServiceState field if the new config is present.
		if conf := sd.GetServiceStateConfig(); conf != nil {
			if slices.Contains(conf.GetServiceVersions(), sspb.ServiceStateConfig_INTRINSIC_PROTO_SERVICES_V1_SERVICE_STATE) {
				sd.SupportsServiceState = true
			}
		}
	}

	if opts.clearObsoleteFields {
		sd.SupportsDynamicReconfiguration = false
		sd.SupportsServiceState = false
	}

}
