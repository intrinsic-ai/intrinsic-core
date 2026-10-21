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

// Package solutionutil provides helper functions for resolving clusters from solution names
package solutionutil

import (
	"context"
	"errors"
	"fmt"

	"intrinsic/tools/inctl/cmd/solution/get/get"

	"google.golang.org/grpc"

	clusterdiscoverypb "intrinsic/frontend/cloud/api/v1/clusterdiscovery_api_go_proto"
)

// GetClusterNameFromSolution returns the cluster in which a solution currently runs.
func GetClusterNameFromSolution(ctx context.Context, conn *grpc.ClientConn, solutionName string) (string, error) {
	solution, err := get.GetSolution(ctx, conn, solutionName)
	if err != nil {
		return "", fmt.Errorf("failed to get solution: %w", err)
	}
	if solution.GetState() == clusterdiscoverypb.SolutionState_SOLUTION_STATE_NOT_RUNNING {
		return "", fmt.Errorf("solution is not running")
	}
	if solution.GetClusterName() == "" {
		return "", fmt.Errorf("unknown error: solution is running but cluster is empty")
	}
	return solution.GetClusterName(), nil
}

// GetClusterNameFromSolutionOrDefault checks if solutionName is set and resolves it to cluster
// return default otherwise.
func GetClusterNameFromSolutionOrDefault(ctx context.Context, conn *grpc.ClientConn, solutionName string, defaultCluster string) (string, error) {
	if solutionName != "" {
		cluster, err := GetClusterNameFromSolution(ctx, conn, solutionName)
		if err != nil {
			return "", fmt.Errorf("could not resolve context from solution '%s'"+
				"(please check if the solution is currently running): %w", solutionName, err)
		}
		return cluster, nil
	}
	if defaultCluster == "" {
		return "", errors.New("solution name and default cluster are empty - set exactly one of them")
	}

	return defaultCluster, nil
}
