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

// Package handlers handles the Handler for the /openapi.yaml endpoint.
package handlers

import (
	"log/slog"
	"net/http"
	"os"

	"github.com/bazelbuild/rules_go/go/runfiles"
	"github.com/grpc-ecosystem/grpc-gateway/v2/runtime"
)

// MakeOpenAPIHandlerFromRunfiles initializes a runfiles instance with the caller's repo
// context and resolves the rlocation before passing it to the path-based constructor.
func MakeOpenAPIHandlerFromRunfiles(rlocationPath string) (runtime.HandlerFunc, error) {
	slog.Info("Creating runfiles object for caller repository", "repository", runfiles.CallerRepository())
	// Initialize runfiles with SourceRepo set to the caller's repository.
	// This ensures correct repo-mapping resolution in Bzlmod environments.
	rf, err := runfiles.New(runfiles.SourceRepo(runfiles.CallerRepository()))
	if err != nil {
		slog.Error("Failed to initialize runfiles: %v", err)
		return nil, err
	}

	absPath, err := rf.Rlocation(rlocationPath)
	if err != nil {
		slog.Error("Failed to resolve rlocation %q: %v", rlocationPath, err)
		return nil, err
	}

	return MakeOpenAPIHandlerFromPath(absPath), nil
}

// MakeOpenAPIHandlerFromPath reads the file from the filesystem and passes
// the content to the content-based constructor.
func MakeOpenAPIHandlerFromPath(path string) runtime.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request, pathParams map[string]string) {
		content, err := os.ReadFile(path)
		if err != nil {
			slog.Error("Failed to read file at %q: %v", path, err)
			http.Error(w, "Failed to read specification", http.StatusInternalServerError)
			return
		}

		MakeOpenAPIHandlerFromContent(string(content))(w, r, pathParams)
	}
}

// MakeOpenAPIHandlerFromContent returns a handler that serves the provided string as YAML.
func MakeOpenAPIHandlerFromContent(content string) runtime.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request, _ map[string]string) {
		w.Header().Set("Content-Type", "application/x-yaml")
		w.WriteHeader(http.StatusOK)
		if _, err := w.Write([]byte(content)); err != nil {
			slog.Error("Failed to write OpenAPI content to response: %v", err)
		}
	}
}
