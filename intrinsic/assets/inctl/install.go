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

// Package install defines the command to install an asset.
package install

import (
	"context"
	"fmt"
	"log"
	"os"
	"regexp"

	"golang.org/x/sync/errgroup"

	"intrinsic/assets/bundle"
	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/assets/imagetransfer"
	"intrinsic/assets/platformlevelswitch"
	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/services/bundleimages"
	"intrinsic/assets/throttle"
	"intrinsic/skills/tools/skill/cmd/directupload/directupload"
	"intrinsic/storage/content_addressable_storage/pkg/filetocas" 
	"intrinsic/tools/inctl/util/casgeometryuploader"              
	"intrinsic/tools/inctl/util/color"
	"intrinsic/util/status/extstatus"

	"github.com/spf13/cobra"
	"google.golang.org/grpc/status"

	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"
	rpb "intrinsic/assets/proto/v1/reference_go_proto"
	casgrpcpb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto" 

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
)

// solutionAssetRegex matches strings of the form <branch_id>/<asset_id> that indicate an Asset in
// a specific Solution branch.
//
// Further validation is needed on both parts to ensure validity.
var solutionAssetRegex = regexp.MustCompile(`^(?P<branch>[A-Za-z0-9_\-]+)/(?P<asset>[a-z0-9_\.]+)$`)


const (
	// numGeoUploadWorkers enables parallelization of legacy CAS geometry uploads while processing the Asset.
	numGeoUploadWorkers = 8
)



// GetCommand returns a command to install an asset.
func GetCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "install <asset>...",
		Short: "Install one or more Assets",
		Example: `
  Install a local Asset bundle into the specified Solution:
  $ inctl asset install abc/bundle.tar \
      --org $my_org \
      --solution $my_solution_id

  Install an Asset from the catalog into the specified Solution:
  $ inctl asset install ai.intrinsic.calculator_service.0.20260126.0-RC00 \
      --org $my_org \
      --solution $my_solution_id

  Install an Asset from another Solution into the specified Solution:
  $ inctl asset install $source_solution_id/ai.intrinsic.calculator_service \
      --org $my_org \
      --solution $my_solution_id

  Install multiple Assets into the specified Solution:
  $ inctl asset install abc/bundle.tar ai.intrinsic.calculator_service.0.20260126.0-RC00 \
      --org $my_org \
      --solution $my_solution_id

  To find a running Solution's id, run:
  $ inctl solution list --org $my_org --filter "running_on_hw,running_in_sim" --output json

  The Asset can also be installed by specifying the cluster on which the Solution is running:
  $ inctl asset install $my_asset --org $my_org --cluster $my_cluster
`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := cmd.Context()
			targets := args

			policy, err := flags.GetFlagPolicy()
			if err != nil {
				return err
			}

			ctx, err = platformlevelswitch.AppendOrgToOutgoingContext(ctx, flags.GetFlagOrganization())
			if err != nil {
				return fmt.Errorf("failed to add org information to context: %w", err)
			}
			ctx, rawConn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer rawConn.Close()

			conn := throttle.ConnectionWithRateLimit(rawConn, flags.GetFlagRateLimit(), flags.GetFlagRateBurst())

			// Determine the image transferer to use. Default to direct injection into the cluster.
			var transfer imagetransfer.Transferer
			if registry := flags.GetFlagRegistry(); registry != "" {
				user, pwd := flags.GetFlagsRegistryAuthUserPassword()
				transfer = imagetransfer.RemoteTransferer(registry, user, pwd)
			}
			if !flags.GetFlagSkipDirectUpload() {
				transfer = directupload.NewTransferer(
					directupload.WithDiscovery(directupload.NewFromConnection(conn)),
					directupload.WithOutput(cmd.OutOrStdout()),
					directupload.WithFailOver(transfer),
				)
			}
			if transfer == nil {
				return fmt.Errorf("--registry must be specified if --skip-direct-upload is used")
			}

			uploader, err := casgeometryuploader.New(filetocas.NewUploader(casgrpcpb.NewContentAddressableStorageServiceClient(conn)), numGeoUploadWorkers)
			if err != nil {
				return fmt.Errorf("failed to create a CAS geometry uploader: %w", err)
			}


			client := iagrpcpb.NewInstalledAssetsClient(conn)

			rdProcessor := referenceddata.NewProcessor(
				assetartifactspb.NewAssetArtifactsClient(conn),
				lropb.NewOperationsClient(conn),
				referenceddata.WithProgressWriter(cmd.OutOrStdout()),
			)
			processor := &bundle.Processor{
				ImageProcessor:          bundleimages.CreateImageProcessor(transfer),
				ReferencedDataProcessor: rdProcessor,
				GZFProcessor: gzfprocessor.New(
					rdProcessor,
					gzfprocessor.WithLegacyUploader(uploader), 
					gzfprocessor.WithConcurrencyLimiter(throttle.NewConcurrencyLimiter(flags.GetFlagProcessingConcurrency())),
				),
			}

			// Prepare all assets client-side (parsing catalog refs, solution refs,
			// or processing local bundle files) into proto messages.
			assets := make([]*iapb.CreateInstalledAssetsRequest_Asset, len(targets))
			g, gCtx := errgroup.WithContext(ctx)
			g.SetLimit(15)
			for i, target := range targets {
				g.Go(func() error {
					asset, err := assetFromTarget(gCtx, target, processor.ProcessFile)
					if err != nil {
						return err
					}
					assets[i] = asset
					return nil
				})
			}
			if err := g.Wait(); err != nil {
				return err
			}

			// Call the batch CreateInstalledAssets API once with all assets together.
			op, err := client.CreateInstalledAssets(ctx, &iapb.CreateInstalledAssetsRequest{
				Policy: policy,
				Assets: assets,
			})
			if err != nil {
				return fmt.Errorf("could not install the assets: %w", err)
			}

			log.Printf("Awaiting completion of the installation")
			lroClient := lrogrpcpb.NewOperationsClient(conn)
			for !op.GetDone() {
				op, err = lroClient.WaitOperation(ctx, &lropb.WaitOperationRequest{
					Name: op.GetName(),
				})
				if err != nil {
					return fmt.Errorf("unable to check status of installation: %v", err)
				}
			}

			if err := status.ErrorProto(op.GetError()); err != nil {
				return fmt.Errorf("installation failed: %w", err)
			}
			installed := &iapb.CreateInstalledAssetsResponse{}
			if err := op.GetResponse().UnmarshalTo(installed); err != nil {
				return fmt.Errorf("unable to parse result from successful installation: %w", err)
			}
			for _, item := range installed.GetInstalledAssets() {
				idv := idutils.IDVersionFromProtoUnchecked(item.GetMetadata().GetIdVersion())
				log.Printf("Finished installing %q", idv)
			}
			if op.GetMetadata() != nil {
				metadata := &iapb.CreateInstalledAssetsMetadata{}
				if err := op.GetMetadata().UnmarshalTo(metadata); err != nil {
					log.Printf("failed to check for warnings: failed to unmarshal operation metadata: %v", err)
				} else if metadata.GetWarnings() != nil {
					ext := extstatus.FromProto(metadata.GetWarnings())
					color.C.Yellow().Printf("\nWARNING: Installation succeeded with warnings:\n%v\n", ext)
				}
			}
			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagPolicy("asset")
	flags.AddFlagsProjectOrg()
	flags.AddFlagsRateLimit(throttle.OnPremRateLimit, throttle.OnPremBurst)
	flags.AddFlagProcessingConcurrency(throttle.LocalProcessingConcurrency)
	flags.AddFlagRegistry()
	flags.AddFlagsRegistryAuthUserPassword()
	flags.AddFlagSkipDirectUpload("asset")

	return cmd
}

