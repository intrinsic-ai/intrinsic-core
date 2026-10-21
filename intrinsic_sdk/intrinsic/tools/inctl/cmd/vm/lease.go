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

package vm

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/signal"
	"os/user"
	"regexp"
	"strings"
	"syscall"
	"time"
	"unicode"

	"intrinsic/kubernetes/vmpool/service/pkg/defaults"
	"intrinsic/tools/inctl/util/color"
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"

	"github.com/pborman/uuid"
	"github.com/rs/xid"
	"github.com/spf13/cobra"
	"go.opencensus.io/trace"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/encoding/protojson"

	leaseapigrpcpb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	leasepb "intrinsic/kubernetes/vmpool/manager/api/v1/lease_api_go_proto"
	vmpoolapigrpcpb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"
	vmpoolpb "intrinsic/kubernetes/vmpool/service/api/v1/vmpool_api_go_proto"

	tpb "google.golang.org/protobuf/types/known/timestamppb"
)

// randomID is a function to generate a random ID.
// It is stubbed out for testing.
var randomID = func() string {
	return xid.New().String()
}

var leaseDesc = `
Lease a VM from a pool of VMs.

There are three ways to specify the VM to lease:
1. Specify nothing and let the server choose a pool to lease from.
2. Specify the pool name with --pool <pool-name>.
3. Specify the runtime version with --runtime <runtime-version> and/or the IntrinsicOS version with --intrinsic-os <intrinsic-os-version>. If one of both are omitted, the server will backfill the missing with the latest version.

Example:
	inctl vm lease

	or headless:

	LEASEDVM=$(inctl vm lease --silent)
`

const retryInterval = 20 * time.Second

