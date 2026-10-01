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

// solution_update updates intrinsic_solution targets.
package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"time"

	"intrinsic/assets/build_defs/assetprocessor"
	"intrinsic/assets/build_defs/cascopyprocessor"
	"intrinsic/assets/bundle"
	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/imagetransfer"
	"intrinsic/assets/platformlevelswitch"
	"intrinsic/assets/referenceddata"
	"intrinsic/assets/scene_objects/gzfprocessor"
	"intrinsic/assets/services/bundleimages"
	"intrinsic/assets/throttle"
	"intrinsic/config/operationmode"
	intrinsicinit "intrinsic/production/intrinsic"
	"intrinsic/skills/tools/skill/cmd/directupload/directupload"
	"intrinsic/storage/content_addressable_storage/pkg/filetocas"
	"intrinsic/tools/inctl/cmd/apputil"
	"intrinsic/tools/inctl/util/casgeometryuploader"
	"intrinsic/tools/inctl/util/introspection"
	"intrinsic/tools/inctl/util/releaseutil"
	"intrinsic/tools/inctl/util/traceutil"
	"intrinsic/util/go/build"
	"intrinsic/util/path_resolver/pathresolver"

	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	"github.com/bazelbuild/rules_go/go/runfiles"
	backoff "github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"

	assetpb "intrinsic/assets/build_defs/asset_go_proto"
	assetartifactspb "intrinsic/assets/proto/v1/asset_artifacts_go_proto"
	apb "intrinsic/config/proto/application_go_proto"
	commonpb "intrinsic/config/proto/common_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	solutionservicegrpcpb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	solutionservicepb "intrinsic/frontend/solution_service/proto/solution_service_go_proto"
	solutionstatuspb "intrinsic/frontend/solution_service/proto/status_go_proto"
	deploygrpcpb "intrinsic/kubernetes/workcell_spec/proto/deploy_go_proto"
	dpb "intrinsic/kubernetes/workcell_spec/proto/deploy_go_proto"
	casgrpcpb "intrinsic/storage/content_addressable_storage/proto/cas_service_go_proto"
)

func readLocalSolution(path string) (*assetpb.LocalSolution, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read solution assets proto %q: %w", path, err)
	}
	assets := new(assetpb.LocalSolution)
	if err := proto.Unmarshal(b, assets); err != nil {
		return nil, fmt.Errorf("failed to parse solution assets proto %q: %w", path, err)
	}
	return assets, nil
}

// deployApplication deploys the given application using the provided client with retry logic.
func deployApplication(ctx context.Context, client deploygrpcpb.DeployServiceClient, app *apb.Application) error {
	ctx, span := trace.StartSpan(ctx, "inctl.app_start.deployApplication")
	defer span.End()

	deployRetries := 5
	eb := backoff.NewExponentialBackOff()
	eb.InitialInterval = 2 * time.Second
	eb.RandomizationFactor = 0.5
	deployBackoff := backoff.WithContext(backoff.WithMaxRetries(eb, uint64(deployRetries)), ctx)
	err := backoff.Retry(func() error {
		if _, err := client.DeployApplication(ctx, &dpb.DeployApplicationRequest{
			Application: app,
		}); err != nil {
			// Retry to be resilient against transient infrastructure failures
			if status.Code(err) == codes.Unavailable {
				log.WarningContextf(ctx, "Retrying DeployApplication due to transient error: %v", err)
				return err // Retry
			}
			return backoff.Permanent(err)
		}
		return nil // Success
	}, deployBackoff)
	return err
}

// clearPrintedLines clears numLines of output to stdout. This assumes
// that the current line is empty, i.e. that the last printed char was a '\n'.
func clearPrintedLines(w io.Writer, numLines int) {
	// Attempting to clear 0 lines is not a no-op but will clear 1 line.
	if numLines <= 0 {
		return
	}
	fmt.Fprintf(w, "\x1b[%dF", numLines) // Move cursor up numLines.
	fmt.Fprint(w, "\x1b[J")              // Clear from cursor until end of screen.
}

