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

package handlers

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
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"intrinsic/storage/onprem_cas/internal"
)

// defaultSessionLockWait is how long an RPC waits for another stream of the same upload session
// to finish before it fails with Aborted. When a client cancels a stream, the handler of that
// stream persists the received bytes and releases the session within milliseconds, so waiting
// lets QueryWriteStatus return the final persisted size instead of Aborted.
const defaultSessionLockWait = 10 * time.Second

// sessionMeta is the content of the metadata file of a resumable upload session.
type sessionMeta struct {
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

// sessionStore stores resumable upload sessions in a directory and makes sure that at most one
// RPC at a time works on a session. The on-prem CAS runs as a single process, so in-process locks
// are sufficient.
//
// For each session, the directory contains:
//   - <server_id>.partial: the uploaded data,
//   - <server_id>.metadata.json: the sessionMeta, replaced atomically,
//
// where server_id is derived from the upload ID (see serverID).
type sessionStore struct {
	dir      string
	lockWait time.Duration

	mu sync.Mutex
	// active maps each locked session ID to a channel that is closed when its holder releases it.
	active map[string]chan struct{}
}

func newSessionStore(dir string) *sessionStore {
	return &sessionStore{
		dir:      dir,
		lockWait: defaultSessionLockWait,
		active:   make(map[string]chan struct{}),
	}
}

// serverID returns the ID under which the session with the given upload ID is stored. Like in
// the cloud CAS, it is a hash of the upload ID, which makes any upload ID a safe file name. There
// is no caller identity on-prem, so unlike the cloud, the hash doesn't include an email.
func serverID(uploadID string) string {
	sum := sha512.Sum512([]byte(uploadID))
	return hex.EncodeToString(sum[:])
}

func (s *sessionStore) dataPath(id string) string {
	return filepath.Join(s.dir, id+internal.PartialExt)
}

func (s *sessionStore) metaPath(id string) string {
	return filepath.Join(s.dir, id+internal.SessionMetadataExt)
}

func (s *sessionStore) releaser(id string, ch chan struct{}) func() {
	return sync.OnceFunc(func() {
		s.mu.Lock()
		delete(s.active, id)
		close(ch)
		s.mu.Unlock()
	})
}

// acquire locks the session id. If another RPC holds it, acquire waits up to lockWait and then
// fails with Aborted. The returned function releases the session and is safe to call more than
// once.
func (s *sessionStore) acquire(ctx context.Context, id string) (func(), error) {
	var timer *time.Timer
	for {
		if err := ctx.Err(); err != nil {
			return nil, status.FromContextError(err).Err()
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
			return nil, status.FromContextError(ctx.Err()).Err()
		case <-timer.C:
			return nil, statusErrorf(codes.Aborted, "upload session has an active stream")
		}
	}
}

// tryAcquire locks the session id if no other RPC holds it. The returned function releases the
// session and is safe to call more than once.
func (s *sessionStore) tryAcquire(id string) (func(), bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, busy := s.active[id]; busy {
		return nil, false
	}
	ch := make(chan struct{})
	s.active[id] = ch
	return s.releaser(id, ch), true
}

// readMeta reads the metadata of the session id. It fails with NotFound if the session doesn't
// exist.
func (s *sessionStore) readMeta(id string) (*sessionMeta, error) {
	b, err := os.ReadFile(s.metaPath(id))
	if errors.Is(err, fs.ErrNotExist) {
		return nil, status.Error(codes.NotFound, "upload session not found or expired, call StartResumableWrite first")
	}
	if err != nil {
		return nil, toGRPCError(err, "failed to read upload session metadata")
	}
	var m sessionMeta
	if err := json.Unmarshal(b, &m); err != nil {
		return nil, statusErrorf(codes.Internal, "failed to decode upload session metadata: %v", err)
	}
	return &m, nil
}

// writeMeta atomically replaces the metadata of the session id and syncs it to disk.
func (s *sessionStore) writeMeta(id string, m *sessionMeta) error {
	b, err := json.Marshal(m)
	if err != nil {
		return statusErrorf(codes.Internal, "failed to encode upload session metadata: %v", err)
	}
	path := s.metaPath(id)
	if err := writeFileAtomicSync(path, path+internal.SessionTmpExt, b); err != nil {
		return toGRPCError(err, "failed to save upload session metadata")
	}
	return nil
}

// remove deletes all files of the session id. The metadata file is removed first so that a crash
// mid-removal doesn't leave a session that QueryWriteStatus reports as active while its data is
// already gone.
func (s *sessionStore) remove(id string) {
	for _, path := range []string{s.metaPath(id), s.metaPath(id) + internal.SessionTmpExt, s.dataPath(id)} {
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
	if err := syncDir(filepath.Dir(path)); err != nil {
		return fmt.Errorf("sync directory: %w", err)
	}
	return nil
}

// syncDir fsyncs dir so that directory entry changes (such as os.Rename or os.Link) are durable
// on disk before acknowledging an RPC. Syncing a file alone only flushes the file's data and
// inode, not its name entry in the parent directory.
func syncDir(dir string) error {
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

// preallocate reserves disk space for size bytes of f without changing its length, so that a
// full disk is detected before the upload starts. File systems that don't support it are
// ignored.
func preallocate(f *os.File, size int64) error {
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
