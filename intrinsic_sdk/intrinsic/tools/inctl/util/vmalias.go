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

// Package vmalias provides utilities for working with VM aliases.
package vmalias

import (
	"fmt" 
	"os"  
	"strings"

	"intrinsic/tools/inctl/util/color"       
	"intrinsic/tools/internal/k8sclientutil" 
)

// IsPoolVM returns true if the given name looks like a pool VM.
func IsPoolVM(name string) bool {
	return strings.HasPrefix(name, "vmp-")
}

// ResolveResult contains the result of a VM or alias resolution.
type ResolveResult struct {
	VM    string
	Alias string

	Project string

}

// Resolve resolves a VM or alias. Returns the (project, VM) name.
func Resolve(vmOrAlias string) (ResolveResult, error) {
	ret := ResolveResult{
		VM:    vmOrAlias,
		Alias: vmOrAlias,
	}


	project, vm, err := resolve(vmOrAlias)
	if err != nil {
		return ResolveResult{}, err
	}
	ret.Project = project
	ret.VM = vm


	return ret, nil
}

// ResolvePrint resolves a VM or alias and prints a warning if the alias does not resolve to the
// expected project. expectedProject may be empty, in which case no warning is printed.
func ResolvePrint(vmOrAlias, expectedProject string) string {
	if IsPoolVM(vmOrAlias) {
		return vmOrAlias
	}
	retVM := vmOrAlias

	aliasProject, aliasVM, err := resolve(vmOrAlias)
	if err != nil {
		// show no warning, resolve always fails for non-pool VMs (physical clusters)
		return retVM
	}
	// print warnings on project mismatch
	if expectedProject != "" && aliasProject != expectedProject {
		color.C.Yellow().Fprintf(os.Stderr, "Warning: context alias %q resolved to project %q but you specified project %q\n", vmOrAlias, aliasProject, expectedProject)
	}
	// successful resolved alias
	if aliasVM != "" && aliasVM != vmOrAlias {
		color.C.Green().Fprintf(os.Stderr, "Using pool VM %q resolved from context alias %q.\n", aliasVM, vmOrAlias)
		retVM = aliasVM
	}

	return retVM
}



// Resolve tries to lookup a context alias set by lease. Returns the
// project and VM name (in that order). Returns an error if the context alias
// does not resolve to a pool VM.
func resolve(alias string) (string, string, error) {
	clusterProject, cluster, err := k8sclientutil.ResolveContext(alias)
	if err != nil {
		return "", "", fmt.Errorf("could not resolve context alias: %w", err)
	}
	if !IsPoolVM(cluster) {
		return "", "", fmt.Errorf("cluster from context alias %q is not a pool VM", cluster)
	}
	return clusterProject, cluster, nil
}


