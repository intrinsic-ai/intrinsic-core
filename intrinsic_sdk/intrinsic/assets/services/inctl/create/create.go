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

// Package create defines the service create command.
package create

import (
	"embed"
	"fmt"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"text/template"

	"intrinsic/assets/idutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/buildozer"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/tools/inctl/util/templateutil"

	"github.com/spf13/cobra"
)

var (
	flagProtoPackage string
	flagLanguage     string
	flagDryRun       bool
	flagPath         string
)

type cmdParams struct {
	assetID       string
	bazelPackage  string
	workspaceRoot string
	protoPackage  string
	language      string
	dryRun        bool
	outputType    string
}

//go:embed templates/*
var embeddedTemplates embed.FS

var supportedLanguages = []string{"cpp", "python"}

func validateParams(params *cmdParams) error {
	if err := idutils.ValidateID(params.assetID); err != nil {
		return err
	}

	if !slices.Contains(supportedLanguages, params.language) {
		return fmt.Errorf("unknown language %s, must be one of %v", params.language, supportedLanguages)
	}
	return nil
}

type BazelOutput struct {
	WorkspaceRoot string
	BazelPackage  string
}

// bazelInfo determines the workspace root and the relative package path.
// It supports paths that do not exist yet by climbing the tree to find the workspace.
func bazelInfo(path string) (BazelOutput, error) {
	absPath, err := filepath.Abs(path)
	if err != nil {
		return BazelOutput{}, err
	}

	// Find the closest existing ancestor directory
	existingDir := absPath
	for {
		info, err := os.Stat(existingDir)
		if err == nil && info.IsDir() {
			break
		}

		parent := filepath.Dir(existingDir)
		if parent == existingDir {
			return BazelOutput{}, fmt.Errorf("could not find an existing directory for path: %s", path)
		}
		existingDir = parent
	}

	// Traverse upwards to find the Bazel workspace root
	workspaceRoot := ""
	currentDir := existingDir
	for {
		if isWorkspaceRoot(currentDir) {
			workspaceRoot = currentDir
			break
		}

		parent := filepath.Dir(currentDir)
		// If we've reached the root of the filesystem and found no boundary files
		if parent == currentDir {
			return BazelOutput{}, fmt.Errorf("failed to find bazel workspace: no MODULE.bazel or WORKSPACE found in %s or its parents", existingDir)
		}
		currentDir = parent
	}

	// Calculate the relative path from the workspace root to the ORIGINAL target path
	relPackage, err := filepath.Rel(workspaceRoot, absPath)
	if err != nil {
		return BazelOutput{}, fmt.Errorf("path %s is not inside bazel workspace %s", absPath, workspaceRoot)
	}

	// Sanity check: ensure the path isn't "outside" the workspace (e.g., ../../)
	if strings.HasPrefix(relPackage, "..") {
		return BazelOutput{}, fmt.Errorf("path %s is outside of the bazel workspace %s", absPath, workspaceRoot)
	}

	// If the path is the root itself, Rel returns ".", but we want an empty string
	if relPackage == "." {
		relPackage = ""
	}

	return BazelOutput{
		WorkspaceRoot: workspaceRoot,
		// Ensure package paths always use forward slashes (important for Windows vs Bazel compat)
		BazelPackage: filepath.ToSlash(relPackage),
	}, nil
}

// isWorkspaceRoot checks if a directory contains a Bazel workspace boundary file.
func isWorkspaceRoot(dir string) bool {
	boundaries := []string{"MODULE.bazel", "WORKSPACE.bazel", "WORKSPACE"}
	for _, boundary := range boundaries {
		info, err := os.Stat(filepath.Join(dir, boundary))
		if err == nil && !info.IsDir() {
			return true
		}
	}
	return false
}

