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

package bazel

import (
	"embed"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"text/template"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/cmd/version/version"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/tools/inctl/util/templateutil"

	"github.com/spf13/cobra"
)

//go:embed templates/*
var embeddedTemplates embed.FS

const (
	keySDKRepository     = "sdk_repository"
	keyLocalSDKPath      = "local_sdk_path"
	keyOverride          = "override"
	keyBazelrcOnly       = "bazelrc_only"
	defaultSDKRepository = "https://github.com/intrinsic-ai/sdk.git"
)

var (
	flagWorkspaceRoot string
	flagSDKRepository string
	flagSDKVersion    string
	flagLocalSDKPath  string
	flagOverride      bool
	flagBazelrcOnly   bool
	flagDryRun        bool
)

// InitCmdParams contains all parameters that are relevant for executing the bazelinit command
type InitCmdParams struct {
	WorkspaceRoot string
	SdkRepository string
	SdkVersion    string
	Override      bool
	BazelrcOnly   bool
	LocalSDKPath  string
	DryRun        bool
}

type templateParams struct {
	WorkspaceName          string
	SDKRepository          string
	SDKVersion             string
	SDKStripPrefix         string
	LocalSDKPath           string
	SDKVersionDefaultValue string
}

// InitSuccessMessage is printed to stdout after a successful run. This struct is
// currently the single source of truth for the JSON format that we use in case
// of "--output=json".
type InitSuccessMessage struct {
	Message           string   `json:"message,omitempty"`
	AffectedWorkspace string   `json:"affectedWorkspace,omitempty"`
	AffectedFiles     []string `json:"affectedFiles,omitempty"`
}

// String prints a successMessage in the case of --output=text.
func (msg *InitSuccessMessage) String() string {
	var result strings.Builder
	result.WriteString(msg.Message)
	result.WriteString(" Affected files:")
	for _, affectedFile := range msg.AffectedFiles {
		result.WriteByte('\n')
		result.WriteString(filepath.Join(msg.AffectedWorkspace, affectedFile))
	}
	return result.String()
}

// RunInitCmd implements the bazel init command. It is the entry-point for
// unit tests and does not rely on any global state (e.g. global flag
// variables).
func RunInitCmd(params *InitCmdParams) (InitSuccessMessage, error) {
	var err error

	// --workspace_root flag
	workspaceRoot := params.WorkspaceRoot
	if workspaceRoot == "" {
		if workspaceRoot, err = os.Getwd(); err != nil {
			return InitSuccessMessage{}, fmt.Errorf("getting current working directory: %w", err)
		}
	}

	templateSet, err := template.ParseFS(embeddedTemplates, "templates/*.template")
	if err != nil {
		return InitSuccessMessage{}, fmt.Errorf("parsing templates: %w", err)
	}

	// Strip ".git" suffix from the SDK repository URL if present.
	SDKRepository := params.SdkRepository
	if strings.HasSuffix(SDKRepository, ".git") {
		SDKRepository = strings.TrimSuffix(SDKRepository, ".git")
	}

	// Change any slashes in the SDK version to dashes. (b/362500909)
	SDKVersion := strings.ReplaceAll(params.SdkVersion, "/", "-")

	// In the case that a version tag is used (eg "v1.2.3"), GitHub will strip the "v" prefix.
	if strings.HasPrefix(SDKVersion, "v") {
		SDKVersion = strings.TrimPrefix(SDKVersion, "v")
	}

	SDKStripPrefix := "sdk-" + SDKVersion + "/"

	// Local SDK path takes precedence over the SDK repository.
	if params.LocalSDKPath != "" {
		SDKRepository = ""
		SDKVersion = ""
	}

	templateParams := &templateParams{
		WorkspaceName:          filepath.Base(workspaceRoot),
		SDKRepository:          SDKRepository,
		SDKVersion:             params.SdkVersion,
		SDKStripPrefix:         SDKStripPrefix,
		LocalSDKPath:           params.LocalSDKPath,
		SDKVersionDefaultValue: version.SDKVersionDefaultValue,
	}

	moduleFile := filepath.Join(workspaceRoot, "MODULE.bazel")
	bazelVersionFile := filepath.Join(workspaceRoot, ".bazelversion")
	bazelrcFile := filepath.Join(workspaceRoot, ".bazelrc")
	permissiveContentMirrorFile := filepath.Join(workspaceRoot, "bazel/content_mirror/permissive.cfg")
	createdFiles := []string{bazelrcFile}

	if !params.BazelrcOnly {
		createdFiles = append(createdFiles, moduleFile)
		createdFiles = append(createdFiles, bazelVersionFile)
		createdFiles = append(createdFiles, permissiveContentMirrorFile)
	}

	// Check early for collisions with existing files to enable dry-runs and to
	// make the creation of the workspace files more atomic. I.e. we don't want to
	// create file one and then return an error because file two already exists.
	err = templateutil.CheckFilesDoNotExist(createdFiles)
	if err != nil && !params.Override {
		return InitSuccessMessage{}, err
	}

	if !params.DryRun {
		// Recursively create requested dir if it does not exist yet.
		if err = os.MkdirAll(workspaceRoot, 0o750 /*rwxr-x---*/); err != nil {
			return InitSuccessMessage{}, fmt.Errorf("creating directory %s: %w", workspaceRoot, err)
		}

		if !params.BazelrcOnly {
			if err := templateutil.CreateNewFileFromTemplate(
				moduleFile, "MODULE.bazel.template", templateParams, templateSet,
				templateutil.CreateFileOptions{
					Override: params.Override,
				}); err != nil {
				return InitSuccessMessage{}, fmt.Errorf("creating file: %w", err)
			}

			if err := templateutil.CreateNewFileFromTemplate(
				bazelVersionFile, "bazelversion.template", templateParams, templateSet,
				templateutil.CreateFileOptions{
					Override: params.Override,
				}); err != nil {
				return InitSuccessMessage{}, fmt.Errorf("creating file: %w", err)
			}
			if err := templateutil.CreateNewFileFromTemplate(
				permissiveContentMirrorFile, "permissive_content_mirror.template", templateParams, templateSet,
				templateutil.CreateFileOptions{
					Override: params.Override,
				}); err != nil {
				return InitSuccessMessage{}, fmt.Errorf("creating file: %w", err)
			}
		}

		if err = templateutil.CreateNewFileFromTemplate(
			bazelrcFile, "bazelrc.template", templateParams, templateSet,
			templateutil.CreateFileOptions{
				Override: params.Override,
			}); err != nil {
			return InitSuccessMessage{}, fmt.Errorf("creating file: %w", err)
		}
	}

	var affectedFiles []string
	for _, file := range createdFiles {
		path, err := filepath.Rel(workspaceRoot, file)
		if err != nil {
			return InitSuccessMessage{}, fmt.Errorf("getting rel path: %w", err)
		}
		affectedFiles = append(affectedFiles, path)
	}
	sort.Sort(sort.StringSlice(affectedFiles))

	successMsg := InitSuccessMessage{
		AffectedWorkspace: workspaceRoot,
		AffectedFiles:     affectedFiles,
	}
	if params.DryRun {
		successMsg.Message = fmt.Sprintf("Will initialize Bazel workspace in %q.", workspaceRoot)
	} else {
		successMsg.Message = fmt.Sprintf("Successfully initialized Bazel workspace in %q.", workspaceRoot)
	}
	return successMsg, nil
}

