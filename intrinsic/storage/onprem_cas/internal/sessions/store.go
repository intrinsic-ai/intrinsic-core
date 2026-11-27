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

// Package sessions manages on-disk state and in-process locking for resumable upload sessions.
package sessions

import (
	"context"
	"crypto/sha512"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"sync"
	"time"

	"golang.org/x/sys/unix"

	"intrinsic/storage/onprem_cas/internal"
)

// defaultLockWait is how long an RPC waits for another stream of the same upload session to finish
// before Acquire fails with ErrBusy. When a client cancels a stream, the handler of that stream
// persists the received bytes and releases the session within milliseconds, so waiting lets
// QueryWriteStatus return the final persisted size instead of failing with Aborted.
const defaultLockWait = 10 * time.Second

var (
	// ErrNotFound is returned by ReadMeta when a session does not exist on disk.
	ErrNotFound = errors.New("upload session not found or expired")
	// ErrBusy is returned by Acquire when another caller holds the session longer than lockWait.
	ErrBusy = errors.New("upload session has an active stream")
)

// Meta is the content of the metadata file of a resumable upload session.
type Meta struct {
	// ObjectSize is the expected size of the object, if the client provided it.
	ObjectSize *int64 `json:"object_size,omitempty"`
	// PersistedSize is the number of bytes at the start of the data file that were synced to disk.
	PersistedSize int64 `json:"persisted_size"`
	// SHA512State is the marshaled state of the SHA-512 hasher after PersistedSize bytes.
	SHA512State []byte `json:"sha512_state,omitempty"`
	// CommittedDigest is set when finalizing starts, before the data file is linked to the
	// object. From then on, the data file may be the object itself, so the session accepts no
	// more data and can only be finalized (again).
	CommittedDigest string `json:"committed_digest,omitempty"`
	// FinalObjectID is set once the upload was finalized.
	FinalObjectID string `json:"final_object_id,omitempty"`
}

// Store stores resumable upload sessions in a directory and makes sure that at most one caller at
// a time works on a session. The on-prem CAS runs as a single process, so in-process locks are
// sufficient.
//
// For each session, the directory contains:
//   - <server_id>.partial: the uploaded data,
//   - <server_id>.metadata.json: the Meta, replaced atomically,
//
// where server_id is derived from the upload ID (see ServerID).
type Store struct {
	dir      string
	lockWait time.Duration

	mu sync.Mutex
	// active maps each locked session ID to a channel that is closed when its holder releases it.
	active map[string]chan struct{}
}

// Option configures a Store created by NewStore.
type Option func(*Store)

// WithLockWait sets the maximum duration Acquire waits for a busy session before returning ErrBusy.
func WithLockWait(d time.Duration) Option {
	return func(s *Store) {
		s.lockWait = d
	}
}

// NewStore creates a Store backed by dir.
func NewStore(dir string, opts ...Option) *Store {
	s := &Store{
		dir:      dir,
		lockWait: defaultLockWait,
		active:   make(map[string]chan struct{}),
	}
	for _, opt := range opts {
		opt(s)
	}
	return s
}

// ServerID returns the ID under which the session with the given upload ID is stored. Like in
// the cloud CAS, it is a hash of the upload ID, which makes any upload ID a safe file name. There
// is no caller identity on-prem, so unlike the cloud, the hash doesn't include an email.
func ServerID(uploadID string) string {
	sum := sha512.Sum512([]byte(uploadID))
	return hex.EncodeToString(sum[:])
}

// Dir returns the directory where session files are stored.
func (s *Store) Dir() string {
	return s.dir
}

// DataPath returns the path to the partial data file for the session id.
func (s *Store) DataPath(id string) string {
	return filepath.Join(s.dir, id+internal.PartialExt)
}

func (s *Store) metaPath(id string) string {
	return filepath.Join(s.dir, id+internal.SessionMetadataExt)
}

func (s *Store) releaser(id string, ch chan struct{}) func() {
	return sync.OnceFunc(func() {
		s.mu.Lock()
		delete(s.active, id)
		close(ch)
		s.mu.Unlock()
	})
}

// Acquire locks the session id. If another caller holds it, Acquire waits up to lockWait and then
// fails with ErrBusy (or ctx.Err() if ctx is cancelled first). The returned function releases the
// session and is safe to call more than once.
func (s *Store) Acquire(ctx context.Context, id string) (func(), error) {
	var timer *time.Timer
	for {
		if err := ctx.Err(); err != nil {
			return nil, err
		}
		s.mu.Lock()
		done, busy := s.active[id]
		if !busy {
			ch := make(chan struct{})
			s.active[id] = ch
			s.mu.Unlock()
			return s.releaser(id, ch), nil
		}
		s.mu.Unlock()

		if timer == nil {
			timer = time.NewTimer(s.lockWait)
			defer timer.Stop()
		}
		select {
		case <-done:
			// Current holder released; retry acquiring.
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-timer.C:
			return nil, ErrBusy
		}
	}
}

