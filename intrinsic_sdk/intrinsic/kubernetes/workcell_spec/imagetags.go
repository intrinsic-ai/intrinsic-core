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

// Package imagetags contains logic to generate default tags for container images.
package imagetags

import (
	"os"
	"os/user"

	"github.com/pborman/uuid"
	"github.com/pkg/errors"
)

const (
	// DevPrefix is the prefix for dev images tags.
	DevPrefix                = "dev."
	intrinsicDevContainerEnv = "INTRINSIC_DEV_CONTAINER"
	userVSCode               = "vscode"
	userCodespaces           = "codespaces"
	dockerMarkerFile         = "/.dockerenv"
	termProgramEnv           = "TERM_PROGRAM"
	termProgramEnvValue      = "vscode"
)

// ReleaseCandidateTag returns an image tag derived from the proper candidate env variable.
func ReleaseCandidateTag() (string, bool) {
	tag := os.Getenv("RELEASE_CANDIDATE_NAME")
	return tag, tag != ""
}

// DefaultTag generates a tag for container images.
func DefaultTag() (string, error) {
	if tag, ok := ReleaseCandidateTag(); ok {
		return tag, nil
	}
	user, err := user.Current()
	if err != nil {
		return "", errors.Wrapf(err, "getting current user")
	}
	result := DevPrefix + user.Username

	if isIntrinsicDevContainer(user.Username) {
		result += "-" + uuid.NewRandom().String()[:8]
	}

	return result, nil
}

func isIntrinsicDevContainer(username string) bool {
	// If this is set, we are sure we have intrinsic dev container
	// we do not care about value
	if _, exists := os.LookupEnv(intrinsicDevContainerEnv); exists {
		return true
	}
	// Even if we do not have dev container marker, we will try to guess if we are running
	// in container by any chance. This is mostly temporary fallback
	mayBeContainer := username == userVSCode || username == userCodespaces ||
		os.Getenv(termProgramEnv) == termProgramEnvValue
	if mayBeContainer {
		// For caveat see https://superuser.com/questions/1021834/what-are-dockerenv-and-dockerinit
		_, err := os.Stat(dockerMarkerFile)
		return err == nil
	}
	// We are not in dev container or we cannot determine the environment.
	return false
}