var vmLeaseCmd = &cobra.Command{
	Use:   "lease",
	Short: "Lease a VM from a pool of VMs.",
	Long:  leaseDesc,
	RunE: func(cmd *cobra.Command, args []string) error {
		ctx, stop := signal.NotifyContext(cmd.Context(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		ctx, span := trace.StartSpan(ctx, "inctl.vm.lease")
		span.AddAttributes(trace.StringAttribute("pool", flagPool))
		span.AddAttributes(trace.StringAttribute("org", vmCmdFlags.GetFlagOrganization()))
		defer span.End()
		cl, err := newLeaseClient(ctx)
		if err != nil {
			return err
		}
		pc, err := newVmpoolsClient(ctx)
		if err != nil {
			return err
		}
		err = Lease(ctx, cl, &pc, printer.GetFlagOutputType(cmd), &LeaseOptions{
			AbortAfter:    flagAbortAfter,
			Duration:      flagDuration,
			ReservationID: flagReservationID,
			Retry:         flagRetry,
			Pool:          flagPool,
			Project:       vmCmdFlags.GetFlagProject(),
			SetContext:    flagSetContext,
			ContextAlias:  flagContextAlias,
			Runtime:       flagRuntime,
			IntrinsicOS:   flagIntrinsicOS,
			Silent:        flagSilent,
			Stderr:        os.Stderr,
			ServiceTag:    flagServiceTag,
		})
		if err != nil {
			// If the context of RunE (which intercepts SIGINT/SIGTERM) was cancelled,
			// it means the user interrupted the command. Exit gracefully without printing error.
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		return nil
	},
}

// LeaseOptions contains the options for leasing a VM.
type LeaseOptions struct {
	AbortAfter    time.Duration
	Duration      string
	ReservationID string
	Retry         bool
	Pool          string
	Project       string
	SetContext    bool
	ContextAlias  string
	Runtime       string
	IntrinsicOS   string
	Silent        bool
	Stderr        io.Writer
	ServiceTag    string
}

// Lease leases a VM from a pool of VMs.
func Lease(ctx context.Context, leaseClient leaseapigrpcpb.VMPoolLeaseServiceClient, poolClient *vmpoolapigrpcpb.VMPoolServiceClient, outputType printer.OutputType, opts *LeaseOptions) error {
	span := trace.FromContext(ctx)
	ctx, cancel := context.WithTimeout(ctx, opts.AbortAfter)
	defer cancel()
	now := time.Now()
	var duration time.Duration
	if opts.Duration != "" {
		var err error
		duration, err = time.ParseDuration(opts.Duration)
		if err != nil {
			return fmt.Errorf("%v is not valid for time.ParseDuration: %v", opts.Duration, err)
		}
	}
	if opts.ReservationID != "" {
		span.AddAttributes(trace.StringAttribute("reservation_id", opts.ReservationID))
	}
	isLeaseTypeAdhoc := false
	if opts.Runtime != "" {
		span.AddAttributes(trace.StringAttribute("runtime", opts.Runtime))
		isLeaseTypeAdhoc = true
	}
	if opts.IntrinsicOS != "" {
		span.AddAttributes(trace.StringAttribute("os_tag", opts.IntrinsicOS))
		isLeaseTypeAdhoc = true
	}
	opts.ReservationID = strings.TrimSpace(opts.ReservationID)
	if opts.ReservationID == "" {
		opts.ReservationID = uuid.New()
	}
	var lr *leaseResult
	var err error
	if isLeaseTypeAdhoc {
		lr, err = requestAdhocLease(ctx, duration, leaseClient, poolClient, opts)
	} else {
		lr, err = requestLease(ctx, duration, leaseClient, opts)
	}
	if err != nil {
		return fmt.Errorf("failed lease: %w", err)
	}
	l := lr.lease

	if outputType == printer.OutputTypeJSON {
		ms, err := protojson.MarshalOptions{
			Multiline:         true,
			UseProtoNames:     true,
			EmitUnpopulated:   true,
			EmitDefaultValues: true,
		}.Marshal(l)
		if err != nil {
			return fmt.Errorf("failed to marshal lease to json: %w", err)
		}
		// We print directly to stdout to avoid double encoding by the json printer
		fmt.Println(string(ms))
		return nil
	}

	if opts.Silent {
		fmt.Print(l.GetInstance())
		return nil
	}
	fmt.Printf("Your shiny new VM is ready (leasing took %s)\n", time.Since(now).Round(time.Second))
	fmt.Println("- Instance:", l.GetInstance())
	span.AddAttributes(trace.StringAttribute("instance", l.GetInstance()))
	fmt.Println("- Pool:", l.GetPool())
	color.C.BlueBackground().White().Printf("- Frontend URL: %s", getPortalURL(opts.Project, l.GetInstance()))
	fmt.Println("")
	gotExpires := l.GetExpires().AsTime()
	fmt.Printf("- Lease expires: %s (in %s)\n", gotExpires.Format(time.RFC3339), time.Until(gotExpires).Round(time.Second))

	// $20 estimate based on hourly costs in us-central1 of $0.38 for n1-standard-8 an $0.35 for
	// nvidia-tesla-t4.
	// https://cloud.google.com/compute/vm-instance-pricing
	// https://cloud.google.com/compute/gpus-pricing
	fmt.Println("\nVMs cost around $20 for 24h, please return it when you don't need it anymore:")


	fmt.Printf("	inctl vm return %s --org %s\n", l.GetInstance(), orgutil.QualifiedOrg(opts.Project, vmCmdFlags.GetFlagOrganization()))

	fmt.Println("\nExtend the lease for the VM if you need it longer:")
	fmt.Printf("	inctl vm expire-in %s 1h --org %s\n", l.GetInstance(), orgutil.QualifiedOrg(opts.Project, vmCmdFlags.GetFlagOrganization()))


	fmt.Printf("\nConnect via k9s:\n")
	fmt.Printf("	k9s --context %s\n", lr.context)
	fmt.Println("\nConnect via SSH:")
	fmt.Printf("	inctl ssh --context %s --no-tofu\n", lr.context)


	return nil
}

type leaseResult struct {
	lease   *leasepb.Lease
	context string
}

func getContext(ctx context.Context, opts *LeaseOptions, l *leasepb.Lease) (string, error) {
	if !opts.SetContext {
		return l.GetInstance(), nil
	}
	retContext := l.GetInstance()
	if len(opts.ContextAlias) > 0 {
		retContext = opts.ContextAlias
	}
	return retContext, nil
}

func optionalExpiresIn(optDuration time.Duration) *tpb.Timestamp {
	var t time.Time
	if optDuration != 0 {
		t = time.Now().Add(optDuration)
	}
	return tpb.New(t)
}

// requestLease a VM from a pool.
func requestLease(ctx context.Context, duration time.Duration, leaseClient leaseapigrpcpb.VMPoolLeaseServiceClient, opts *LeaseOptions) (*leaseResult, error) {
	var l *leasepb.Lease
	for l == nil { // retry until lease successful or retry not set
		req := &leasepb.LeaseRequest{
			Pool:          opts.Pool,
			Expires:       optionalExpiresIn(duration),
			ServiceTag:    opts.ServiceTag,
			ReservationId: &opts.ReservationID,
		}
		lResp, err := leaseClient.Lease(ctx, req)
		if err != nil {
			if status.Code(err) == codes.PermissionDenied {
				return nil, fmt.Errorf("lease request failed: %v\n. Your api-key might have expired, run `inctl auth login` to refresh it and retry", err)
			}
			if status.Code(err) == codes.Unauthenticated {
				return nil, fmt.Errorf("lease request failed: %v\n. Please ensure you are logged in via `inctl auth login` and try again", err)
			}
			if ctx.Err() != nil {
				return nil, fmt.Errorf("lease request failed: %v. please try again", ctx.Err())
			}
			if opts.Retry {
				fmt.Fprintf(opts.Stderr, "lease request did not succeed yet, retrying soon: %v\n", err)
				time.Sleep(retryInterval)
				continue
			}
			return nil, fmt.Errorf("lease failed, consider using --retry. %v", err)
		}
		l = lResp.GetLease()
	}

	retContext, err := getContext(ctx, opts, l)
	if err != nil {
		fmt.Printf("Failed to get context: %v\n", err)
	}

	return &leaseResult{lease: l, context: retContext}, nil
}

// getPoolName creates a gcp-compatible random pool name.
// If the user can be determined, the username is added to the pool name.
// Transformation for the username string e.g.: max.mustermann@intrinsic.ai -> max-mustermann-intrinsic-ai
func getPoolName(u *user.User) string {
	if u == nil {
		return fmt.Sprintf("adhoc-lease-%s", randomID())
	}
	// Replace all non-alphanumeric and non-hyphen characters with a hyphen
	var result strings.Builder

	for _, r := range u.Username {
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '-' {
			result.WriteRune(r)
		} else {
			result.WriteRune('-')
		}
	}

	// Clean up the string by replacing multiple hyphens with a single one
	re := regexp.MustCompile("-{2,}")
	transformed := re.ReplaceAllString(result.String(), "-")

	// Trim leading and trailing hyphens
	transformed = strings.Trim(transformed, "-")

	// add suffix because we will add a random suffix to the pool name
	return fmt.Sprintf("adhoc-lease-%s-%s", transformed, randomID())
}

func createPoolIfNeeded(ctx context.Context, poolClient *vmpoolapigrpcpb.VMPoolServiceClient, opts *LeaseOptions) (bool, error) {
	if poolClient == nil {
		return false, fmt.Errorf("poolClient is not available")
	}
	if opts.Pool != "" {
		return false, nil
	}
	if opts.Runtime == "" && opts.IntrinsicOS == "" {
		return false, nil
	}

	u, _ := user.Current()

	poolName := getPoolName(u)
	resp, err := (*poolClient).CreatePool(ctx, &vmpoolpb.CreatePoolRequest{
		Name: poolName,
		Spec: &vmpoolpb.Spec{
			PoolTier:         defaults.Tier,
			HardwareTemplate: defaults.HardwareTemplate,
			Runtime:          opts.Runtime,
			IntrinsicOs:      opts.IntrinsicOS,
		},
	})
	if err != nil {
		return false, err
	}
	// use stderr for debug logs
	fmt.Fprintf(opts.Stderr, "Created pool with runtime %s and IntrinsicOS %s to satisfy the request.\n", resp.GetSpec().GetRuntime(), resp.GetSpec().GetIntrinsicOs())
	fmt.Fprintln(opts.Stderr, "Leasing from this pool, once lease succeeds, this pool will be deleted.")
	fmt.Fprintln(opts.Stderr, "\nIf you abort this command from now on before the pool is deleted, you need to delete it manually:")
	fmt.Fprintf(opts.Stderr, "\tinctl vm pool delete --pool %s --org %s\n\n", resp.GetName(), orgutil.QualifiedOrg(vmCmdFlags.GetFlagProject(), vmCmdFlags.GetFlagOrganization()))
	fmt.Fprintln(opts.Stderr, "This can take a few minutes, please be patient or grab a coffee c|_|")
	opts.Pool = resp.GetName()
	return true, nil
}

func requestAdhocLease(ctx context.Context, duration time.Duration, leaseClient leaseapigrpcpb.VMPoolLeaseServiceClient, poolClient *vmpoolapigrpcpb.VMPoolServiceClient, opts *LeaseOptions) (*leaseResult, error) {
	reservationUUID := strings.TrimSpace(opts.ReservationID)
	if reservationUUID == "" {
		reservationUUID = uuid.New()
	}

	isAdhocPoolPath, err := createPoolIfNeeded(ctx, poolClient, opts)
	if err != nil {
		return nil, err
	}

	var leasedVM *leasepb.Lease

	if isAdhocPoolPath {
		defer func() {
			cleanupCtx, cleanupCancel := context.WithTimeout(context.WithoutCancel(ctx), 30*time.Second)
			defer cleanupCancel()

			if leasedVM == nil {
				fmt.Fprintf(opts.Stderr, "\nAborting... Cleaning up temporary pool %s\n", opts.Pool)
			} else {
				fmt.Fprintf(opts.Stderr, "\nCleaned up temporary pool %s\n", opts.Pool)
			}

			if _, err := (*poolClient).DeletePool(cleanupCtx, &vmpoolpb.DeletePoolRequest{Name: opts.Pool}); err != nil {
				fmt.Fprintf(opts.Stderr, "Failed to delete pool %s: %v\n Please delete it manually with: \n\t`inctl vm pool delete --pool %s --org %s`\n\n", opts.Pool, err, opts.Pool, orgutil.QualifiedOrg(vmCmdFlags.GetFlagProject(), vmCmdFlags.GetFlagOrganization()))
			} else if leasedVM == nil {
				fmt.Fprintf(opts.Stderr, "Successfully deleted temporary pool %s. No manual cleanup required.\n", opts.Pool)
			}
		}()
	}

	poolIsBooting := isAdhocPoolPath

	var l *leasepb.Lease
	for l == nil { // retry until lease successful or retry not set
		req := &leasepb.LeaseRequest{
			Pool:       opts.Pool,
			Expires:    optionalExpiresIn(duration),
			ServiceTag: opts.ServiceTag,
		}
		if reservationUUID != "" {
			req.ReservationId = &reservationUUID
		}
		lResp, err := leaseClient.Lease(ctx, req)
		if err != nil {
			if status.Code(err) == codes.PermissionDenied {
				return nil, fmt.Errorf("lease request failed: %v\n. Your api-key might have expired, run `inctl auth login` to refresh it and retry", err)
			}
			if status.Code(err) == codes.Unauthenticated {
				return nil, fmt.Errorf("lease request failed: %v\n. Please ensure you are logged in via `inctl auth login` and try again", err)
			}
			if ctx.Err() != nil {
				return nil, fmt.Errorf("lease request failed: %v. please try again", ctx.Err())
			}
			if status.Code(err) == codes.NotFound && poolIsBooting {
				fmt.Fprint(opts.Stderr, ".")
			}
			if status.Code(err) != codes.NotFound && poolIsBooting { // once the pool is present we deactivate the booting state
				poolIsBooting = false
				fmt.Fprintln(opts.Stderr)
			}
			if !poolIsBooting { // skip messages about pool not beeing present if the pool is booting to not confuse users
				fmt.Fprintf(opts.Stderr, "lease request did not succeed yet, retrying soon: %v\n", err)
			}
			select {
			case <-ctx.Done():
				return nil, fmt.Errorf("lease request failed: %v. please try again", ctx.Err())
			case <-time.After(retryInterval):
			}
		} else {
			l = lResp.GetLease()
		}
	}

	leasedVM = l

	retContext, err := getContext(ctx, opts, l)
	if err != nil {
		fmt.Fprintf(opts.Stderr, "Failed to get context: %v\n", err)
	}

	return &leaseResult{lease: l, context: retContext}, nil
}