// runCreateCmd implements the service create stub.
func runCreateCmd(cmdParams *cmdParams, p printer.CommandPrinter) error {
	if cmdParams.protoPackage == "" {
		// Don't assume assetID will always be a valid proto package.
		cmdParams.protoPackage = strings.Join(idutils.SplitID(cmdParams.assetID), ".")
	}

	if err := validateParams(cmdParams); err != nil {
		return err
	}

	templateParams, err := makeTemplateParameters(cmdParams)
	if err != nil {
		return err
	}

	var newFileFromTemplates []newFileFromTemplate
	var appendFileFromTemplates []appendFileFromTemplate
	var buildozerCommands []string
	switch cmdParams.language {
	case "cpp":
		newFileFromTemplates, appendFileFromTemplates = gatherCppTemplates(cmdParams, &templateParams)
		buildozerCommands = gatherCppBuildozerCommands()
	case "python":
		newFileFromTemplates, appendFileFromTemplates = gatherPythonTemplates(cmdParams, &templateParams)
		buildozerCommands = gatherPythonBuildozerCommands()
	default:
		return fmt.Errorf("unsupported language %q", cmdParams.language)
	}

	if cmdParams.dryRun {
		p.Println("Files to be created:")
	} else {
		p.Println("Creating files:")
	}
	for _, t := range newFileFromTemplates {
		p.Printf("\t%s\n", t.newFile)
	}
	if cmdParams.dryRun {
		p.Println("Files to be modified:")
	} else {
		p.Println("Modifying files:")
	}
	for _, t := range appendFileFromTemplates {
		p.Printf("\t%s\n", t.changeFile)
	}

	if !cmdParams.dryRun {
		if err := expandTemplates(templateParams, newFileFromTemplates, appendFileFromTemplates); err != nil {
			return err
		}
		if err := buildozer.ExecuteBuildozerCommands(buildozerCommands, cmdParams.workspaceRoot, templateParams.BazelPackage); err != nil {
			return err
		}
		p.Println("Successfully created service %s", cmdParams.assetID)
	}

	return nil
}

type serviceParams struct {
	ServiceNameUpperSnakeCase string // e.g. "MY_SERVICE"
	ServiceNameUpperCamelCase string // e.g. "MyService"
	ServiceNameSnakeCase      string // e.g. "my_service"
	ServicePackageName        string // e.g. "com.my_org"

	// Package of service configuration proto. E.g. "my_org.motion" becomes ["my_org", "motion"].
	// Stored as a string array so that it can be string-joined later.
	ProtoPackage []string
	ProtoName    string

	// Bazel package (=path from WORKSPACE root to service directory).
	// Stored as a string array so that it can be flexibly string-joined later.
	BazelPackage               []string // e.g. ["services", "my_service"]
	BazelPackageUpperSnakeCase string   // e.g. "SERVICES_MY_SERVICE"
}

func makeTemplateParameters(params *cmdParams) (serviceParams, error) {
	// Get service name "com.org.my_service" -> "my_service"
	snakeCase, err := idutils.NameFrom(params.assetID)
	if err != nil {
		return serviceParams{}, err
	}

	// Get package name "com.org.my_service" -> "com.org"
	servicePackage, err := idutils.PackageFrom(params.assetID)
	if err != nil {
		return serviceParams{}, err
	}

	// Convert snake_case to UpperCamelCase (e.g., "my_service" -> "MyService")
	var upperCamel string
	for _, part := range strings.Split(snakeCase, "_") {
		if len(part) > 0 {
			upperCamel += strings.ToUpper(part[:1]) + part[1:]
		}
	}

	// Process Proto Package segments
	protoParts := strings.Split(params.protoPackage, ".")

	// Process Bazel Package segments, filtering out empty strings from potential leading/trailing slashes
	var bazelParts []string
	for _, part := range strings.Split(params.bazelPackage, "/") {
		if part != "" {
			bazelParts = append(bazelParts, part)
		}
	}

	return serviceParams{
		ServiceNameUpperSnakeCase:  strings.ToUpper(snakeCase),
		ServiceNameUpperCamelCase:  upperCamel,
		ServiceNameSnakeCase:       snakeCase,
		ServicePackageName:         servicePackage,
		ProtoPackage:               protoParts,
		BazelPackage:               bazelParts,
		BazelPackageUpperSnakeCase: strings.ToUpper(strings.Join(bazelParts, "_")),
	}, nil
}

