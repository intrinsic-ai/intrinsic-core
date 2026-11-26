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

// Package internal contains shared constants and utilities for onprem_cas.
package internal

const (
	// PartialExt is the extension used for partial files during upload.
	PartialExt = ".partial"

	// SessionsSubdir is the subdirectory of the partial directory that stores resumable upload
	// sessions. The startup cleanup of partial files only scans the top level of the partial
	// directory, so sessions survive a restart.
	SessionsSubdir = "sessions"
	// SessionMetadataExt is the extension of the metadata file of a resumable upload session.
	SessionMetadataExt = ".metadata.json"
	// SessionTmpExt is appended to a session file name while the file is being replaced.
	SessionTmpExt = ".tmp"
)
