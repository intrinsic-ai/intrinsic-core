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

// Package root contains the root command for the inctl CLI.
package root

import (
	"context"
	"flag"
	"fmt"
	"os"
	"strings"

	"cloud.google.com/go/compute/metadata" 
	log "github.com/golang/glog"
	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"go.opencensus.io/trace"
	"golang.org/x/exp/slices"
	grpccodes "google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"
	apierrors "k8s.io/apimachinery/pkg/api/errors" 

	intrinsic "intrinsic/production/intrinsic"
	"intrinsic/skills/tools/skill/cmd/dialerutil"
	"intrinsic/tools/inctl/util/agents" 
	"intrinsic/tools/inctl/util/orgutil"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/tools/inctl/util/traceutil" 
)

const (
	// AssetCmdName is the name of the `inctl assets` command.
	AssetCmdName = "asset"
	// ClusterCmdName is the name of the `inctl cluster` command.
	ClusterCmdName = "cluster"

	// DoctorCmdName is the name of the `inctl doctor` command.
	DoctorCmdName = "doctor"

	// ProcessCmdName is the name of the `inctl process` command.
	ProcessCmdName = "process"
	// ServiceCmdName is the name of the `inctl service` command.
	ServiceCmdName = "service"
	// SolutionCmdName is the name of the `inctl solution` command.
	SolutionCmdName = "solution"
	// SolutionsCmdName is the alias for the `inctl solution` command.
	SolutionsCmdName = "solutions"
	// SolutionVersionCmdName is the name of the `inctl solution_version` command.
	SolutionVersionCmdName = "solution_version"
	// SkillCmdName is the name of the `inctl skill` command.
	SkillCmdName = "skill"
)

var (
	// FlagOutput holds the value of the --output flag.
	FlagOutput = printer.TextOutputFormat

	// FlagTrace holds the value of the --trace flag. 
	FlagTrace = false 
	// FlagPrintTrace prints the trace identifier to stderr on exit. 
	FlagPrintTrace = false 

	// FlagFullHelp holds the value of the --full help flag.
	FlagFullHelp = false
)

// RootCmd is the top level command of inctl.
var RootCmd = &cobra.Command{
	Use:   "inctl",
	Short: "inctl is the Intrinsic commandline tool",
	Long:  `inctl (pronounced "in control") provides access to high-level APIs and utilities of the Intrinsic stack to application developers.`,
	// Do not print usage when a command exits with an error.
	SilenceUsage: true,
	// Silence errors so we can control how they are printed.
	SilenceErrors: true,
}

type executionContext struct{}