// waitForReady waits until the solution service reports ready based on various health signals.
func waitForReady(ctx context.Context, client solutionservicegrpcpb.SolutionServiceClient, updates io.Writer) error {
	retries := 0
	maxRetries := 7 * 60 // seconds
	ctx, spanAppStatus := trace.StartSpan(ctx, "inctl.app_start.waitForReady")
	defer spanAppStatus.End()

	backoffStrategy := backoff.WithContext(backoff.WithMaxRetries(backoff.NewConstantBackOff(1*time.Second), uint64(maxRetries)), ctx)
	// Print two lines since they will be removed by the retry loop.
	fmt.Fprintln(updates)
	fmt.Fprintln(updates)
	return backoff.Retry(func() error {
		retries++
		response, err := client.GetStatus(ctx, &solutionservicepb.GetStatusRequest{})
		if err != nil {
			code := status.Code(err)
			// If inctl just installed transfer service it might not be available yet.
			if code == codes.Unimplemented || code == codes.Unavailable {
				return err
			}
			return backoff.Permanent(err)
		}

		switch response.State {
		case solutionstatuspb.Status_PLATFORM_DEPLOYING:
			// Clear out the past line and print the new status.
			clearPrintedLines(updates, 2)
			fmt.Fprintf(updates, "Waiting for platform to report ready, try %v/%v.\n%v\n", retries, maxRetries, response.GetStateReason())
			err = status.Error(codes.Unavailable, response.GetStateReason())
		case solutionstatuspb.Status_DEPLOYING:
			// Clear out the past line and print the new status.
			clearPrintedLines(updates, 2)
			fmt.Fprintf(updates, "Waiting for solution to report ready, try %v/%v.\n%v\n", retries, maxRetries, response.GetStateReason())
			err = status.Error(codes.Unavailable, response.GetStateReason())
		case solutionstatuspb.Status_ERROR:
			err = errors.New(response.GetStateReason())
		case solutionstatuspb.Status_READY:
			clearPrintedLines(updates, 2)
		default:
			err = fmt.Errorf("unexpected status %s from solution service", response.GetState().String())
		}
		return err
	}, backoffStrategy)
}