var initCmd = &cobra.Command{
	Use:   "init",
	Short: "Initialize a Bazel workspace",
	Long:  "Initialize a Bazel workspace for use with the Intrinsic SDK.",
	Example: `Initialize a Bazel workspace in the current working directory using the given SDK repository:
$ inctl bazel init --sdk_repository=https://github.com/intrinsic-ai/sdk.git

Initialize a Bazel workspace in the folder "/src/skill_workspace":
$ inctl bazel init --workspace_root /src/skill_workspace --sdk_repository=<repo url>

Override only the .bazelrc file in already existing workspace
$ inctl bazel init --sdk_repository=<repo url> --bazelrc-only --override
`,
	Args: cobra.NoArgs,
	RunE: func(cmd *cobra.Command, _ []string) error {
		params := &InitCmdParams{
			WorkspaceRoot: flagWorkspaceRoot,
			SdkRepository: flagSDKRepository,
			SdkVersion:    flagSDKVersion,
			LocalSDKPath:  flagLocalSDKPath,
			Override:      flagOverride,
			BazelrcOnly:   flagBazelrcOnly,
			DryRun:        flagDryRun,
		}

		successMsg, err := RunInitCmd(params)
		if err != nil {
			return err
		}

		printr, err := printer.NewPrinterWithWriter(root.FlagOutput, cmd.OutOrStdout())
		if err != nil {
			return fmt.Errorf("creating printer: %w", err)
		}
		printr.Print(&successMsg)

		return nil
	},
}

func init() {
	initCmd.Flags().StringVar(&flagWorkspaceRoot, "workspace_root", "", "(optional) Path of the directory in "+
		"which to initialize the Bazel workspace. Defaults to the current working directory. "+
		"Can either be a relative path (to the current working directory) or an absolute filesystem "+
		"path. Non-existing folders will be created automatically.")
	initCmd.Flags().StringVar(&flagSDKRepository, keySDKRepository,
		defaultSDKRepository,
		"Git repository from which to fetch the Intrinsic SDK.")

	// In the case that a development version of the SDK is used, we want to pin the workspace to the
	// latest version of the SDK.  In released versions, the version.SDKVersion is set to the
	// version of the SDK that was used to build the inctl binary.
	defaultVersion := version.SDKVersion
	if version.SDKVersion == "unknown" {
		defaultVersion = "latest"
	}

	initCmd.Flags().StringVar(&flagSDKVersion, "sdk_version", defaultVersion, "(optional) "+
		"The Intrinsic SDK version on which the new Bazel workspace should be pinned, e.g., "+
		"\"intrinsic.platform.20221231.RC00\". If set to \"latest\", the Bazel workspace will not be "+
		"pinned to a fixed version of the Intrinsic SDK but instead always depend on the newest "+
		"version available in the SDK repository (see --sdk_repository).")
	initCmd.Flags().StringVar(&flagLocalSDKPath, keyLocalSDKPath, "", "An absolute path to a local "+
		"Intrinsic SDK folder.  Overrides the --sdk_repository flag.")
	initCmd.Flags().BoolVar(&flagDryRun, "dry_run", false, "(optional) If set, no files will be "+
		"created or modified.")
	initCmd.Flags().BoolVar(&flagOverride, keyOverride, false, "If set, existing workspace files will "+
		"be overridden.")
	initCmd.Flags().BoolVar(&flagBazelrcOnly, keyBazelrcOnly, false, "If set, only the .bazelrc "+
		"file will be generated.")

	// This flag is not intended to be used externally. We use it for testing against unreleased
	// Intrinsic SDKs.
	initCmd.Flags().MarkHidden(keyLocalSDKPath)
	bazelCmd.AddCommand(initCmd)
}