type processBundle func(ctx context.Context, path string) (bundle.ProcessedBundle, error)

func assetFromTarget(ctx context.Context, target string, process processBundle) (*iapb.CreateInstalledAssetsRequest_Asset, error) {
	fileExists := false
	if _, err := os.Stat(target); err == nil {
		fileExists = true
	}

	idvParts, err := idutils.NewIDVersionParts(target)
	isIDVersion := err == nil

	solutionAssetMatches := solutionAssetRegex.FindStringSubmatch(target)
	isSolutionAsset := false
	if solutionAssetMatches != nil {
		isSolutionAsset = idutils.IsID(solutionAssetMatches[solutionAssetRegex.SubexpIndex("asset")])
	}

	if (isIDVersion || isSolutionAsset) && fileExists {
		return nil, fmt.Errorf("input is ambiguous; %q is both a file and an id_version or Solution Asset", target)
	}

	if isIDVersion {
		return &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_Catalog{
				Catalog: idvParts.IDVersionProto(),
			},
		}, nil
	}

	if isSolutionAsset {
		return &iapb.CreateInstalledAssetsRequest_Asset{
			Variant: &iapb.CreateInstalledAssetsRequest_Asset_SolutionAsset{
				SolutionAsset: &rpb.SolutionAsset{
					BranchId: solutionAssetMatches[solutionAssetRegex.SubexpIndex("branch")],
					Name:     solutionAssetMatches[solutionAssetRegex.SubexpIndex("asset")],
				},
			},
		}, nil
	}

	if fileExists {
		processedBundle, err := process(ctx, target)
		if err != nil {
			return nil, fmt.Errorf("unable to process file: %w", err)
		}
		return processedBundle.InstallBatch(), nil
	}

	return nil, fmt.Errorf("%q is not a file, id_version, or Solution Asset; check that the input is formatted correctly", target)
}