// RewriteError looks at the root cause of an error and tries to add an
// actionable suggestion for how to resolve it.
func (e *executionContext) RewriteError(err error, cmdNames []string) string {
	cause := errors.Cause(err)

	// Guess the cause of the error. As these errors don't support errors.Is(), we
	// have to use typecasting and string comparison.
	if strings.HasPrefix(cause.Error(), "unknown command") {
		// Probably a Cobra error caused by an unknown top-level command (eg inctl asdf).
		return fmt.Sprintf("%v\nRun 'inctl --help' for usage.", err)
	}

	loginCmdHint := "'inctl auth login'"

	loginCmdHint += " (or 'inctl auth login --env=<dev|staging>' for non-prod)"


	// This will also find wrapped gRPC error/statuses.
	if grpcStatus, ok := grpcstatus.FromError(cause); ok {
		if grpcStatus.Code() == grpccodes.Unauthenticated {
			return fmt.Sprintf("%v\nStored credentials are invalid. (Re-)Run %s.", err, loginCmdHint)
		}

		// Restrict to certain commands. Otherwise this error hint is too noisy
		// (see b/292218614).
		// TODO(ferstlt): Consider removing this case once the external commands
		// have an '--org' flag and '--org'-related errors are handled here.
		if grpcStatus.Code() == grpccodes.Unavailable && len(cmdNames) > 0 &&
			slices.Contains([]string{
				ClusterCmdName, ProcessCmdName, SolutionCmdName, SolutionsCmdName, SkillCmdName,
			}, cmdNames[0]) {

			return fmt.Sprintf("%v\nThe GCP project given by --project is not reachable at the "+
				"moment or is not valid.", err)
		}


		if grpcStatus.Code() == grpccodes.Unknown {
			// Try to get some useful hints out of the error message.
			if strings.Contains(cause.Error(), "301 (Moved Permanently)") {
				return fmt.Sprintf("%v\nCheck if the service is available and enabled in this "+
					"--project.", err)
			}
		}

	}

	// Some commands don't have the --project flag as a hard requirement but have
	// execution paths which require it so that the correct API keys can be loaded.
	if errors.Is(cause, dialerutil.ErrCredentialsRequired) {
		return fmt.Sprintf("%v\nThe --project flag is required to load the appropriate "+
			"credentials.", err)
	}

	// User org not known
	var orgErr *orgutil.ErrOrgNotFound
	if errors.As(cause, &orgErr) {
		base := fmt.Sprintf("Credentials for given organization %q not found.", orgErr.OrgName)
		additions := []string{}
		if len(orgErr.CandidateOrgs) > 0 {
			additions = append(additions, "There's similar organizations in your store:")
			for _, org := range orgErr.CandidateOrgs {
				additions = append(additions, fmt.Sprintf(" - %s", org))
			}
		}

		if len(additions) > 0 {
			return fmt.Sprintf("%s\n%s\nTo add %q run %s.", base, strings.Join(additions, "\n"), orgErr.OrgName, loginCmdHint)
		}

		return fmt.Sprintf("%s\nRun %s to add it.", base, loginCmdHint)
	}

	// User not logged in.
	var credErr *dialerutil.ErrCredentialsNotFound
	if errors.As(cause, &credErr) {
		return fmt.Sprintf("%v\nCredentials for given organization not found. Run %s.", err, loginCmdHint)
	}


	if apierrors.IsUnauthorized(cause) || strings.Contains(cause.Error(), "403 Forbidden") {
		// Probably missing cloud access.
		cmd, _, _ := RootCmd.Find(flag.Args())
		project := orgutil.GetProject(cmd, flag.Args())
		if project == "" {
			project = "<project>"
		}
		return fmt.Sprintf(`%v
If using a customer cluster, you might need to request temporary GCP access (go/intrinsic-project-access):
    inctl auth temp-gcp-access --project=%s --bug=b/<bug-number> --justification="<reason>"`, err, project)
	}

	var notDefinedError metadata.NotDefinedError
	if errors.As(cause, &notDefinedError) ||
		strings.Contains(cause.Error(), "google: could not find default credentials") {
		// These errors appear on cloudtop/workstation when no GCE credentials are found.
		return fmt.Sprintf(`%v
Try running:
    gcloud auth login --quiet --update-adc --no-launch-browser`, err)
	}


	return err.Error()
}

// getCommandNames returns a vector of subcommand names - e.g. ["app", "status"]
// for "inctl app status" or [] for "inctl". Returns an error if there is no
// matching command, e.g. because the user misspelled the command name(s).
//
// DO NOT CALL this before executing the root command or it might behave
// unexpectedly (e.g. for "inctl help" it will return an error instead of
// ["help"]).
func getCommandNames() ([]string, error) {
	cmd, _, err := RootCmd.Find(flag.Args())
	if err != nil {
		return nil, err
	}

	var names []string
	for node := cmd; node.HasParent(); node = node.Parent() {
		names = append([]string{node.Name()}, names...)
	}
	return names, nil
}