func updateCmd() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	var flagOperationMode string
	cmd := &cobra.Command{
		Use:   "solution_update [app name]",
		Short: "Deploy an app",
		Long: `Deploy an app to a cluster.

This takes an application defined by intrinsic_solution, uploads all of the
relevant artifacts, and then calls the deploy service to put the cluster into a
defined state.

The cluster can be targeted in two modes.  The first is over the api-relay.
This can be specified using --cluster, --address, or --solution.  The second is
using port-forwarding or SSH by specifying --context.  The latter mode allows
for updating the runtime (a.k.a. intrinsic-base) within the cluster.  This is
done by default and matches the version of intrinsic-base with which inctl was
built.

The solutions started by this command are ephemeral.  As a consequence, any
modifications made to the solution will only live for the duration of the
solution.  Any changes that need to be persisted must be translated back into
the intrinsic_solution target (and its dependencies).  This is, in effect, a
completely separate pathway when compared to solutions launched from Flowstate.
If you want to start a specific solution from Flowstate on a specific cluster,
look at inctl solution start.  If you want to convert a solution started by
this command into one that can persist changes and can be started from the
fronted, then use inctl app save-as.
`,
		Example: `
Start a solution using the org and cluster:
$ bazel run //intrinsic/config:empty_application --\
		--org my_org \
		--cluster my_cluster_id

Start a solution using the $INTRINSIC_CLUSTER and $INTRINSIC_ORG variables:
$ bazel run //intrinsic/config:empty_application

Overwrite a currently running solution:
$ bazel run //intrinsic/config:empty_application --\
		--org my_org \
		--solution my_solution_id

Start a solution on a cluster on the local network:
$ bazel run //intrinsic/config:empty_application --\
		--address 192.168.0.100:17080
`,
		// Do not print usage when a command exits with an error.
		SilenceUsage: true,
		Args:         cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx, span := trace.StartSpan(cmd.Context(), "inctl app start")
			span.AddAttributes(trace.BoolAttribute("completed", false))
			span.AddAttributes(trace.StringAttribute("build_label", build.Label()))
			span.AddAttributes(trace.StringAttribute("operation_mode", flagOperationMode))
			defer span.End()

			ctx, err := platformlevelswitch.AppendOrgToOutgoingContext(ctx, flags.GetFlagOrganization())
			if err != nil {
				return errors.Wrap(err, "unable to setup the context")
			}
			project := "giza-workcells"

			log.InfoContextf(ctx, "Deploying to cluster")

			assetsPath := args[0]
			span.AddAttributes(trace.StringAttribute("app_path", assetsPath))
			runfilesFS, err := pathresolver.ResolveRunfilesFsRoot()
			if err != nil {
				return fmt.Errorf("failed to resolve runfiles FS root: %w", err)
			}
			assets, err := readLocalSolution(assetsPath)
			if err != nil {
				return err
			}

			ctx, rawIngressConn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return errors.Wrap(err, "cannot connect to cluster")
			}
			defer rawIngressConn.Close()

			ingressConn := throttle.ConnectionWithRateLimit(rawIngressConn, flags.GetFlagRateLimit(), flags.GetFlagRateBurst())

			casClient := casgrpcpb.NewContentAddressableStorageServiceClient(ingressConn)
			f2c := filetocas.NewUploader(
				casClient,
				filetocas.WithRunfilesFS(runfilesFS),
				filetocas.UseChunkSize(releaseutil.CASUploadChunkSize),
			)
			processingLimiter := throttle.NewConcurrencyLimiter(flags.GetFlagProcessingConcurrency())
			gu, err := casgeometryuploader.New(f2c, 8)
			if err != nil {
				return fmt.Errorf("failed to create a CAS geometry uploader: %w", err)
			}

			// Determine the image transferer to use. Default to direct injection into the cluster.
			var transfer imagetransfer.Transferer
			if registry := flags.GetFlagRegistry(); registry != "" {
				user, pwd := flags.GetFlagsRegistryAuthUserPassword()
				transfer = imagetransfer.RemoteTransferer(registry, user, pwd)
			}
			if !flags.GetFlagSkipDirectUpload() {
				transfer = directupload.NewTransferer(
					directupload.WithDiscovery(directupload.NewFromConnection(ingressConn)),
					directupload.WithOutput(cmd.OutOrStdout()),
					directupload.WithFailOver(transfer),
				)
			}
			if transfer == nil {
				return fmt.Errorf("--registry must be specified if --skip-direct-upload is used")
			}

			// Process the application's assets as defined in the LocalSolution proto
			// and populate the Assets and Instances maps in the Application proto.
			startProcessAssets := time.Now()
			baseRDProcessor := referenceddata.NewProcessor(
				assetartifactspb.NewAssetArtifactsClient(ingressConn),
				lropb.NewOperationsClient(ingressConn),
			)
			rdProcessor := cascopyprocessor.New(baseRDProcessor, project, casClient)
			defer rdProcessor.Close()
			proc := &assetprocessor.Processor{
				Processor: bundle.Processor{
					ImageProcessor: bundleimages.CreateImageProcessor(transfer),
					GZFProcessor: gzfprocessor.New(
						rdProcessor,
						gzfprocessor.WithLegacyUploader(gu),
						gzfprocessor.WithConcurrencyLimiter(processingLimiter),
					),
					ReferencedDataProcessor: rdProcessor,
				},
				PathResolver: func(path string) (string, error) {
					r, err := runfiles.New()
					if err != nil {
						return "", err
					}
					return r.Rlocation(path)
				},
				ConcurrencyLimiter: processingLimiter,
			}
			app, err := proc.Process(ctx, assets)
			if err != nil {
				return err
			}
			if mode := operationmode.FromString(flagOperationMode); mode != opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED {
				app.OperationMode = mode
			}
			app.GetMetadata().LastUpdatedBy = apputil.CurrentUsername()
			app.GetMetadata().Category = commonpb.Metadata_INSTANCE
			dProcessAssets := time.Since(startProcessAssets)

			startDeployApplication := time.Now()
			deployClient := deploygrpcpb.NewDeployServiceClient(ingressConn)
			if err := deployApplication(ctx, deployClient, app); err != nil {
				return fmt.Errorf("deploy application: %w", err)
			}
			dDeployApplication := time.Since(startDeployApplication)

			startWaitForReady := time.Now()
			solutionServiceClient := solutionservicegrpcpb.NewSolutionServiceClient(ingressConn)
			if err := waitForReady(ctx, solutionServiceClient, os.Stdout); err != nil {
				return fmt.Errorf("wait for chart assignment ready: %w", err)
			}
			dWaitForReady := time.Since(startWaitForReady)

			fmt.Printf("Timing: %.2f seconds to process assets.\n", dProcessAssets.Seconds())
			fmt.Printf("Timing: %.2f seconds to deploy application.\n", dDeployApplication.Seconds())
			fmt.Printf("Timing: %.2f seconds to wait for ready.\n", dWaitForReady.Seconds())

			fmt.Println(introspection.CloudTraceURL(span.SpanContext().TraceID.String(), project))

			// This must be at the very end.
			span.AddAttributes(trace.BoolAttribute("completed", true))
			return nil
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagsProjectOrg()
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsRateLimit(throttle.OnPremRateLimit, throttle.OnPremBurst)
	flags.AddFlagProcessingConcurrency(throttle.LocalProcessingConcurrency)
	flags.AddFlagRegistry()
	flags.AddFlagsRegistryAuthUserPassword()
	flags.AddFlagSkipDirectUpload("app")

	cmd.Flags().StringVar(&flagOperationMode, "operation_mode", "", "Sets the operation mode of the app [sim,real]")
	return cmd
}

func main() {
	ctx := context.Background()
	intrinsicinit.Init()

	flush := traceutil.SetUpTracing(ctx, flag.Args())
	defer flush()

	if err := updateCmd().ExecuteContext(ctx); err != nil {
		os.Exit(1)
	}
}
