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

// Package readeropener contains a function that takes an io.Reader and returns
// an Opener function that can be called multiple times.
package readeropener

import (
	"fmt"
	"io"
)

// Opener is a function that returns an io.ReadCloser. It can be called
// multiple times.
type Opener func() (io.ReadCloser, error)

type readerAtSeeker interface {
	io.ReaderAt
	io.Seeker
}

// New takes an io.Reader that supports random access (io.ReaderAt and io.Seeker)
// and returns an Opener function that can be called multiple times without
// copying data to memory or temporary files.
func New(r io.Reader) (Opener, error) {
	ras, ok := r.(readerAtSeeker)
	if !ok {
		return nil, fmt.Errorf("reader of type %T must implement io.ReaderAt and io.Seeker", r)
	}
	start, err := ras.Seek(0, io.SeekCurrent)
	if err != nil {
		return nil, fmt.Errorf("failed to seek to current offset: %w", err)
	}
	end, err := ras.Seek(0, io.SeekEnd)
	if err != nil {
		return nil, fmt.Errorf("failed to seek to end: %w", err)
	}
	size := end - start
	opener := func() (io.ReadCloser, error) {
		return io.NopCloser(io.NewSectionReader(ras, start, size)), nil
	}
	return opener, nil
}
