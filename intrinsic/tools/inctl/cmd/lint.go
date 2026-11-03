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

// Package lint lets users lint a chart assignment for common errors.
package lint

import (
	"context"
	"encoding/base64"
	"os"
	"os/exec"
	"strings"

	"dario.cat/mergo"
	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/pkg/errors"
	"go.opencensus.io/trace"
	kubeyaml "sigs.k8s.io/yaml"

	"intrinsic/tools/inctl/util/resolve"
	"intrinsic/util/go/set"
)

const (
	linterPath = "intrinsic-core/intrinsic/kubernetes/lint_wrapped"
)

func linterEnv() ([]string, error) {
	// Set runfiles environment variables to allow the linter to find its runfiles.
	r, err := runfiles.New()
	if err != nil {
		return nil, err
	}
	result := r.Env()

	forwardVars := set.New[string](
		"USER",
	)
	for _, env := range os.Environ() {
		envName := strings.SplitN(env, "=", 2)[0]
		if forwardVars.Contains(envName) {
			result = append(result, env)
		}
	}
	return result, nil
}

// Lint checks the helm chart to be deployed for syntax errors.
func Lint(ctx context.Context, kubeContext string, ca *v1alpha1.ChartAssignment, runtimeValues v1alpha1.ConfigValues) error {
	_, span := trace.StartSpan(ctx, "lint.Lint")
	defer span.End()

	chartContentBase64 := ca.Spec.Chart.Inline
	decodedContent, err := base64.StdEncoding.DecodeString(chartContentBase64)
	if err != nil {
		return errors.Wrap(err, "decode inlined chart")
	}
	chartFile, err := os.CreateTemp("", "lintchart.tgz")
	if err != nil {
		return errors.Wrap(err, "create temp file for chart")
	}
	defer os.Remove(chartFile.Name())
	if _, err := chartFile.Write(decodedContent); err != nil {
		return errors.Wrapf(err, "write chart to %q", chartFile.Name())
	}
	if err := chartFile.Close(); err != nil {
		return errors.Wrapf(err, "close %q", chartFile.Name())
	}

	// Merge runtime values with ca.Spec.Chart.Values as helm template linting
	// requires all values not in the helm chart tarball to be specified
	// explicitly. We make a copy to not modify ca.Spec.Chart.Values.
	lintValues := ca.Spec.Chart.Values.DeepCopy()
	if err := mergo.Merge(&lintValues, runtimeValues, mergo.WithOverride); err != nil {
		return errors.Wrap(err, "merging default with runtime values")
	}

	// Marshal and write out the values to a temp file.
	// We do this to support arbitrary complexity/nesting of values, which
	// is not supported by the --set flag for the `helm` CLI tool used by lint.sh.
	b, err := kubeyaml.Marshal(lintValues)
	if err != nil {
		return errors.Wrap(err, "marshalling values")
	}
	valuesFile, err := os.CreateTemp("", "lintvalues.yaml")
	if err != nil {
		return errors.Wrap(err, "create temp file for chart assignment values")
	}
	defer os.Remove(valuesFile.Name())
	if _, err := valuesFile.Write(b); err != nil {
		return errors.Wrapf(err, "write values to %q", valuesFile.Name())
	}
	if err := valuesFile.Close(); err != nil {
		return errors.Wrapf(err, "close %q", valuesFile.Name())
	}

	linter, err := resolve.Resolve(linterPath)
	if err != nil {
		return err
	}
	env, err := linterEnv()
	if err != nil {
		return err
	}
	cmd := exec.Command(linter, chartFile.Name(), kubeContext, valuesFile.Name())
	cmd.Env = env
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return errors.Wrap(err, "linting chart assignment")
	}
	return nil
}
