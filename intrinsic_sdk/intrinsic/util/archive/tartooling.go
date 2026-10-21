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

// Package tartooling provides helpers to work with tar archives.
package tartooling

import (
	"bytes"
	"io"
	"os"
	"path/filepath"

	"github.com/google/safearchive/tar"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
)

const (
	defaultMode = 0o644
)

// AddDir adds a directory dir recursively to the writer w.
// Only files are added. Paths are made relative to dir.
func AddDir(dir string, w *tar.Writer) error {
	err := filepath.Walk(dir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return errors.Wrapf(err, "failed to walk directory %q", path)
		}
		if info.IsDir() {
			return nil
		}
		relPath, err := filepath.Rel(dir, path)
		if err != nil {
			return errors.Wrapf(err, "failed to get relative path %q %q", dir, path)
		}
		if err := AddFile(path, w, relPath); err != nil {
			return errors.Wrapf(err, "failed to add %q as %q to tar", path, relPath)
		}
		return nil
	})
	if err != nil {
		return errors.Wrapf(err, "failed to add %q to tar", dir)
	}
	return err
}

// AddFile adds a local file to the given tar writer.
// No mode or creation timestamp is set. If overwriteName is empty, filepath.Base(name)
// is used as name in the tar. overwriteName is allowed to be a path.
func AddFile(path string, w *tar.Writer, overwriteName string) error {
	f, err := os.Open(path)
	if err != nil {
		return errors.Wrapf(err, "failed to open %q", path)
	}
	defer f.Close()
	s, err := f.Stat()
	if err != nil {
		return errors.Wrapf(err, "failed to stat %q", path)
	}
	name := filepath.Base(path)
	if overwriteName != "" {
		name = overwriteName
	}
	if err := AddReader(f, s.Size(), w, name); err != nil {
		return errors.Wrapf(err, "failed to add %q as %q to tar", name, overwriteName)
	}
	return nil
}

// AddReader adds the content of a reader to a tar writer.
// Name can be path. The size of the content need to be known beforehand.
// Returns an error if reader r size does not match size parameter.
func AddReader(r io.Reader, size int64, w *tar.Writer, name string) error {
	h := &tar.Header{
		Name:     name,
		Size:     size,
		Mode:     defaultMode,
		Typeflag: tar.TypeReg,
	}

	if err := w.WriteHeader(h); err != nil {
		return errors.Wrapf(err, "failed to write header %+v for %q", h, name)
	}
	cnt, err := io.Copy(w, r)
	if err != nil {
		return errors.Wrapf(err, "failed to write %q after %d/%d bytes written", name, cnt, size)
	}
	if cnt != size {
		return errors.Errorf("unexpected data size, %d Bytes given but expected %d", cnt, size)
	}
	return nil
}

// AddBinaryProto writes a proto message as a binary file in the tar writer.
// This is done deterministically so this can be used as a build artifact.  A
// nil message will be ignored and not create a file.
func AddBinaryProto(p proto.Message, w *tar.Writer, path string) error {
	if p == nil {
		return nil
	}
	b, err := proto.MarshalOptions{Deterministic: true}.Marshal(p)
	if err != nil {
		return errors.Wrapf(err, "failed to serialize %q", path)
	}
	contents := bytes.NewBuffer(b)

	h := &tar.Header{
		Name:     path,
		Mode:     defaultMode,
		Size:     int64(contents.Len()),
		Typeflag: tar.TypeReg,
	}
	if err := w.WriteHeader(h); err != nil {
		return errors.Wrapf(err, "failed to write header %+v for %q", h, path)
	}
	if _, err := io.Copy(w, contents); err != nil {
		return err
	}

	return nil
}

// AddBytes writes a slice of bytes as a binary file in the tar writer.
// A nil slice create an empty file.
func AddBytes(b []byte, w *tar.Writer, path string) error {
	contents := bytes.NewBuffer(b)

	h := &tar.Header{
		Name:     path,
		Mode:     defaultMode,
		Size:     int64(contents.Len()),
		Typeflag: tar.TypeReg,
	}
	if err := w.WriteHeader(h); err != nil {
		return errors.Wrapf(err, "failed to write header %+v for %q", h, path)
	}
	if _, err := io.Copy(w, contents); err != nil {
		return err
	}

	return nil
}

// Copy copies from a tar reader to a tar writer.
func Copy(tr *tar.Reader, tw *tar.Writer) error {
	for {
		h, err := tr.Next()
		if errors.Is(err, io.EOF) {
			return nil
		}
		if err != nil {
			return errors.Wrap(err, "failed to read from source bundle")
		}
		if err := tw.WriteHeader(h); err != nil {
			return errors.Wrapf(err, "failed to write header")
		}
		if _, err := io.Copy(tw, tr); err != nil {
			return errors.Wrapf(err, "failed to copy %q from tar", h.Name)
		}
	}
}

// SeekTo advances a tar reader to the start of the specified file.
func SeekTo(r *tar.Reader, name string) error {
	for {
		h, err := r.Next()
		if err != nil {
			// This will return io.EOF if the desired name is not found.
			return err
		}
		if h.Name == name {
			return nil
		}
	}
}