type appendFileFromTemplate struct {
	changeFile   string
	templateName string
}

type newFileFromTemplate struct {
	newFile      string
	templateName string
}

func gatherCppTemplates(params *cmdParams, templateParams *serviceParams) ([]newFileFromTemplate, []appendFileFromTemplate) {
	outputDir := filepath.Join(params.workspaceRoot, params.bazelPackage)
	snakeName := templateParams.ServiceNameSnakeCase
	buildFilePath := filepath.Join(outputDir, "BUILD")

	newFiles := []newFileFromTemplate{
		{
			newFile:      filepath.Join(outputDir, snakeName+".proto"),
			templateName: "templates/config_proto.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+"_defaults.textproto"),
			templateName: "templates/defaults.textproto.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+"_manifest.textproto"),
			templateName: "templates/service_manifest.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+".cc"),
			templateName: "templates/service_cc.template",
		},
	}

	var appendFiles []appendFileFromTemplate

	// Check if BUILD file exists to decide between Create or Append
	if _, err := os.Stat(buildFilePath); os.IsNotExist(err) {
		newFiles = append(newFiles, newFileFromTemplate{
			newFile:      buildFilePath,
			templateName: "templates/BUILD_cc.template",
		})
	} else {
		appendFiles = append(appendFiles, appendFileFromTemplate{
			changeFile:   buildFilePath,
			templateName: "templates/BUILD_cc.template",
		})
	}

	return newFiles, appendFiles
}

func gatherCppBuildozerCommands() []string {
	return []string{
		"new_load @rules_cc//cc:cc_binary.bzl cc_binary",
		"new_load @com_google_protobuf//bazel:proto_library.bzl proto_library",
		"new_load @com_google_protobuf//bazel:cc_proto_library.bzl cc_proto_library",
		"new_load @ai_intrinsic_sdks//intrinsic/assets/services/build_defs:services.bzl intrinsic_service",
		"new_load @ai_intrinsic_sdks//bazel:container.bzl container_image",
		"fix movePackageToTop",
		"fix unusedLoads",
	}
}

func gatherPythonTemplates(params *cmdParams, templateParams *serviceParams) ([]newFileFromTemplate, []appendFileFromTemplate) {
	outputDir := filepath.Join(params.workspaceRoot, params.bazelPackage)
	snakeName := templateParams.ServiceNameSnakeCase
	buildFilePath := filepath.Join(outputDir, "BUILD")

	newFiles := []newFileFromTemplate{
		{
			newFile:      filepath.Join(outputDir, snakeName+".proto"),
			templateName: "templates/config_proto.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+"_defaults.textproto"),
			templateName: "templates/defaults.textproto.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+"_manifest.textproto"),
			templateName: "templates/service_manifest.template",
		},
		{
			newFile:      filepath.Join(outputDir, snakeName+".py"),
			templateName: "templates/service_py.template",
		},
	}

	var appendFiles []appendFileFromTemplate

	// Check if BUILD file exists to decide between Create or Append
	if _, err := os.Stat(buildFilePath); os.IsNotExist(err) {
		newFiles = append(newFiles, newFileFromTemplate{
			newFile:      buildFilePath,
			templateName: "templates/BUILD_py.template",
		})
	} else {
		appendFiles = append(appendFiles, appendFileFromTemplate{
			changeFile:   buildFilePath,
			templateName: "templates/BUILD_py.template",
		})
	}

	return newFiles, appendFiles
}

