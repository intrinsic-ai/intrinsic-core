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

// Package ioutils provides I/O utils for working with Assets.
package ioutils

import (
	"context"
	"fmt"
	"io"

	"github.com/google/safearchive/tar"
	"google.golang.org/protobuf/proto"
)

// ReadBinaryProto reads a binary proto from a reader and unmarshals it into a proto.
func ReadBinaryProto(r io.Reader, p proto.Message) error {
	if b, err := io.ReadAll(r); err != nil {
		return fmt.Errorf("error reading: %v", err)
	} else if err := proto.Unmarshal(b, p); err != nil {
		return fmt.Errorf("error parsing proto: %v", err)
	}

	return nil
}

// WalkTarFileHandler is a function that handles a file in a .tar archive.
type WalkTarFileHandler func(ctx context.Context, r io.Reader) error

// WalkTarFileFallbackHandler is a fallback handler for files in a .tar archive that aren't handled
// by another handler.
type WalkTarFileFallbackHandler func(context.Context, string, io.Reader) error

type walkTarFileOptions struct {
	handlers map[string]WalkTarFileHandler
	fallback WalkTarFileFallbackHandler
}

// WalkTarFileOption is a functional option for WalkTarFile.
type WalkTarFileOption func(*walkTarFileOptions)

// WithHandlers specifies the handlers to use when calling WalkTarFile.
func WithHandlers(handlers map[string]WalkTarFileHandler) WalkTarFileOption {
	return func(opts *walkTarFileOptions) {
		opts.handlers = handlers
	}
}

// WithFallbackHandler specifies the fallback handler to use when calling WalkTarFile.
func WithFallbackHandler(fallback WalkTarFileFallbackHandler) WalkTarFileOption {
	return func(opts *walkTarFileOptions) {
		opts.fallback = fallback
	}
}

// IgnoreHandler can be used as a handler to ignore specific files.
func IgnoreHandler(ctx context.Context, r io.Reader) error {
	return nil
}

// AlwaysErrorAsUnexpected can be used as a fallback handler that will always trigger an unexpected
// file error.
//
// Using this handler forces all files to be handled explicitly.
func AlwaysErrorAsUnexpected(ctx context.Context, n string, r io.Reader) error {
	return fmt.Errorf("unexpected file %q", n)
}

// MakeBinaryProtoHandler creates a handler that reads a binary proto file and unmarshals it into a
// message.
//
// The proto must not be nil.
func MakeBinaryProtoHandler(p proto.Message) WalkTarFileHandler {
	return func(ctx context.Context, r io.Reader) error {
		b, err := io.ReadAll(r)
		if err != nil {
			return fmt.Errorf("error reading: %v", err)
		}
		if err := proto.Unmarshal(b, p); err != nil {
			return fmt.Errorf("error parsing proto: %v", err)
		}
		return nil
	}
}

// MakeCollectInlinedFallbackHandler creates a fallback handler that collects all of the unknown
// files and reads their bytes into a map.
//
// Keys are filenames and values are file contents.
func MakeCollectInlinedFallbackHandler() (map[string][]byte, WalkTarFileFallbackHandler) {
	inlined := map[string][]byte{}
	fallback := func(ctx context.Context, n string, r io.Reader) error {
		b, err := io.ReadAll(r)
		if err != nil {
			return fmt.Errorf("error reading: %v", err)
		}
		inlined[n] = b
		return nil
	}
	return inlined, fallback
}

// readerAtSeeker is an io.Reader that also supports random access and seeking.
type readerAtSeeker interface {
	io.Reader
	io.ReaderAt
	io.Seeker
}

// WalkTarFile walks through a tar file and invokes handlers on specific filenames.
//
// fallback can be nil.
//
// If r supports random access (io.ReaderAt and io.Seeker), handlers receive an
// *io.SectionReader over the entry's bytes in r, which they may re-read or pass
// to readeropener.New. Otherwise, handlers receive a forward-only reader.
//
// Returns an error if all handlers in handlers are not invoked.  It ignores all non-regular files.
func WalkTarFile(ctx context.Context, r io.Reader, options ...WalkTarFileOption) error {
	// WalkTarFile previously took a *tar.Reader. Wrapping one in another
	// tar.NewReader would silently see an empty archive, so reject it.
	if _, ok := r.(*tar.Reader); ok {
		return fmt.Errorf("WalkTarFile: pass the underlying reader, not a *tar.Reader")
	}

	opts := &walkTarFileOptions{}
	for _, opt := range options {
		opt(opts)
	}

	t := tar.NewReader(r)
	ras, _ := r.(readerAtSeeker)

	handlers := opts.handlers

	for len(handlers) > 0 || opts.fallback != nil {
		hdr, err := t.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return fmt.Errorf("getting next file failed: %v", err)
		}
		if hdr.Typeflag != tar.TypeReg {
			continue
		}

		// When r supports random access, expose each tar entry as an
		// *io.SectionReader over its byte range in the outer archive. Because tar
		// stores file payloads uncompressed immediately after each header block,
		// handlers (such as image processors) can re-read large entries directly
		// from disk without copying them into memory or /tmp. SectionReader uses
		// ReadAt (pread), so it does not disturb r's seek offset when t.Next()
		// advances to the next header.
		var entryReader io.Reader = t
		if ras != nil {
			offset, err := ras.Seek(0, io.SeekCurrent)
			if err != nil {
				return fmt.Errorf("failed to get current offset in tar stream: %w", err)
			}
			entryReader = io.NewSectionReader(ras, offset, hdr.Size)
		}

		n := hdr.Name
		if h, ok := handlers[n]; ok {
			delete(handlers, n)
			if err := h(ctx, entryReader); err != nil {
				return fmt.Errorf("error processing file %q: %v", n, err)
			}
		} else if opts.fallback != nil {
			if err := opts.fallback(ctx, n, entryReader); err != nil {
				return fmt.Errorf("error processing file %q: %v", n, err)
			}
		}
	}
	if len(handlers) != 0 {
		keys := make([]string, 0, len(handlers))
		for k := range handlers {
			keys = append(keys, k)
		}
		return fmt.Errorf("missing expected files %s", keys)
	}
	return nil
}
