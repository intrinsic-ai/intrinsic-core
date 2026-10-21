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

// Package promptutil provides shared utilities for CLI interactive prompts.
package promptutil

import (
	"bufio"
	"fmt"
	"io"
	"unicode"

	"intrinsic/tools/inctl/util/color"

	"github.com/spf13/cobra"
)

// InvalidInputBehavior configures what happens when a user types an invalid response.
type InvalidInputBehavior int

const (
	// ReemitPromptOnInvalidInput prompts the user again if they input an invalid character.
	ReemitPromptOnInvalidInput InvalidInputBehavior = iota
	// EmitErrorOnInvalidInput returns an error if they input an invalid character.
	EmitErrorOnInvalidInput
)

// DefaultBehavior configures the default answer if the user inputs nothing (e.g. hits Enter).
type DefaultBehavior int

const (
	// DefaultYes makes yes the default answer.
	DefaultYes DefaultBehavior = iota
	// DefaultNo makes no the default answer.
	DefaultNo
	// NoDefault means there is no default. Empty input is considered invalid.
	NoDefault
)

// PromptYesNo prints the prompt appending [Y/n], [y/N], or [y/n] depending on defaultBehavior,
// and waits for a user to input. It handles the defaults automatically if the user hits Enter.
// If an invalid character is entered, it handles it according to the provided invalidInputBehavior.
func PromptYesNo(cmd *cobra.Command, prompt string, defaultBehavior DefaultBehavior, invalidInputBehavior InvalidInputBehavior) (bool, error) {
	prompt = appendDefaultBehaviorSuffix(prompt, defaultBehavior)

	reader := bufio.NewReader(cmd.InOrStdin())
	for {
		color.C.Yellow().Fprintf(cmd.OutOrStdout(), "%s", prompt)

		input, _, err := reader.ReadRune()
		if err != nil && err != io.EOF {
			return false, err
		}

		switch input {
		case '\n', '\r', 0:
			if handled, result := handleEmptyInput(defaultBehavior); handled {
				return result, nil
			}
		default:
			switch unicode.ToLower(input) {
			case 'y':
				return true, nil
			case 'n':
				return false, nil
			}
		}

		if err := handleInvalidInput(invalidInputBehavior, input); err != nil {
			return false, err
		}

		// Consume the rest of the line before reprompting
		if input != '\n' && input != '\r' && input != 0 {
			_, _ = reader.ReadString('\n')
		}
	}
}

// appendDefaultBehaviorSuffix appends the indicator suffix (e.g. " [Y/n] ") including
// a trailing space to ensure user input doesn't run into the prompt text.
func appendDefaultBehaviorSuffix(prompt string, defaultBehavior DefaultBehavior) string {
	switch defaultBehavior {
	case DefaultYes:
		return prompt + " [Y/n] "
	case DefaultNo:
		return prompt + " [y/N] "
	case NoDefault:
		fallthrough
	default:
		return prompt + " [y/n] "
	}
}

// handleEmptyInput routes empty input (like Enter key) to the requested default behavior natively.
func handleEmptyInput(defaultBehavior DefaultBehavior) (bool, bool) {
	switch defaultBehavior {
	case DefaultYes:
		return true, true
	case DefaultNo:
		return true, false
	}
	return false, false
}

// handleInvalidInput acts based on the configuration when the user types an invalid response.
func handleInvalidInput(behavior InvalidInputBehavior, input rune) error {
	switch behavior {
	case EmitErrorOnInvalidInput:
		return fmt.Errorf("invalid input: expected 'y' or 'n', got %q", input)
	case ReemitPromptOnInvalidInput:
		// ReemitPromptOnInvalidInput will just loop again
		return nil
	default:
		return nil
	}
}