// TryAcquire locks the session id if no other caller holds it. The returned function releases the
// session and is safe to call more than once.
func (s *Store) TryAcquire(id string) (func(), bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, busy := s.active[id]; busy {
		return nil, false
	}
	ch := make(chan struct{})
	s.active[id] = ch
	return s.releaser(id, ch), true
}

// ReadMeta reads the metadata of the session id. It returns ErrNotFound if the session doesn't
// exist.
func (s *Store) ReadMeta(id string) (*Meta, error) {
	b, err := os.ReadFile(s.metaPath(id))
	if errors.Is(err, fs.ErrNotExist) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, fmt.Errorf("read metadata: %w", err)
	}
	var m Meta
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, fmt.Errorf("decode metadata: %w", err)
	}
	return &m, nil
}

// WriteMeta atomically replaces the metadata of the session id and syncs it to disk.
func (s *Store) WriteMeta(id string, m *Meta) error {
	b, err := json.Marshal(m)
	if err != nil {
		return fmt.Errorf("encode metadata: %w", err)
	}
	path := s.metaPath(id)
	if err := writeFileAtomicSync(path, path+internal.SessionTmpExt, b); err != nil {
		return fmt.Errorf("save metadata: %w", err)
	}
	return nil
}

// Remove deletes all files of the session id. The metadata file is removed first so that a crash
// mid-removal doesn't leave a session that QueryWriteStatus reports as active while its data is
// already gone.
func (s *Store) Remove(id string) {
	for _, path := range []string{s.metaPath(id), s.metaPath(id) + internal.SessionTmpExt, s.DataPath(id)} {
		if err := os.Remove(path); err != nil && !errors.Is(err, fs.ErrNotExist) {
			slog.Warn("Failed to remove upload session file", slog.String("path", path), slog.Any("error", err))
		}
	}
}

// writeFileAtomicSync writes b to tmpPath, syncs it, renames it to path, and syncs path's parent
// directory. If writing or renaming fails, tmpPath is removed.
func writeFileAtomicSync(path, tmpPath string, b []byte) error {
	f, err := os.OpenFile(tmpPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("create temp file: %w", err)
	}
	renamed := false
	defer func() {
		if !renamed {
			_ = os.Remove(tmpPath)
		}
	}()
	if _, err := f.Write(b); err != nil {
		_ = f.Close()
		return fmt.Errorf("write temp file: %w", err)
	}
	if err := f.Sync(); err != nil {
		_ = f.Close()
		return fmt.Errorf("sync temp file: %w", err)
	}
	if err := f.Close(); err != nil {
		return fmt.Errorf("close temp file: %w", err)
	}
	if err := os.Rename(tmpPath, path); err != nil {
		return fmt.Errorf("rename temp file: %w", err)
	}
	renamed = true
	if err := SyncDir(filepath.Dir(path)); err != nil {
		return fmt.Errorf("sync directory: %w", err)
	}
	return nil
}

// SyncDir fsyncs dir so that directory entry changes (such as os.Rename or os.Link) are durable
// on disk before acknowledging an RPC. Syncing a file alone only flushes the file's data and
// inode, not its name entry in the parent directory.
func SyncDir(dir string) error {
	d, err := os.Open(dir)
	if err != nil {
		return err
	}
	if err := d.Sync(); err != nil {
		_ = d.Close()
		return err
	}
	return d.Close()
}

// Preallocate reserves disk space for size bytes of f without changing its length, so that a
// full disk is detected before the upload starts. File systems that don't support it are
// ignored.
func Preallocate(f *os.File, size int64) error {
	if size <= 0 {
		return nil
	}
	rawConn, err := f.SyscallConn()
	if err != nil {
		return err
	}
	var fallocErr error
	if err := rawConn.Control(func(fd uintptr) {
		for {
			fallocErr = unix.Fallocate(int(fd), unix.FALLOC_FL_KEEP_SIZE, 0, size)
			if !errors.Is(fallocErr, unix.EINTR) {
				return
			}
		}
	}); err != nil {
		return err
	}
	if errors.Is(fallocErr, unix.EOPNOTSUPP) || errors.Is(fallocErr, unix.ENOSYS) {
		return nil
	}
	return fallocErr
}
