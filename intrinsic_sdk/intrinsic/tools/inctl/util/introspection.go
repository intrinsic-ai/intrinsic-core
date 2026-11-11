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

// Package introspection contains helper functions for providing introspection
// capabilities.
package introspection

import (
	"fmt"
)

const (
	cloudTraceURLTemplate = "https://console.cloud.google.com/traces/explorer;traceId=%s?project=%s"
)

// CloudTraceURL returns a URL to the CloudTrace UI for the given trace ID.
func CloudTraceURL(traceID, project string) string {
	return fmt.Sprintf(cloudTraceURLTemplate, traceID, project)
}
