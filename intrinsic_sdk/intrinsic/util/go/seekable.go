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

// Package seekable adapts forward-only io.Readers into seekable io.ReadSeekers by recording read
// bytes to a temporary file on demand.
package seekable

import (
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"runtime"
	"sync"
)

// From returns an io.ReadSeeker for the given reader, along with a cleanup function to release
// any temporary resources allocated during recording.
//
// If r already implements io.ReadSeeker, it is returned directly and cleanup is a no-op.
// Otherwise, it returns an io.ReadSeeker backed by a temporary file, and cleanup removes the
// temporary file.
func From(r io.Reader) (io.ReadSeeker, func() error, error) {
	if r == nil {
		return nil, nil, errors.New("reader cannot be nil")
	}
	if rs, ok := r.(io.ReadSeeker); ok {
		return rs, func() error { return nil }, nil
	}

	f, err := os.CreateTemp("", "seekable-")
	if err != nil {
		return nil, nil, fmt.Errorf("failed to create temp file: %w", err)
	}
	rs := &recordSeeker{
		file: f,
		src:  r,
	}

	runtime.SetFinalizer(rs, func(s *recordSeeker) {
		_ = s.cleanup()
	})

	return rs, rs.cleanup, nil
}

// recordSeeker is an io.ReadSeeker that records bytes from an unseekable io.Reader to a temporary
// file as they are read, allowing rewinding to previously read data.
type recordSeeker struct {
	bytesRecorded int64
	closed        bool
	file          *os.File
	mu            sync.Mutex
	readPos       int64
	src           io.Reader
}

func (s *recordSeeker) Read(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return 0, errors.New("read on closed recordSeeker")
	}

	bytesRead := 0

	// Read from recorded temp file if readPos is behind what we've recorded.
	if s.readPos < s.bytesRecorded {
		bytesToRead := int64(len(p))
		if bytesAvailable := s.bytesRecorded - s.readPos; bytesToRead > bytesAvailable {
			bytesToRead = bytesAvailable
		}
		n, err := s.file.ReadAt(p[:bytesToRead], s.readPos)
		s.readPos += int64(n)
		bytesRead += n
		if err != nil && err != io.EOF {
			return bytesRead, err
		}
		if bytesRead == len(p) || s.readPos < s.bytesRecorded {
			return bytesRead, nil
		}
	}

	// Read remaining bytes from source Reader if caught up to recorded data.
	if bytesRead < len(p) {
		n, err := s.readAndRecord(p[bytesRead:])
		s.readPos += int64(n)
		bytesRead += n
		if err != nil {
			return bytesRead, err
		}
	}

	return bytesRead, nil
}

func (s *recordSeeker) Seek(offset int64, whence int) (int64, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return 0, errors.New("seek on closed recordSeeker")
	}

	var target int64
	switch whence {
	case io.SeekStart:
		target = offset
	case io.SeekCurrent:
		target = s.readPos + offset
	case io.SeekEnd:
		if err := s.readThrough(math.MaxInt64); err != nil {
			return 0, err
		}
		target = s.bytesRecorded + offset
	default:
		return 0, errors.New("invalid whence")
	}

	if target < 0 {
		return 0, errors.New("negative position")
	}

	if target > s.bytesRecorded {
		if err := s.readThrough(target); err != nil {
			return 0, err
		}
		if target > s.bytesRecorded {
			target = s.bytesRecorded
		}
	}

	s.readPos = target
	return s.readPos, nil
}

// readAndRecord reads bytes from src into p, writes them to the temporary file at bytesRecorded,
// and advances bytesRecorded.
func (s *recordSeeker) readAndRecord(p []byte) (int, error) {
	n, err := s.src.Read(p)
	if n > 0 {
		if _, writeErr := s.file.WriteAt(p[:n], s.bytesRecorded); writeErr != nil {
			return 0, writeErr
		}
		s.bytesRecorded += int64(n)
	}
	return n, err
}

// readThrough reads from src and records to the temporary file until bytesRecorded >= target or
// EOF is reached.
func (s *recordSeeker) readThrough(target int64) error {
	buf := make([]byte, 32*1024)
	for target > s.bytesRecorded {
		bytesToRead := int64(len(buf))
		if bytesNeeded := target - s.bytesRecorded; bytesNeeded < bytesToRead {
			bytesToRead = bytesNeeded
		}
		n, err := s.readAndRecord(buf[:bytesToRead])
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if n == 0 {
			return nil
		}
	}
	return nil
}

func (s *recordSeeker) cleanup() error {
	s.mu.Lock()
	defer s.mu.Unlock()

	if s.closed {
		return nil
	}
	s.closed = true
	runtime.SetFinalizer(s, nil)

	if s.file != nil {
		name := s.file.Name()
		err := errors.Join(s.file.Close(), os.Remove(name))
		s.file = nil

		return err
	}

	return nil
}
