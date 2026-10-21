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

// Package kvstore provides the Go interface and primitives for the
// KeyValueStore, providing an equivalent API to the C++ KeyValueStore class.
// The concrete implementation is in the pubsub package.
package kvstore

import (
	"context"
	"fmt"
	"path"
	"strings"
	"time"

	"intrinsic/platform/pubsub/golang/pubsubinterface"

	"google.golang.org/protobuf/proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

// MakeKey creates a key from an arbitrary number of strings, removing leading and
// trailing slashes, and joining them with a slash delimiter.
func MakeKey(parts ...string) string {
	// path.Join correctly removes leading and trailing slashes from all parts,
	// except the leading slash in the first part. TrimPrefix removes that
	// leading slash.
	return strings.TrimPrefix(path.Join(parts...), "/")
}

// VerificationMode defines the verification strategy for SetWithVerification.
type VerificationMode int

const (
	// HighConsistency blocks until the value is verified by reading it back and
	// checking if it matches the expected bytes.
	HighConsistency VerificationMode = iota

	// FirstReply returns as soon as any populated value is read from the key,
	// without verifying its contents.
	FirstReply
)

// SetWithVerificationOptions holds options for SetWithVerification.
type SetWithVerificationOptions struct {
	Mode VerificationMode
}

// SetWithVerificationOption is a functional option for SetWithVerification.
type SetWithVerificationOption func(*SetWithVerificationOptions)

// WithVerificationMode sets the verification mode.
func WithVerificationMode(mode VerificationMode) SetWithVerificationOption {
	return func(o *SetWithVerificationOptions) {
		o.Mode = mode
	}
}

// KVStore represents an instance of a KVStore object. It provides an
// interface that allows getting and setting key/value pairs.
type KVStore interface {
	// Set sets the value for the given key. The value is wrapped into anypb.Any
	// before it's written to the KV store.
	//
	// A key can't include any of the
	// following characters: /, *, ?, #, [ and ]. It will be namespaced according
	// to the settings provided in the NamespaceConfig.
	//
	// Passing highConsistency is deprecated: use SetWithVerification for verified writes,
	// or call Set with two arguments for unverified writes.
	// TODO(methyldragon): Remove the deprecated highConsistency parameter so the two-argument
	// Set(key, value) signature becomes the canonical unverified write API.
	Set(key string, value proto.Message, highConsistency ...bool) error

	// SetWithVerification sets the value for the given key and blocks until
	// verification completes according to the specified options (defaults to [HighConsistency]).
	// If ctx has no deadline (e.g. context.Background()), a default 30-second
	// verification timeout is applied.
	// Returns [ErrDeadlineExceeded] if the timeout expires.
	// Under HighConsistency mode, returns [ErrAborted] if concurrent modification is detected.
	SetWithVerification(ctx context.Context, key string, value proto.Message, opts ...SetWithVerificationOption) error

	// SetAny sets the value for the given key. The value is expected to be Any proto,
	// and it's written to the KV store as is.
	//
	// A key can't include any of the
	// following characters: /, *, ?, #, [ and ]. It will be namespaced according
	// to the settings provided in the NamespaceConfig.
	//
	// Passing highConsistency is deprecated: use SetAnyWithVerification for verified writes,
	// or call SetAny with two arguments for unverified writes.
	// TODO(methyldragon): Remove the deprecated highConsistency parameter so the two-argument
	// SetAny(key, valueAny) signature becomes the canonical unverified write API.
	SetAny(key string, valueAny *anypb.Any, highConsistency ...bool) error

	// SetAnyWithVerification sets the value for the given key and blocks until
	// verification completes according to the specified options (defaults to [HighConsistency]).
	// If ctx has no deadline (e.g. context.Background()), a default 30-second
	// verification timeout is applied.
	// Returns [ErrDeadlineExceeded] if the timeout expires.
	// Under HighConsistency mode, returns [ErrAborted] if concurrent modification is detected.
	SetAnyWithVerification(ctx context.Context, key string, valueAny *anypb.Any, opts ...SetWithVerificationOption) error

	// Get returns the value for the given key. Wildcard queries are not supported
	// with this method, use the GetAll method instead. If wildcard values are
	// sent as part of the config, an error is returned. Timeout may be nil.
	Get(key string, timeout *time.Duration) (*anypb.Any, error)

	// GetAll invokes the valueCallback for a given key that matches the expression.
	GetAll(key string, valueCallback func(*anypb.Any), ondoneCallback func(string)) (KVQuery, error)

	// ListAllKeys invokes the keyCallback for a given key that matches the expression.
	ListAllKeys(key string, keyCallback func(string), ondoneCallback func(string)) (KVQuery, error)

	// Delete removes a value from the store for the given key.
	Delete(key string) error

	// AdminCloudCopy copies local key-value pairs to the cloud key value
	// store.
	AdminCloudCopy(sourceKey string, targetKey string, timeout time.Duration) error

	// Subscribe creates a subscription to changes in the KV store.
	//
	// It assumes that all values whose keys match the given key expression have
	// the same type.
	//
	// Parameters:
	// - keyExpression - key expression to subscribe to.
	// - config - subscription configuration.
	// - exemplar - empty proto of the same type as values in the KV store.
	// - msgCallback - callback that will be called when a value is added or updated.
	// - deletionCallback - callback that will be called when a value is deleted.
	// - errCallback - callback that will be called in case of type mismatch.
	Subscribe(
		keyExpression string, config pubsubinterface.TopicConfig,
		exemplar proto.Message,
		msgCallback func(string, proto.Message),
		deletionCallback func(string),
		errCallback func(string, *anypb.Any, error)) (pubsubinterface.Subscription, error)

	// SubscribeToRawValues creates a subscription to changes in the KV store.
	//
	// It doesn't make any assumptions about types of values that match the given
	// key expression. The calling code is responsible for extracting those values
	// from `anypb.Any` protos and checking their type.
	//
	// Parameters:
	// - keyExpression - key expression to subscribe to.
	// - config - subscription configuration.
	// - msgCallback - callback that will be called when a value is added or updated.
	// - deletionCallback - callback that will be called when a value is deleted.
	SubscribeToRawValues(
		keyExpression string, config pubsubinterface.TopicConfig,
		msgCallback func(string, *anypb.Any),
		deletionCallback func(string)) (pubsubinterface.Subscription, error)

	// GetWorkcellReplicationNamespace returns a string that corresponds to the
	// namespace needed for the workcell's replicated namespace. Using this
	// namespace prefix for keyexprs will make the KVStore use replicated storage.
	//
	// This function may return an error because the namespace may not be available
	// immediately. The calling code should check whether the returned error is
	// kvstore.ErrNotFound, and retry.
	GetWorkcellReplicationNamespace() (string, error)

	// GetGlobalReplicationNamespace returns a string that corresponds to the
	// namespace needed for the global replicated namespace. Using this
	// namespace prefix for keyexprs will make the KVStore use replicated storage
	// available to all workcells within an organization.
	GetGlobalReplicationNamespace() string
}

// KVQuery is a handle for a GetALl KVStore query. Keep the handle alive to
// continue the query.
type KVQuery interface {
	// Close closes out the query
	Close()
}

var (
	// ErrDeadlineExceeded is returned if a deadline is exceeded.
	ErrDeadlineExceeded = fmt.Errorf("deadline exceeded")

	// ErrNotFound is returned if Get does not find any values.
	ErrNotFound = fmt.Errorf("not found")

	// ErrAborted is returned if the set operation was aborted due to a write race.
	ErrAborted = fmt.Errorf("aborted")
)
