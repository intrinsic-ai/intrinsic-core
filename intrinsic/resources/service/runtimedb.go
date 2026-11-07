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

// Package runtimedb implements a light-weight DB to be used to hold runtime
// information about resource types.
package runtimedb

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"

	bolt "go.etcd.io/bbolt"
	"golang.org/x/exp/slices"
	"google.golang.org/grpc/codes"
	grpcstatus "google.golang.org/grpc/status"

	rdpb "intrinsic/resources/proto/runtime_db_go_proto"
)

func bucketToken(b *bolt.Bucket) string {
	return strconv.FormatUint(b.Sequence(), 10)
}

func bumpBucketToken(b *bolt.Bucket) error {
	if _, err := b.NextSequence(); err != nil {
		return grpcstatus.Errorf(codes.Internal, "could not increment bucket sequence: %v", err)
	}
	return nil
}

// Server implements a DB backed by bolt-db.
type Server struct {
	buckets []string
	db      *bolt.DB
}

type bucketed interface {
	GetBucket() string
}

func (s *Server) validateBucket(request bucketed) error {
	if !slices.Contains(s.buckets, request.GetBucket()) {
		return grpcstatus.Errorf(codes.FailedPrecondition, "bucket %q was not passed in the initial bucket list: %s", request.GetBucket(), strings.Join(s.buckets, ","))
	}
	return nil
}

// Put adds a new key-value pair to the DB.
func (s *Server) Put(ctx context.Context, request *rdpb.PutRequest) (*rdpb.PutResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	err := s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		if err := b.Put([]byte(request.GetKey()), request.GetValue()); err != nil {
			return grpcstatus.Errorf(codes.Internal, "could not put value with key %q", request.GetKey())
		}
		return bumpBucketToken(b)
	})
	if err != nil {
		return nil, err
	}
	return &rdpb.PutResponse{}, nil
}

// Get retrieves a value from the DB given a key.
func (s *Server) Get(ctx context.Context, request *rdpb.GetRequest) (*rdpb.GetResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	var v []byte
	if err := s.db.View(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		// bucket.Get() returns a byte slice that is only valid for the duration of the transaction.
		// We need to clone it to use it outside of the transaction.
		v = slices.Clone(b.Get([]byte(request.GetKey())))
		if v == nil {
			return grpcstatus.Errorf(codes.NotFound, "no value with key %q", request.GetKey())
		}
		return nil
	}); err != nil {
		return nil, err
	}

	return &rdpb.GetResponse{
		Value: v,
	}, nil
}

// List retrieves all values from the DB.
func (s *Server) List(ctx context.Context, request *rdpb.ListRequest) (*rdpb.ListResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	var keys []string
	var token string
	if err := s.db.View(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		token = bucketToken(b)
		if err := b.ForEach(func(k, v []byte) error {
			keys = append(keys, string(k))
			return nil
		}); err != nil {
			return grpcstatus.Errorf(codes.Internal, "could not get all entries")
		}
		return nil
	}); err != nil {
		return nil, err
	}

	return &rdpb.ListResponse{
		Keys:          keys,
		RevisionToken: token,
	}, nil
}

// Delete retrieves a value from the DB given a key. Note that if the key does
// not exist, this function still returns an OK status.
func (s *Server) Delete(ctx context.Context, request *rdpb.DeleteRequest) (*rdpb.DeleteResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	if err := s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		if err := b.Delete([]byte(request.GetKey())); err != nil {
			return grpcstatus.Errorf(codes.Internal, "could not delete entry %q", request.GetKey())
		}
		return bumpBucketToken(b)
	}); err != nil {
		return nil, err
	}

	return &rdpb.DeleteResponse{}, nil
}

// Clear removes all entries in the DB.
func (s *Server) Clear(ctx context.Context, request *rdpb.ClearRequest) (*rdpb.ClearResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	if err := s.db.Update(func(tx *bolt.Tx) error {
		var seq uint64
		if b := tx.Bucket([]byte(request.GetBucket())); b != nil {
			seq = b.Sequence()
		}
		if err := tx.DeleteBucket([]byte(request.GetBucket())); err != nil {
			return fmt.Errorf("could not delete bucket %q: %v", request.GetBucket(), err)
		}
		b, err := tx.CreateBucketIfNotExists([]byte(request.GetBucket()))
		if err != nil {
			return fmt.Errorf("could not recreate bucket %q: %v", request.GetBucket(), err)
		}
		if err := b.SetSequence(seq + 1); err != nil {
			return fmt.Errorf("could not set sequence on bucket %q: %v", request.GetBucket(), err)
		}
		return nil
	}); err != nil {
		return nil, grpcstatus.Errorf(codes.Internal, "could not clear all entries: %v", err)
	}
	return &rdpb.ClearResponse{}, nil
}

