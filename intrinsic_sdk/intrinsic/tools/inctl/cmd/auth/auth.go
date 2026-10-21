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

// Package auth provides authorization client and client side library.
package auth

import (
	"bufio"
	"strings"

	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"
)

const (
	keyProjectShort = "p"
	keyAlias        = "alias"
	keyBatch        = "batch"
)

// Exposed for testing
var (
	authStore      = auth.NewStore()
	checkOrgExists = true
)

func setPrinterFromOutputFlag(command *cobra.Command, args []string) (err error) {
	if out, err := printer.NewPrinter(root.FlagOutput); err == nil {
		command.SetOut(out)
	}
	return
}

// AuthCmd is the `inctl auth` command.
var AuthCmd = &cobra.Command{
	Use:   "auth",
	Short: "Manages user authorization",
	Long:  "Manages user authorization for accessing solutions in the project.",
	// Catching common typos and potential alternatives
	SuggestFor:        []string{"ath", "uath", "auht", "user", "credentials"},
	PersistentPreRunE: setPrinterFromOutputFlag,
}

func userPrompt(rw *bufio.ReadWriter, prompt string, defaultOption int, options ...string) (string, error) {
	if len(options) > 0 {
		prompt += " [" + strings.Join(options, "/") + "]"
	} else {
		defaultOption = -1 // we just mark options as no default just in case here
	}
	prompt += ": "
	if _, err := rw.WriteString(prompt); err != nil {
		return "", err
	}
	rw.Flush() // print out buffer content before we request user input

	response, err := rw.ReadString('\n')
	if err != nil {
		return "", err
	}
	response = strings.TrimSpace(response)
	if response == "" && defaultOption > -1 {
		response = options[defaultOption]
	}
	return response, nil
}

func newReadWriterForCmd(cmd *cobra.Command) *bufio.ReadWriter {
	return bufio.NewReadWriter(
		bufio.NewReader(cmd.InOrStdin()),
		bufio.NewWriter(cmd.OutOrStdout()))
}

func init() {
	root.RootCmd.AddCommand(AuthCmd)
}
