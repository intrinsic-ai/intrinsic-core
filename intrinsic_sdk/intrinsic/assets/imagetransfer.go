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

// Package imagetransfer contains a Transferer interface and implementation
// that reads and writes images to a container registry.
package imagetransfer

import (
	"context"
	"fmt"
	"strings"

	backoff "github.com/cenkalti/backoff/v4"
	"github.com/google/go-containerregistry/pkg/authn"
	"github.com/google/go-containerregistry/pkg/name"
	containerregistry "github.com/google/go-containerregistry/pkg/v1"
	"github.com/google/go-containerregistry/pkg/v1/google"
	"github.com/google/go-containerregistry/pkg/v1/remote"
	"github.com/google/go-containerregistry/pkg/v1/remote/transport"
	"github.com/pkg/errors"

	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"
)

// Number of times to try uploading a container image if we get retriable errors.
const remoteWriteTries = 5

// Transferer provides methods to read and write images to a container registry.
type Transferer interface {
	Write(ctx context.Context, imageName string, tag string, img containerregistry.Image) (*ipb.Image, error)
}

type remoteImage struct {
	registry string
	authUser string
	authPwd  string
	Opts     []remote.Option
}

// Write pushes an image to a container registry.
func (r remoteImage) Write(ctx context.Context, imageName string, tag string, img containerregistry.Image) (*ipb.Image, error) {
	dst := fmt.Sprintf("%s/%s:%s", r.registry, imageName, tag)
	ref, err := name.NewTag(dst)
	if err != nil {
		return nil, errors.Wrapf(err, "name.NewTag(%q)", dst)
	}

	opts := append(r.Opts, remote.WithContext(ctx))
	b := backoff.WithMaxRetries(backoff.NewExponentialBackOff(), remoteWriteTries)
	if err := backoff.Retry(func() error {
		err := remote.Write(ref, img, opts...)
		if err, ok := err.(*transport.Error); ok && err.StatusCode >= 500 {
			// Retry server errors like 504 Gateway Timeout.
			return err
		}
		if err != nil {
			// TODO - https://github.com/golang/go/issues/74578: use errors.As()
			if strings.Contains(err.Error(), "server sent GOAWAY and closed the connection") {
				return err
			}
			return backoff.Permanent(err)
		}
		return nil
	}, b); err != nil {
		return nil, errors.Wrapf(err, "remote.Write to %q", ref)
	}

	digest, err := img.Digest()
	if err != nil {
		return nil, errors.Wrap(err, "getting image digest")
	}

	return &ipb.Image{
		Registry:     r.registry,
		Name:         imageName,
		Tag:          "@" + digest.String(),
		AuthUser:     r.authUser,
		AuthPassword: r.authPwd,
	}, nil
}

// RemoteTransferer returns a new Transferer using the passed-in options.
func RemoteTransferer(registry string, user string, pwd string, opts ...remote.Option) Transferer {
	if user != "" && pwd != "" {
		opts = append(opts, remote.WithAuth(authn.FromConfig(authn.AuthConfig{
			Username: user,
			Password: pwd,
		})))
	} else {
		opts = append(opts, remote.WithAuthFromKeychain(google.Keychain))
	}
	return remoteImage{
		registry: registry,
		authUser: user,
		authPwd:  pwd,
		Opts:     opts,
	}
}

// NoOpTransferer does nothing and returns a success when called.
type NoOpTransferer struct {
	RegistryURI string
}

func (n NoOpTransferer) Write(ctx context.Context, name, tag string, img containerregistry.Image) (*ipb.Image, error) {
	digest, err := img.Digest()
	if err != nil {
		return nil, fmt.Errorf("could not get digest: %w", err)
	}
	registry := n.RegistryURI
	if registry == "" {
		registry = "fake.io"
	}
	return &ipb.Image{
		Registry: registry,
		Name:     name,
		Tag:      "@" + digest.String(),
	}, nil
}
