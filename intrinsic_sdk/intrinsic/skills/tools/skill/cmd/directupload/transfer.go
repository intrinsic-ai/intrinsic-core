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

package directupload

import (
	"context"
	"fmt"
	"io"

	"intrinsic/assets/imagetransfer"
	"intrinsic/storage/artifacts/client/client"

	backoff "github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"github.com/google/go-containerregistry/pkg/name"
	crv1 "github.com/google/go-containerregistry/pkg/v1"
	"github.com/pkg/errors"
	"go.uber.org/atomic"
	"golang.org/x/sync/singleflight"

	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"
	artifactgrpcpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

// Option allows setting direct upload transferer options.
type Option func(transfer *directTransfer)

// WithMaxRetries allows setting max retries for the upload
func WithMaxRetries(maxRetries int) Option {
	return func(transfer *directTransfer) {
		transfer.maxRetries = maxRetries
	}
}

// WithCatalogOptions configures meaningful defaults for transferrer towards Catalog.
func WithCatalogOptions(parallelism int) Option {
	return func(transfer *directTransfer) {
		transfer.uploadType = client.WithStreamingUpload()
		if parallelism < 1 {
			parallelism = 1 // we do need at least ONE stream.
		}
		if parallelism > 10 {
			parallelism = 10 // upper bound for single client.
		}
		transfer.parallelism = parallelism
	}
}

// WithClient allows caller to set client side implementation. If this option
// is specified, the client will be used to create an uploader instance,
// ignoring discovery strategy set by WithDiscovery
func WithClient(client artifactgrpcpb.ArtifactServiceApiClient) Option {
	return func(transfer *directTransfer) {
		transfer.client = client
	}
}

// WithOutput allows adding simple progress monitor with w as its output.
func WithOutput(w io.Writer) Option {
	return func(transfer *directTransfer) {
		transfer.writer = w
	}
}

// WithDiscovery allows setting a TargetDiscovery implementation to discover
// the most suitable client path. One of WithClient or WithDiscovery have to be
// used in order to specify upload target.
func WithDiscovery(discovery TargetDiscovery) Option {
	return func(transfer *directTransfer) {
		transfer.discovery = discovery
	}
}

// WithFailOver allows to set fail-over transferer in case direct upload
// is not possible.
func WithFailOver(failOver imagetransfer.Transferer) Option {
	return func(transfer *directTransfer) {
		transfer.failOver = failOver
	}
}

// WithRegistry allows setting the registry for the direct upload.
func WithRegistry(registry string) Option {
	return func(transfer *directTransfer) {
		if registry != "" {
			transfer.registry = registry
		}
	}
}

const directUploadRegistry = "localhost:17127"

// NewTransferer create a new instance of direct upload Transferer implementation
// and applies options if specified.
func NewTransferer(opts ...Option) imagetransfer.Transferer {
	transfer := &directTransfer{
		maxRetries: 5,
		// TODO(@rkomara): As a result of b/330747118, re-evaluate if it is viable to
		// increase this number.
		parallelism: 1,
		uploadType:  client.WithSequentialUpload(),
		registry:    directUploadRegistry,
	}

	for _, opt := range opts {
		opt(transfer)
	}

	if transfer.client == nil {
		if transfer.discovery == nil {
			// this is programmer error...
			panic("cannot obtain client, use WithDiscovery or WithClient options")
		}
	}

	return transfer
}

type directTransfer struct {
	maxRetries  int
	failOver    imagetransfer.Transferer
	uploader    client.Uploader
	client      artifactgrpcpb.ArtifactServiceApiClient
	discovery   TargetDiscovery
	writer      io.Writer
	parallelism int
	uploadType  client.UploaderOption
	registry    string
	uploadGroup singleflight.Group
}

func (dt *directTransfer) Write(ctx context.Context, nameStr string, tag string, img crv1.Image) (*ipb.Image, error) {
	dst := fmt.Sprintf("%s/%s:%s", dt.registry, nameStr, tag)
	res, err, _ := dt.uploadGroup.Do(dst, func() (any, error) {
		return dt.writeInternal(ctx, dst, nameStr, tag, img)
	})
	if err != nil {
		return nil, err
	}
	return res.(*ipb.Image), nil
}

func (dt *directTransfer) writeInternal(ctx context.Context, dst string, nameStr string, tag string, img crv1.Image) (*ipb.Image, error) {
	ref, err := name.NewTag(dst)
	if err != nil {
		return nil, fmt.Errorf("name.NewTag(%q): %w", dst, err)
	}

	if dt.writer != nil {
		ctx = client.SetProgressMonitor(ctx, newMonitor(dt.writer))
	}
	if dt.uploader == nil {
		apiClient, err := dt.getClient(ctx)
		if err != nil {
			return nil, fmt.Errorf("cannot connect: %w", err)
		}
		dt.uploader, err = client.NewUploader(apiClient, dt.uploadType, client.WithUploadParallelism(dt.parallelism))
		if err != nil {
			return nil, fmt.Errorf("cannot create uploader: %w", err)
		}
	}

	numAttempts := atomic.NewUint32(0)
	// The initial attempt is not counted as a retry.
	maxAttempts := 1 + dt.maxRetries
	err = backoff.Retry(func() error {
		if ctx.Err() != nil {
			return backoff.Permanent(ctx.Err())
		}
		attempt := numAttempts.Inc()
		err := dt.uploader.UploadImage(ctx, ref.String(), img)
		if err != nil {
			log.Errorf("attempt %d/%d: failed to upload image (%s): %s", attempt, maxAttempts, ref, err)
			if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
				return backoff.Permanent(err)
			}
			// todo: evaluate other permanent errors, such as 500
		}
		return err
	}, backoff.WithMaxRetries(backoff.NewExponentialBackOff(), uint64(dt.maxRetries)))
	if err != nil {
		if dt.failOver != nil {
			res, foErr := dt.failOver.Write(ctx, nameStr, tag, img)
			if foErr != nil {
				return nil, fmt.Errorf("image write failed (direct: %s): %w", err, foErr)
			}
			log.WarningContextf(ctx, "fail over succeeded with prior direct upload failure: %s", err)
			return res, nil
		}
		return nil, fmt.Errorf("image write failed: %w", err)
	}

	digest, err := img.Digest()
	if err != nil {
		return nil, fmt.Errorf("could not get image digest: %w", err)
	}

	return &ipb.Image{
		Registry: dt.registry,
		Name:     nameStr,
		Tag:      "@" + digest.String(),
	}, nil
}

func (dt *directTransfer) getClient(ctx context.Context) (artifactgrpcpb.ArtifactServiceApiClient, error) {
	if dt.client != nil {
		return dt.client, nil
	}

	apiClient, err := dt.discovery.GetClient(ctx)
	if err != nil {
		return nil, err
	}

	dt.client = apiClient
	return dt.client, nil
}
