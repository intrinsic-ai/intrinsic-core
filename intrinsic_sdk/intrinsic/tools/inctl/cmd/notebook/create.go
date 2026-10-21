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

package notebook

import (
	"embed"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"text/template"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/tools/inctl/util/templateutil"

	"github.com/spf13/cobra"
)

var (
	flagTargetPath string
	flagDryRun     bool
)

type cmdParams struct {
	NotebookName string
	TargetPath   string
	DryRun       bool
}

// SuccessMessage is printed to stdout after a successful run. This struct is
// currently the single source of truth for the JSON format that we use in case
// of "--output=json".
type SuccessMessage struct {
	TargetPath    string   `json:"targetPath,omitempty"`
	AffectedFiles []string `json:"affectedFiles,omitempty"`
	WasDryRun     bool     `json:"wasDryRun,omitempty"`
}

// String prints a successMessage in the case of --output=text.
func (msg *SuccessMessage) String() string {
	var result strings.Builder
	if msg.WasDryRun {
		result.WriteString("Will create:")
	} else {
		result.WriteString("Created:")
	}
	for _, affectedFile := range msg.AffectedFiles {
		result.WriteByte('\n')
		result.WriteString(filepath.Join(msg.TargetPath, affectedFile))
	}
	return result.String()
}

//go:embed templates/*
var embeddedTemplates embed.FS

// RunCreateCmd implements the notebook create command. It is the entry-point for
// unit tests and does not rely on any global state (e.g. global flag
// variables).
func RunCreateCmd(params *cmdParams) (SuccessMessage, error) {
	notebookFileName := params.NotebookName + ".ipynb"

	templateSet, err := template.New("").ParseFS(embeddedTemplates, "templates/*.template")
	if err != nil {
		return SuccessMessage{}, fmt.Errorf("parsing templates: %w", err)
	}

	fullPath := filepath.Join(params.TargetPath, notebookFileName)
	if params.DryRun {
		_, err := os.Stat(fullPath)
		if err == nil {
			return SuccessMessage{}, fmt.Errorf("will fail, file %s already exists", fullPath)
		}
	} else {
		err = templateutil.CreateNewFileFromTemplate(fullPath, "empty_notebook.ipynb.template", nil, templateSet, templateutil.CreateFileOptions{})
		if err != nil {
			return SuccessMessage{}, fmt.Errorf("creating %s: %w", fullPath, err)
		}
	}
	return SuccessMessage{TargetPath: params.TargetPath, AffectedFiles: []string{notebookFileName}, WasDryRun: params.DryRun}, nil
}

var createCmd = &cobra.Command{
	Use:   "create notebook_name",
	Short: "Create notebook.",
	Long:  "Create a Jupyter Notebook that uses the Solution Building Library (SBL).",
	Example: `Create a notebook called test_skill.ipynb in the current working directory:
$ inctl notebook create test_skill`,
	Args:       cobra.ExactArgs(1),
	ArgAliases: []string{"notebook_name"},
	RunE: func(cmd *cobra.Command, argsArray []string) error {
		params := cmdParams{
			NotebookName: argsArray[0],
			TargetPath:   flagTargetPath,
			DryRun:       flagDryRun,
		}
		successMsg, err := RunCreateCmd(&params)
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
	createCmd.Flags().StringVar(&flagTargetPath, "path", "", "(optional) Absolute path to a folder to create the file in.")
	createCmd.Flags().BoolVar(&flagDryRun, "dry_run", false, "(optional) If set, no file will be created.")
	notebookCmd.AddCommand(createCmd)
}
