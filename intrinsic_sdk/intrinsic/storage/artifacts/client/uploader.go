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

// Package client contains client helpers for ArtifactsApiService
package client

import (
	"context"
	"fmt"
	"time"

	log "github.com/golang/glog"
	"github.com/google/go-containerregistry/pkg/name"
	crv1 "github.com/google/go-containerregistry/pkg/v1"
	"github.com/google/go-containerregistry/pkg/v1/types"
	"github.com/pborman/uuid"
	"golang.org/x/sync/errgroup"
	"golang.org/x/sync/singleflight"
	"google.golang.org/grpc/metadata"

	artifactgrpcpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
	artifactpb "intrinsic/storage/artifacts/proto/v1/artifact_go_proto"
)

const (
	extraHeaderIntrinsicClientID = "intrinsic-client-id"
)

// UploaderOption is an option for the Uploader.
type UploaderOption func(options uploaderOptions) uploaderOptions

type uploaderOptions struct {
	maxNumberOfUploads int
	maxCheckWaitTime   time.Duration
	strategy           uploadStrategy
	alignForK8s        bool
	clientIDHeader     string
}

var defaultOptions = uploaderOptions{
	maxNumberOfUploads: 4,
	maxCheckWaitTime:   5 * time.Minute,
	strategy:           nonStreamingUpload,
	alignForK8s:        true,
}

// WithUploadParallelism sets the maximum number of parallel uploads.
func WithUploadParallelism(value int) UploaderOption {
	return func(options uploaderOptions) uploaderOptions {
		options.maxNumberOfUploads = value
		return options
	}
}

// WithMaxCheckWaitTime sets the maximum time various availability checks
// will wait before failing
func WithMaxCheckWaitTime(value time.Duration) UploaderOption {
	return func(options uploaderOptions) uploaderOptions {
		options.maxCheckWaitTime = value
		return options
	}
}

// WithStreamingUpload option instructs uploader to use streaming type
// tasks. While more performant, this type of tasks may run into various
// reverse-proxy upload limits. Looking at you cloud-relay nginx ingress!
func WithStreamingUpload() UploaderOption {
	return func(options uploaderOptions) uploaderOptions {
		options.strategy = streamingUpload
		return options
	}
}

// WithSequentialUpload option instructs uploader to use non-streaming type
// of tasks. This is currently default as we have issues with nginx streaming
// configuration
func WithSequentialUpload() UploaderOption {
	return func(options uploaderOptions) uploaderOptions {
		options.strategy = nonStreamingUpload
		return options
	}
}

// WithAlignForK8sDeployments controls if uploader will align manifest names
// with K8s deployment needs. In order for K8s to correctly load images
// locally, we need to create several extra indexes for manifests.
// Default value is true.
func WithAlignForK8sDeployments(align bool) UploaderOption {
	return func(options uploaderOptions) uploaderOptions {
		options.alignForK8s = align
		return options
	}
}

// Uploader is an interface for the Artifact Service client.
type Uploader interface {
	// UploadImage reads provided image object and uploads required components.
	//
	// Returns error on any failure
	UploadImage(ctx context.Context, imageName string, image crv1.Image) error
}

// NewUploader creates a new Uploader.
func NewUploader(client artifactgrpcpb.ArtifactServiceApiClient, opts ...UploaderOption) (Uploader, error) {
	options := defaultOptions
	for _, opt := range opts {
		options = opt(options)
	}
	// client ID for ingress affinity calculations, collisions are ok.
	options.clientIDHeader = uuid.New()

	return &defaultHelper{uploaderOptions: options, client: client}, nil
}

type defaultHelper struct {
	uploaderOptions
	client      artifactgrpcpb.ArtifactServiceApiClient
	uploadGroup singleflight.Group
}

func (h *defaultHelper) uploadBlob(ctx context.Context, named namedObject, reader itemReader, maxUpdateSize int32) error {
	ref, err := named.Name()
	if err != nil {
		return err
	}
	_, err, _ = h.uploadGroup.Do(ref, func() (any, error) {
		task, err := newTask(ctx, h.strategy, h.client, maxUpdateSize, named, reader)
		if err != nil {
			return nil, err
		}
		return nil, task.runWithCtx(ctx)
	})
	return err
}

func (h *defaultHelper) UploadImage(ctx context.Context, imageName string, image crv1.Image) error {
	imageManifest, err := toImageManifest(image)
	if err != nil {
		return fmt.Errorf("error reading image: %w", err)
	}

	request := &artifactpb.ImageRequest{
		Name:     imageName,
		Manifest: imageManifest,
	}
	response, err := h.client.CheckImage(ctx, request)
	if err != nil {
		return fmt.Errorf("check image failed: %w", err)
	}

	// Per image session header. This is used by server to manage uploaders isolation.
	// If this client is concurrently uploading multiple images with same layers,
	// we can still run into locking issue in the server writer.
	// See go/insrc/pull/50844 for more details.
	ctx = metadata.AppendToOutgoingContext(ctx, extraHeaderIntrinsicClientID, h.clientIDHeader+"/"+uuid.New())
	ctx = attachImageName(ctx, imageName)
	return h.uploadImageParts(ctx, image, response)
}

