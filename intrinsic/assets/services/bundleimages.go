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

// Package bundleimages has utilities to push images from a resource bundle.
package bundleimages

import (
	"context"
	"fmt"
	"io"
	"path/filepath"
	"strings"

	"intrinsic/assets/idutils"
	"intrinsic/assets/imageutils"
	"intrinsic/assets/services/readeropener"
	"intrinsic/kubernetes/workcell_spec/imagetags"

	containerregistry "github.com/google/go-containerregistry/pkg/v1"
	"github.com/google/go-containerregistry/pkg/v1/tarball"
	"github.com/pkg/errors"

	idpb "intrinsic/assets/proto/id_go_proto"
	ipb "intrinsic/kubernetes/workcell_spec/proto/image_go_proto"
)

const (
	// maxInMemorySizeForPushArchive is set to a conservative 100MB for now.
	// Consider raising this value in the future if needed.
	maxInMemorySizeForPushArchive = 100 * 1024 * 1024
)

// CreateImageProcessor returns a closure to handle images within a bundle.  It
// pushes images to the registry using a default tag.  The image is named with
// the id of the resource with the basename image filename appended.
func CreateImageProcessor(transferer writer) imageutils.ImageProcessor {
	return func(ctx context.Context, idProto *idpb.Id, filename string, r io.Reader) (*ipb.Image, error) {
		id, err := idutils.IDFromProto(idProto)
		if err != nil {
			return nil, fmt.Errorf("unable to get tag for image: %v", err)
		}

		fileNoExt := strings.TrimSuffix(filepath.Base(filename), filepath.Ext(filename))
		name := fmt.Sprintf("%s.%s", id, fileNoExt)

		// Some images can be quite large (>1GB) and cause out-of-memory issues when
		// read into a byte buffer. We use the readeropener utility to use an
		// in-memory buffer when the size is small and to write the contents to disk
		// when large. Note that having some buffer is necessary as PushArchive will
		// attempt to read the buffer more than once and tar files don't have a way
		// to seek backwards (tape only ran one direction after all).
		opener, cleanup, err := readeropener.New(r, maxInMemorySizeForPushArchive)
		if err != nil {
			return nil, fmt.Errorf("could not process tar file %q: %v", filename, err)
		}
		defer cleanup()
		img, err := tarball.Image(tarball.Opener(opener), nil)
		if err != nil {
			return nil, fmt.Errorf("could not create tarball image: %v", err)
		}

		tag, err := imagetags.DefaultTag()
		if err != nil {
			return nil, errors.Wrap(err, "generating tag")
		}

		return transferer.Write(ctx, name, tag, img)
	}
}

// writer is the interface required to push an Image to a particular reference.
type writer interface {
	Write(ctx context.Context, name string, tag string, img containerregistry.Image) (*ipb.Image, error)
}