func gatherPythonBuildozerCommands() []string {
	return []string{
		"new_load @rules_python//python:defs.bzl py_binary",
		"new_load @com_google_protobuf//bazel:proto_library.bzl proto_library",
		"new_load @com_github_grpc_grpc//bazel:python_rules.bzl py_proto_library",
		"new_load @ai_intrinsic_sdks//intrinsic/assets/services/build_defs:services.bzl intrinsic_service",
		"new_load @ai_intrinsic_sdks//bazel:python_oci_image.bzl python_oci_image",
		"fix movePackageToTop",
		"fix unusedLoads",
	}
}

func expandTemplates(
	params serviceParams,
	newFiles []newFileFromTemplate,
	appendFiles []appendFileFromTemplate,
) error {
	// Define custom functions (like strJoin)
	funcMap := template.FuncMap{
		"strJoin": func(values []string, sep string) string {
			return strings.Join(values, sep)
		},
	}

	// Create the template set from the embedded FS
	// Note: ParseFS uses the path relative to the //go:embed directive
	templateSet, err := template.New("").Funcs(funcMap).ParseFS(embeddedTemplates, "templates/*.template")
	if err != nil {
		return fmt.Errorf("failed to parse embedded templates: %w", err)
	}

	// Create new files
	for _, nf := range newFiles {
		// Ensure the directory exists
		if err := os.MkdirAll(filepath.Dir(nf.newFile), 0755); err != nil {
			return fmt.Errorf("failed to create directory for %s: %w", nf.newFile, err)
		}

		// Use the helper to create the file
		if err := templateutil.CreateNewFileFromTemplate(
			nf.newFile,
			filepath.Base(nf.templateName), // ParseFS names templates by their base name
			&params,
			templateSet,
			templateutil.CreateFileOptions{},
		); err != nil {
			return fmt.Errorf("error creating file %s: %w", nf.newFile, err)
		}
	}

	// Append to existing files
	for _, af := range appendFiles {
		if err := templateutil.AppendToExistingFileFromTemplate(
			af.changeFile,
			filepath.Base(af.templateName),
			&params,
			templateSet,
		); err != nil {
			return fmt.Errorf("error appending to file %s: %w", af.changeFile, err)
		}
	}

	return nil
}

// Command creates a new service create command.
func Command() *cobra.Command {
	createCmd := &cobra.Command{
		Use:   "create asset_id",
		Short: "Create a new Service.",
		Long:  "Create the sources and build rules for a new Service.",
		Example: `Create a Service with name "my_service" and package "com.my_org":
$ inctl service create com.my_org.my_service --language python`,
		Args:       cobra.ExactArgs(1),
		ArgAliases: []string{"asset_id"},
		RunE: func(cmd *cobra.Command, argsArray []string) error {
			bzInfo, err := bazelInfo(flagPath)
			if err != nil {
				return fmt.Errorf("bazel workspace not found. Run 'inctl bazel init' to create one. (from error %v)", err)
			}

			params := cmdParams{
				assetID:       argsArray[0],
				workspaceRoot: bzInfo.WorkspaceRoot,
				bazelPackage:  bzInfo.BazelPackage,
				protoPackage:  flagProtoPackage,
				language:      flagLanguage,
				dryRun:        flagDryRun,
				outputType:    root.FlagOutput,
			}

			p, err := printer.NewPrinterFromCommand(cmd)
			if err != nil {
				return err
			}
			return runCreateCmd(&params, p)
		},
	}

	// Define parameters to keep
	createCmd.Flags().StringVar(&flagPath, "output_path", ".",
		"(optional) Path to the Service.")

	createCmd.Flags().StringVar(&flagProtoPackage, "proto_package", "",
		"(optional) Proto package for the Service parameter proto.")

	createCmd.Flags().StringVar(&flagLanguage, "language", "",
		"Implementation language to generate (cpp, python).")
	createCmd.MarkFlagRequired("language")

	createCmd.Flags().BoolVar(&flagDryRun, "dry_run", false, "(optional) If set, no files will be "+
		"created or modified.")

	return createCmd
}