// Set the bucket to match the provided list of keys and values.
func (s *Server) Patch(ctx context.Context, request *rdpb.PatchRequest) (*rdpb.PatchResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	if err := s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		if err := b.ForEach(func(k, v []byte) error {
			if msg, ok := request.GetValues()[string(k)]; ok {
				// Only update if a value was specified.
				if msg != nil {
					if err := b.Put(k, msg.GetValue()); err != nil {
						return grpcstatus.Errorf(codes.Internal, "could not update %q: %v", k, err)
					}
				}
				// Remove it from the map now that we're done with it.
				delete(request.GetValues(), string(k))
			} else {
				// In the bucket, but not specified in the request, so delete it.
				if err := b.Delete(k); err != nil {
					return grpcstatus.Errorf(codes.Internal, "could not delete %q: %v", k, err)
				}
			}
			return nil
		}); err != nil {
			return err
		}
		// The remaining keys don't exist yet, so create them.
		for k, msg := range request.GetValues() {
			if msg == nil {
				return grpcstatus.Errorf(codes.FailedPrecondition, "no data specified for %q, which does not yet exist", k)
			}
			if err := b.Put([]byte(k), msg.GetValue()); err != nil {
				return err
			}
		}
		return bumpBucketToken(b)
	}); err != nil {
		return nil, err
	}
	return &rdpb.PatchResponse{}, nil
}

// BatchPut adds new key-value pairs to the DB.
func (s *Server) BatchPut(ctx context.Context, request *rdpb.BatchPutRequest) (*rdpb.BatchPutResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	if err := s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		for k, v := range request.GetValues() {
			if err := b.Put([]byte(k), v); err != nil {
				return grpcstatus.Errorf(codes.Internal, "could not put value with key %q", k)
			}
		}
		return bumpBucketToken(b)
	}); err != nil {
		return nil, err
	}
	return &rdpb.BatchPutResponse{}, nil
}

// BatchGet retrieves values from the DB given a key.
func (s *Server) BatchGet(ctx context.Context, request *rdpb.BatchGetRequest) (*rdpb.BatchGetResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	values := make(map[string][]byte, len(request.GetKeys()))
	if err := s.db.View(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		if request.GetRevisionToken() != "" && request.GetRevisionToken() != bucketToken(b) {
			return grpcstatus.Errorf(codes.Aborted, "revision token mismatch for bucket %q: got %q, want %q", request.GetBucket(), request.GetRevisionToken(), bucketToken(b))
		}
		for _, k := range request.GetKeys() {
			v := b.Get([]byte(k))
			if v == nil {
				return grpcstatus.Errorf(codes.NotFound, "no value with key %q", k)
			}
			// bucket.Get() returns a byte slice that is only valid for the duration of the transaction.
			// We need to clone it to use it outside of the transaction.
			values[k] = slices.Clone(v)
		}
		return nil
	}); err != nil {
		return nil, err
	}

	return &rdpb.BatchGetResponse{
		Values: values,
	}, nil
}

// BatchDelete deletes the specified keys from the DB. Keys that do not exist
// are ignored and do not trigger an error.
func (s *Server) BatchDelete(ctx context.Context, request *rdpb.BatchDeleteRequest) (*rdpb.BatchDeleteResponse, error) {
	if err := s.validateBucket(request); err != nil {
		return nil, err
	}
	if err := s.db.Update(func(tx *bolt.Tx) error {
		b := tx.Bucket([]byte(request.GetBucket()))
		if b == nil {
			return grpcstatus.Errorf(codes.Internal, "could not get bucket %q", request.GetBucket())
		}
		for _, k := range request.GetKeys() {
			if err := b.Delete([]byte(k)); err != nil {
				return grpcstatus.Errorf(codes.Internal, "could not delete entry %q", k)
			}
		}
		return bumpBucketToken(b)
	}); err != nil {
		return nil, err
	}

	return &rdpb.BatchDeleteResponse{}, nil
}

// BoltForceOpen attempts to open a BoltDB file with given parameters. If it
// fails because the file is corrupted, it deletes the file and creates it anew.
func BoltForceOpen(path string, mode os.FileMode, options *bolt.Options) (*bolt.DB, error) {
	bdb, err := bolt.Open(path, mode, options)
	if errors.Is(err, bolt.ErrInvalid) || errors.Is(err, bolt.ErrVersionMismatch) || errors.Is(err, bolt.ErrChecksum) {
		if removeErr := os.Remove(path); removeErr != nil {
			return nil, fmt.Errorf("cannot remove invalid BoltDB file %q: %v", path, removeErr)
		}
		return nil, fmt.Errorf("removed invalid BoltDB file %q, please restart your application", path)
	}
	if err != nil {
		return nil, fmt.Errorf("encountered unrecoverable error when opening BoltDB file %q, not attempting file deletion", path)
	}
	return bdb, nil
}

// CleanupFunc is a type that does cleanup work.
type CleanupFunc func()

// NewServer creates a new server using bolt-db opened using a file at path. If
// the file does not exist, it will be created. All entries are stored in the
// collection named bucket.
func NewServer(path string, buckets []string) (*Server, CleanupFunc, error) {
	boltOpts := bolt.DefaultOptions
	boltOpts.FreelistType = bolt.FreelistMapType
	db, err := BoltForceOpen(path, 0o600, boltOpts)
	if err != nil {
		return nil, nil, fmt.Errorf("could not open/create database file using path %q: %v", path, err)
	}

	// Create the bucket if it does not already exist.
	err = db.Update(func(tx *bolt.Tx) error {
		for _, bucket := range buckets {
			_, err := tx.CreateBucketIfNotExists([]byte(bucket))
			if err != nil {
				return fmt.Errorf("could not create bucket %q: %v", bucket, err)
			}
		}
		return nil
	})
	if err != nil {
		return nil, nil, err
	}

	return &Server{
		buckets: buckets,
		db:      db,
	}, func() { db.Close() }, nil
}