// Execute is the top level function that runs the app and prints any errors.
// It returns true if the command was successful.
// rewriteError rewrites an error into a helpful string.
func Execute(ec executionContext) bool {
	ctx := context.Background()
	RootCmd.SetArgs(flag.Args())


	flush := traceutil.SetUpTracing(ctx, flag.Args())
	defer flush()


	ctx, span := trace.StartSpan(ctx, "inctl")
	defer span.End()

	success := true
	invocationStatus := "SUCCESS" 
	if err := RootCmd.ExecuteContext(ctx); err != nil {
		cmdNames, _ := getCommandNames() // ignore error, cmdNames will simply be nil
		RootCmd.PrintErrln("Error:", ec.RewriteError(err, cmdNames), "TraceID:", span.SpanContext().TraceID)
		success = false


		if errors.Is(err, agents.ErrAgentBlocked) {
			invocationStatus = "AGENT_BLOCKED"
		} else {
			invocationStatus = "ERROR"
		}

	}


	span.AddAttributes(trace.StringAttribute("inctl_invocation_status", invocationStatus))

	cmdNames, err := getCommandNames()
	if err != nil {
		// The only reason for getCommandNames to return an error is if the command does not exist,
		// i.e. it was incorrectly entered (a typo), meaning executing it should not
		// have been successful. Avoid logging anything in this common case, to
		// not clobber the default Cobra "command not found" output.
		if success {
			log.FatalContextf(ctx, `error finding command even though executing it was successful.
				This is absolutely not expected.`)
		}
		return false
	}

	// Set full command here at the end.
	// getCommandNames() documents to not call before RootCmd.ExecuteContext.
	span.SetName("inctl " + strings.Join(cmdNames, " "))
	// Add the hostname for audit logging for internal users
	if host, err := os.Hostname(); err == nil && host != "" { // no error and host non-empty
		span.AddAttributes(trace.StringAttribute("host", host))
	}
	if agents.IsAgent() {
		span.AddAttributes(
			trace.BoolAttribute("agent", true),
			trace.StringAttribute("agent_name", agents.AgentName()),
		)
	} else {
		span.AddAttributes(trace.BoolAttribute("agent", false))
	}


	return success
}

// Inctl launches inctl with the currently configured commands.
func Inctl() {
	intrinsic.Init()


	if d := os.Getenv("BUILD_WORKING_DIRECTORY"); d != "" {
		// We were invoked by `bazel run`. Restore the original directory so the user
		// can use relative paths.
		if err := os.Chdir(d); err != nil {
			log.Warningf("Failed to restore working directory %q: %v", d, err)
		}
	}


	success := Execute(executionContext{})

	if !success {
		log.Warning("Command failed")
		os.Exit(1)
	}
}

// helpCmd overrides the default help command to support the --full flag.
var helpCmd = &cobra.Command{
	Use:   "help [command]",
	Short: "Help about any command",
	Run: func(cmd *cobra.Command, args []string) {
		if FlagFullHelp {
			target := RootCmd
			// If arguments are provided, find the specific subcommand to print help for.
			if len(args) > 0 {
				var err error
				target, _, err = RootCmd.Find(args)
				if err != nil {
					fmt.Fprintln(cmd.OutOrStderr(), err)
					return
				}
			}
			PrintFullTree(target, "")
			return
		}
		// Fallback to standard help
		if len(args) == 0 {
			RootCmd.Help()
			return
		}
		// Find the target command from the arguments.
		target, _, err := RootCmd.Find(args)
		if err != nil {
			fmt.Fprintln(cmd.OutOrStderr(), err)
			return
		}
		target.Help()
	},
}

func init() {
	RootCmd.PersistentFlags().StringVarP(
		&FlagOutput, printer.KeyOutput, "o", printer.TextOutputFormat,
		fmt.Sprintf("(optional) Output format. One of: (%s)", strings.Join(printer.AllowedFormats, ", ")))

	RootCmd.PersistentFlags().BoolVar(
		&FlagPrintTrace, "print-trace", false, "Print the trace identifier when exiting.")


	helpCmd.Flags().BoolVar(&FlagFullHelp, "full", false, "Display description of all subcommands and exit.")
	RootCmd.SetHelpCommand(helpCmd)
}