func (h *defaultHelper) uploadImageParts(ctx context.Context, image crv1.Image, response *artifactpb.ArtifactResponse) error {
	taskGroup, _ := errgroup.WithContext(ctx)
	taskGroup.SetLimit(h.maxNumberOfUploads)

	log.InfoContextf(ctx, "uploading image %s to upstream...", response.Ref)
	missingRefs := asRefMap(response.MissingRefs...)
	presentRefs := asRefMap(response.PresentRefs...)
	maxUpdateSize := response.MaxUpdateSize
	// if check image returns image name as missing reference, we need
	// to upload everything.
	imgDigest, err := image.Digest()
	if err != nil {
		return fmt.Errorf("system error: %w", err)
	}

	fullUpload := false

	responseDigest := valueOrDefault(response.Digest, "")
	if responseDigest != imgDigest.String() {
		// the image digest does not match what we expect, we are doing full upload
		// server was not able to determine state of our image. see b/327799134
		fullUpload = true
	}
	if len(missingRefs) > 0 || fullUpload {

		fullUpload = fullUpload || (len(missingRefs) == 1 && response.MissingRefs[0] == imgDigest.String())

		// we are missing some references, let's start by uploading layers
		layers, err := image.Layers()
		if err != nil {
			return fmt.Errorf("cannot read layers: %w", err)
		}

		for _, layer := range layers {

			reader, digest, err := getLayerReader(layer)
			if err != nil {
				return err
			}

			_, isPresent := presentRefs[digest.String()] // see b/327799134
			if _, missing := missingRefs[digest.String()]; missing ||
				(fullUpload && !isPresent) { // if present, we are going to skip
				mediaType, _ := layer.MediaType()
				log.InfoContextf(ctx, "uploading layer %s (%s) ", digest, mediaType)
				delete(missingRefs, digest.String())
				namedLayer := asDigestNamed(layer)
				taskGroup.Go(func() error {
					return h.uploadBlob(ctx, namedLayer, reader, maxUpdateSize)
				})
			}
		}

		// upload container config
		manifest, err := image.Manifest()
		if err != nil {
			return err
		}
		configRef := manifest.Config.Digest.String()
		if _, missing := missingRefs[configRef]; missing || fullUpload {
			log.InfoContextf(ctx, "uploading config for %s: ", response.Ref)
			delete(missingRefs, configRef)
			namedConfig := asDigestNamed(descWrap{value: manifest.Config})
			taskGroup.Go(func() error {
				return h.uploadBlob(ctx, namedConfig, bytesReader(image.RawConfigFile), maxUpdateSize)
			})
		}
	}

	if err := taskGroup.Wait(); err != nil {
		return fmt.Errorf("error uploading image: %w", err)
	}

	// this needs to be last step to tie together all previously uploaded blobs under image name
	log.InfoContextf(ctx, "uploading manifest for %s", response.Ref)
	manifestNames := []namedObject{asSimplyNamed(response.Ref, image)}
	if h.alignForK8s {
		// In order to allow k8s to identify locally sourced image correctly
		// we need to store manifest under few alternative names...
		manifestNames = append(manifestNames,
			asDigestNamed(image),
			asSimplyNamed(getDigestReference(response.Ref, imgDigest.String()), image))
	}

	for _, named := range manifestNames {
		if err := h.uploadBlob(ctx, named, bytesReader(image.RawManifest), maxUpdateSize); err != nil {
			reference, _ := named.Name()
			return fmt.Errorf("error uploading manifest (%s): %w", reference, err)
		}
	}

	return nil
}

func getLayerReader(layer crv1.Layer) (itemReader, crv1.Hash, error) {
	mt, err := layer.MediaType()
	if err != nil {
		return nil, crv1.Hash{}, err
	}
	if !mt.IsLayer() {
		return nil, crv1.Hash{}, fmt.Errorf("object is not a layer: %s", mt)
	}
	if mt == types.OCIUncompressedLayer || mt == types.DockerUncompressedLayer {
		digest, err := layer.DiffID()
		return layer.Uncompressed, digest, err
	}

	digest, err := layer.Digest()
	return layer.Compressed, digest, err
}

func getDigestReference(imgName string, digest string) string {
	reference, err := name.ParseReference(imgName, name.WeakValidation)
	if err != nil {
		panic(fmt.Errorf("cannot parse reference, this is programmer error: %w", err))
	}

	return reference.Context().Digest(digest).Name()
}

func asRefMap(refs ...string) map[string]string {
	result := make(map[string]string, len(refs))

	for _, ref := range refs {
		result[ref] = ""
	}

	return result
}

func toImageManifest(image crv1.Image) (*artifactpb.ImageManifest, error) {
	manifest, err := image.Manifest()
	if err != nil {
		return nil, err
	}

	hash, err := image.Digest()
	if err != nil {
		return nil, err
	}

	size, err := image.Size()
	if err != nil {
		return nil, err
	}

	data, err := image.RawManifest()
	if err != nil {
		return nil, err
	}

	config, err := image.ConfigFile()
	if err != nil {
		return nil, err
	}

	ociDescriptor := &artifactpb.ImageManifest{
		MediaType:   string(manifest.MediaType),
		Digest:      hash.String(),
		Size:        size,
		Data:        data,
		Annotations: manifest.Annotations,
	}

	if platform := config.Platform(); platform != nil {
		platformStr := platform.String()
		ociDescriptor.Platform = &platformStr
	}

	return ociDescriptor, nil
}
