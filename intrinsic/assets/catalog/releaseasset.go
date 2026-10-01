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

// Package releaseasset provides utils for releasing Assets to the AssetCatalog.
package releaseasset

import (
	"context"
	"fmt"
	"io"

	"intrinsic/assets/bundle"
	"intrinsic/assets/idutils"
	"intrinsic/assets/imagetransfer"
	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/services/bundleimages"
	"intrinsic/assets/throttle"
	"intrinsic/storage/content_addressable_storage/pkg/filetocas" 
	"intrinsic/tools/inctl/util/casgeometryuploader"              

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	acpb "intrinsic/assets/catalog/proto/v1/asset_catalog_go_proto"
	rmpb "intrinsic/assets/catalog/proto/v1/release_metadata_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"
	caspb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto" 

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// Printer is a function that prints a formatted status message about the release.
type Printer func(format string, a ...any)

type fromBundleOptions struct {
	aaClient           assetartifactspb.AssetArtifactsClient
	acClient           acpb.AssetCatalogClient
	casClient          caspb.ContentAddressableStorageServiceClient 
	concurrencyLimiter *throttle.ConcurrencyLimiter
	dryRun             bool
	flagDefault        bool
	flagOrgPrivate     bool
	ignoreExisting     bool
	imageTransferer    imagetransfer.Transferer
	lroClient          lropb.OperationsClient
	printer            Printer
	progressWriter     io.Writer
	releaseNotes       string
	version            string
}

// FromBundleOption is an option for FromBundle.
type FromBundleOption func(*fromBundleOptions)

// WithAssetArtifactsClient specifies the AssetArtifactsClient to use.
func WithAssetArtifactsClient(client assetartifactspb.AssetArtifactsClient) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.aaClient = client
	}
}

// WithAssetCatalogClient specifies the client to use for catalog requests.
func WithAssetCatalogClient(acc acpb.AssetCatalogClient) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.acClient = acc
	}
}



// WithCASClient specifies the client to use for CAS requests.
func WithCASClient(casc caspb.ContentAddressableStorageServiceClient) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.casClient = casc
	}
}



// WithConnection specifies the connection to use for all gRPC clients.
func WithConnection(conn grpc.ClientConnInterface) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.aaClient = assetartifactspb.NewAssetArtifactsClient(conn)
		opts.acClient = acpb.NewAssetCatalogClient(conn)
		opts.casClient = caspb.NewContentAddressableStorageServiceClient(conn) 
		opts.lroClient = lropb.NewOperationsClient(conn)
	}
}

// WithDryRun specifies whether to do a dry run of the release.
func WithDryRun(dryRun bool) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.dryRun = dryRun
	}
}

// WithFlagDefault specifies whether the released Asset should be the default version.
func WithFlagDefault(flagDefault bool) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.flagDefault = flagDefault
	}
}

// WithFlagOrgPrivate specifies whether the Asset should be org-private.
func WithFlagOrgPrivate(flagOrgPrivate bool) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.flagOrgPrivate = flagOrgPrivate
	}
}

// WithIgnoreExisting specifies whether to ignore errors if the Asset version already exists in the
// catalog.
func WithIgnoreExisting(ignoreExisting bool) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.ignoreExisting = ignoreExisting
	}
}

// WithImageTransferer specifies the image transferer to use.
func WithImageTransferer(transferer imagetransfer.Transferer) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.imageTransferer = transferer
	}
}

// WithOperationsClient specifies the OperationsClient to use.
func WithOperationsClient(client lropb.OperationsClient) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.lroClient = client
	}
}

func WithPrinter(printer Printer) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.printer = printer
	}
}

// WithProgressWriter specifies a writer to output progress updates during Asset release.
func WithProgressWriter(w io.Writer) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.progressWriter = w
	}
}

// WithReleaseNotes specifies release notes to include with the Asset.
func WithReleaseNotes(releaseNotes string) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.releaseNotes = releaseNotes
	}
}

// WithVersion specifies the version of the Asset.
func WithVersion(version string) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.version = version
	}
}

// WithProcessingConcurrencyLimiter specifies the ConcurrencyLimiter to use to limit the concurrency
// of Asset processing.
func WithProcessingConcurrencyLimiter(limiter *throttle.ConcurrencyLimiter) FromBundleOption {
	return func(opts *fromBundleOptions) {
		opts.concurrencyLimiter = limiter
	}
}

func FromBundle(ctx context.Context, path string, options ...FromBundleOption) error {
	opts := &fromBundleOptions{
		concurrencyLimiter: throttle.NewConcurrencyLimiter(1),
		printer:            nullPrinter,
	}
	for _, opt := range options {
		opt(opts)
	}

	if !opts.dryRun && opts.acClient == nil {
		return fmt.Errorf("acClient must not be nil in non-dry-run mode")
	}
	if opts.imageTransferer == nil {
		return fmt.Errorf("transferer must not be nil")
	}
	if opts.version == "" {
		return fmt.Errorf("version must not be empty")
	}


	if opts.casClient == nil {
		return fmt.Errorf("casClient must not be nil")
	}
	geometryUploader, err := casgeometryuploader.New(filetocas.NewUploader(
		opts.casClient,
		filetocas.DryRun(opts.dryRun),
	), 1) // One worker to avoid overwhelming CAS during release.
	if err != nil {
		return fmt.Errorf("failed to create CAS geometry uploader: %w", err)
	}


	rdProcessor := referenceddata.NewProcessor(
		opts.aaClient,
		opts.lroClient,
		referenceddata.WithDryRun(opts.dryRun),
		referenceddata.WithFallbackCatalogClient(opts.acClient),
		referenceddata.WithProgressWriter(opts.progressWriter),
	)

	processor := bundle.Processor{
		ImageProcessor:          bundleimages.CreateImageProcessor(opts.imageTransferer),
		ReferencedDataProcessor: rdProcessor,
		GZFProcessor: gzfprocessor.New(
			rdProcessor,
			gzfprocessor.WithLegacyUploader(geometryUploader), 
			gzfprocessor.WithConcurrencyLimiter(opts.concurrencyLimiter),
		),
	}

	processedBundle, err := processor.ProcessFile(ctx, path)
	if err != nil {
		return fmt.Errorf("unable to process Asset: %w", err)
	}

	asset := processedBundle.Release(bundle.VersionDetails{
		Version:      opts.version,
		ReleaseNotes: opts.releaseNotes,
		ReleaseMetadata: &rmpb.ReleaseMetadata{
			Default:    opts.flagDefault,
			OrgPrivate: opts.flagOrgPrivate,
		},
	})

	idVersion := idutils.IDVersionFromProtoUnchecked(asset.GetMetadata().GetIdVersion())
	opts.printer("Releasing Asset %q to the catalog", idVersion)

	if opts.dryRun {
		opts.printer("Skipping release: dry-run")
		return nil
	}

	if _, err := opts.acClient.CreateAsset(ctx, &acpb.CreateAssetRequest{
		Asset: asset,
	}); err != nil {
		if s, ok := status.FromError(err); ok && opts.ignoreExisting && s.Code() == codes.AlreadyExists {
			opts.printer("Skipping release: Asset already exists in the catalog")
			return nil
		}
		return fmt.Errorf("could not release the Asset: %w", err)
	}

	opts.printer("Finished releasing the Asset")

	return nil
}

func nullPrinter(format string, a ...any) {
}
