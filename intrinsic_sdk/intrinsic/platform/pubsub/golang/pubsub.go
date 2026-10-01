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

// Package pubsub is a wrapper around the imw library.
//
// This package exposes the same pubsub interface as defined in
// intrinsic/platform/pubsub/pubsub.h
//
// To avoid circular dependencies, we split the core functionality of the Go
// bindings into two distinct packages:
//
//	pubsub:
//	  The concrete implementation of the bindings that call into the
//	  the imw library.
//
//	pubsubinterface:
//	  The high-level interface to pubsub exposed via the concrete
//	  implementation in pubsub.
//
// When using this suite of packages, you will likely need to use this package
// (pubsub) to instantiate a pubsub instance, but pubsubinterface should
// be used for type-level constructs.
package pubsub

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"runtime/cgo"
	"strings"
	"sync"
	"time"
	"unsafe"

	"intrinsic/platform/pubsub/golang/kvstore"
	"intrinsic/platform/pubsub/golang/pubsubinterface"

	"github.com/cenkalti/backoff/v4"
	log "github.com/golang/glog"
	"google.golang.org/protobuf/proto"

	pubsubpb "intrinsic/platform/pubsub/adapters/pubsub_go_proto"
	adminsetpb "intrinsic/platform/pubsub/admin_set_grpc/v1/admin_set_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
	timestamppb "google.golang.org/protobuf/types/known/timestamppb"

	workcellinfopb "intrinsic/platform/common/proto/workcell_info_go_proto"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

/*
#include <stdlib.h>  // for C.free
#include "intrinsic/platform/pubsub/golang/pubsub_c.h"

// We forward declare functions that will be implemented below so we can take their address from Go code.
void intrinsic_ImwSubscriptionCallback(void*, void*, size_t, void*);
void intrinsic_ImwQueryStaticCallback(void*, void*, size_t, void*);
void intrinsic_ImwQueryDoneStaticCallback(void*, void*);
void intrinsic_ImwQueryableStaticCallback(void*, void*, size_t, void*, void*);
void intrinsic_ImwLivelinessSubscriptionCallback(void*, bool, void*);
void intrinsic_ImwLivelinessGetCallback(void*, void*);
void intrinsic_ImwLivelinessOnDoneCallback(void*, void*);
*/
import "C"

var (
	zenohRouter = flag.String(
		"zenoh_router",
		"",
		"Override the default Zenoh connection to PROTOCOL/HOSTNAME:PORT")
	adminSetProxyEndpoint = flag.String(
		"admin_set_proxy_endpoint",
		"zenoh-router.app-intrinsic-base.svc.cluster.local:8081",
		"Override the default admin set proxy URL")
)

const (
	highConsistencyTimeout           = 30 * time.Second
	highConsistencyInitialGetTimeout = 10 * time.Second
	highConsistencyGetTimeout        = 100 * time.Millisecond
	highConsistencyRetryDelayMin     = 10 * time.Millisecond
	highConsistencyRetryDelayMax     = 2500 * time.Millisecond
	highConsistencyRetryDelayFactor  = 5
	defaultKeyPrefix                 = "kv_store"
	replicationKeyPrefix             = "kv_store_repl"
	workcellInfoKey                  = "workcell_info"
	globalReplicationNamespace       = "global"
)

// NewPubSub creates a new PubSub adapter if possible. Returns either a valid handle
// or an error, but not both. The caller is responsible for freeing up resources
// after use by calling Close() on the returned handle.
func NewPubSub() (*Handle, error) {
	zh, err := getZenohHandle()
	if err != nil {
		return nil, err
	}
	result := &Handle{
		zenohHandle: zh,
	}

	zenohConfig, err := getZenohPeerConfig()
	if err != nil {
		return nil, err
	}

	if err := result.zenohHandle.ImwInit(zenohConfig); err != nil {
		return nil, err
	}

	return result, nil
}

func addTopicPrefix(topic string) string {
	topicWithoutLeadingSlash := strings.TrimPrefix(topic, "/")
	if strings.HasPrefix(topicWithoutLeadingSlash, "interipc_ps/") {
		return topicWithoutLeadingSlash
	}
	return "in/" + topicWithoutLeadingSlash
}

func errorFromImwRet(imwRet C.int) error {
	switch imwRet {
	case 0:
		return nil
	case 1:
		return fmt.Errorf("imw returned an error")
	case 2:
		return fmt.Errorf("imw is not initialized")
	case 3:
		return fmt.Errorf("undefined")
	case 4:
		return fmt.Errorf("already exists")
	default:
		return fmt.Errorf("unknown error from imw")
	}
}

func validZenohKey(key string) error {
	if len(key) == 0 {
		return fmt.Errorf("Keyexpr must not be empty")
	}
	if strings.HasPrefix(key, "/") {
		return fmt.Errorf("Keyexpr must not start with /")
	}
	if strings.HasSuffix(key, "/") {
		return fmt.Errorf("Keyexpr must not end with /")
	}
	parts := strings.Split(key, "/")
	for _, part := range parts {
		if len(part) == 0 {
			return fmt.Errorf("Keyexpr must not contain empty parts")
		}
	}
	return nil
}

// Handle represents an instance of a Fast DDS pubsub adapter, as exposed via
// the interface defined in pubsubinterface.PubSub.
type Handle struct {
	mutex     sync.Mutex
	closeOnce sync.Once

	zenohHandle zenohHandle
}

var _ pubsubinterface.PubSub = new(Handle)

// getZenohPeerConfig retrieves the Zenoh configuration.
// We explicitly read the Go flag *zenohRouter and pass it to the CGO wrapper.
// This is required because the underlying C++ code relies on Abseil flags,
// which are not parsed when running inside a Go binary.
func getZenohPeerConfig() (string, error) {
	routerOverride := *zenohRouter
	cRouterOverride := C.CString(routerOverride)
	defer C.free(unsafe.Pointer(cRouterOverride))

	cZenohConfig := C.GetZenohPeerConfigWrapper(cRouterOverride)
	defer C.free(unsafe.Pointer(cZenohConfig))
	return C.GoString(cZenohConfig), nil
}

func topicConfigToZenohQos(config pubsubinterface.TopicConfig) (string, error) {
	switch config.Qos {
	case pubsubinterface.Sensor:
		return "Sensor", nil
	case pubsubinterface.HighReliability:
		return "HighReliability", nil
	default:
		return "UNKNOWN", fmt.Errorf("unknown QOS setting %v", config.Qos)
	}
}

// NewPublisher creates a publisher for a topic
func (ps *Handle) NewPublisher(topic string, config pubsubinterface.TopicConfig) (pubsubinterface.Publisher, error) {
	topicQos, err := topicConfigToZenohQos(config)
	if err != nil {
		return nil, err
	}

	publisher := &publisherHandle{topicName: topic, zenohHandle: ps.zenohHandle}
	if err := ps.zenohHandle.ImwCreatePublisher(addTopicPrefix(topic), topicQos); err != nil {
		return nil, err
	}
	return publisher, nil
}

// NewSubscription will create a subscription to the given topic, using the exemplar proto as the
// type expected to be called by the msgCallback.
// The errCallback is invoked when unmarshaling the payload fails; its first argument receives
// the raw packet bytes (as a string) and its second argument receives the unmarshal error.
func (ps *Handle) NewSubscription(topic string, config pubsubinterface.TopicConfig, exemplar proto.Message, msgCallback func(proto.Message), errCallback func(string, error)) (pubsubinterface.Subscription, error) {
	topicQos, err := topicConfigToZenohQos(config)
	if err != nil {
		return nil, err
	}

	subscription := &subscriptionHandle{
		topicName:     topic,
		fullTopicName: addTopicPrefix(topic),
		zenohHandle:   ps.zenohHandle,
		exemplar:      exemplar,
		callback: func(sub *subscriptionHandle, topic string, bytes []byte) {
			packet := &pubsubpb.PubSubPacket{}
			if err := proto.Unmarshal(bytes, packet); err != nil {
				log.Errorf("Failed to unmarshal packet: %v", err)
				return
			}

			msg := sub.exemplar.ProtoReflect().New().Interface()
			if err := packet.GetPayload().UnmarshalTo(msg); err != nil {
				errCallback(string(bytes), err)
				return
			}

			msgCallback(msg)
		},
	}
	subh := cgo.NewHandle(subscription)
	subscription.subHandle = subh

	if err := ps.zenohHandle.ImwCreateSubscription(subscription.fullTopicName, subscription, topicQos); err != nil {
		return nil, err
	}

	return subscription, nil
}

// NewRawSubscription will create a raw subscription to the given topic, passing the full packet to callback.
func (ps *Handle) NewRawSubscription(topic string, config pubsubinterface.TopicConfig, callback func(*pubsubpb.PubSubPacket)) (pubsubinterface.Subscription, error) {
	topicQos, err := topicConfigToZenohQos(config)
	if err != nil {
		return nil, err
	}

	subscription := &subscriptionHandle{
		topicName:     topic,
		fullTopicName: addTopicPrefix(topic),
		zenohHandle:   ps.zenohHandle,
		callback: func(sub *subscriptionHandle, topic string, bytes []byte) {
			packet := &pubsubpb.PubSubPacket{}
			if err := proto.Unmarshal(bytes, packet); err != nil {
				log.Errorf("Failed to unmarshal packet: %v", err)
				return
			}

			callback(packet)
		},
	}
	subh := cgo.NewHandle(subscription)
	subscription.subHandle = subh

	if err := ps.zenohHandle.ImwCreateSubscription(subscription.fullTopicName, subscription, topicQos); err != nil {
		return nil, err
	}

	return subscription, nil
}

// DeclareLivelinessToken declares a liveliness token on the given key expression.
//
// Liveliness is a feature of Zenoh that allows participants of a Zenoh
// network to keep track of each other's availability. Participants announce
// their availability by declaring a liveliness token on a key expression.
// Other participants can subscribe to notifications about changes in the
// availability status by calling `CreateLivelinessSubscription`.
// Subscribers will receive notifications when the following events occur:
//   - The participant that declared a liveliness token becomes unavailable
//     for any reason, such as a network partition or a process crash.
//   - The participant that declared a liveliness token drops that token
//     (see `DropLivelinessToken` method).
//   - The participant that was unavailable becomes available again.
func (ps *Handle) DeclareLivelinessToken(keyExpr string) error {
	return ps.zenohHandle.ImwDeclareLivelinessToken(keyExpr)
}

// DropLivelinessToken drops the liveliness token that was declared on the given key expression.
func (ps *Handle) DropLivelinessToken(keyExpr string) error {
	return ps.zenohHandle.ImwDropLivelinessToken(keyExpr)
}

// NewLivelinessSubscription subscribes to liveliness notifications matching the given key expression.
//
// Parameters:
//   - keyExpr - key expression to subscribe to.
//   - notifyAboutExistingTokens - whether to receive notifications about tokens that had been declared before NewLivelinessSubscription was called.
//   - msgCallback - called when state of a liveliness token declared on a matching key expression changes.
//     Callback parameters:
//   - key - key expression whose liveliness token's state has changed.
//   - alive - whether that token is currently alive.
func (ps *Handle) NewLivelinessSubscription(keyExpr string, notifyAboutExistingTokens bool, msgCallback func(string, bool)) (pubsubinterface.LivelinessSubscription, error) {
	subscription := &livelinessSubscriptionHandle{
		keyExpr:     keyExpr,
		zenohHandle: ps.zenohHandle,
		callback: func(sub *livelinessSubscriptionHandle, key string, alive bool) {
			msgCallback(key, alive)
		},
	}
	subh := cgo.NewHandle(subscription)
	subscription.subHandle = subh

	if err := ps.zenohHandle.ImwCreateLivelinessSubscription(subscription.keyExpr, notifyAboutExistingTokens, subscription); err != nil {
		return nil, err
	}

	return subscription, nil
}

func (q *livelinessQueryHandle) Close() {
	q.closeOnce.Do(func() {
		q.queryHandle.Delete()
	})
}

// LivelinessGet fetches currently available liveliness tokens matching the given key expression.
// Returns immediately. Liveliness tokens are passed to the caller via callbacks.
//
// Parameters:
//   - keyExpr - key expression for matching liveliness tokens.
//   - callback - function that is called when Zenoh finds a liveliness token.
//     It is called once for each token, and may be called in a different goroutine.
//     The function takes the key expression on which the token was declared.
//   - onDone - function that is called when Zenoh finds all liveliness tokens.
//     It may be called in a different goroutine, but it will be called after all
//     currently running callbacks complete.
//     The function's argument is the key expression that was passed to LivelinessGet.
func (ps *Handle) LivelinessGet(keyExpr string, callback func(string), onDone func(string)) (pubsubinterface.LivelinessGetQuery, error) {
	query := &livelinessQueryHandle{
		callback: callback,
		onDone:   onDone,
	}
	qh := cgo.NewHandle(query)
	query.queryHandle = qh

	if err := ps.zenohHandle.ImwLivelinessGet(keyExpr, query); err != nil {
		return nil, err
	}

	return query, nil
}

// LivelinessGetAllSynchronous fetches currently available liveliness tokens matching the given key expression.
// Blocks until all tokens are found.
//
// Parameters:
//   - keyExpr - key expression for matching liveliness tokens.
func (ps *Handle) LivelinessGetAllSynchronous(keyExpr string) ([]string, error) {
	result := []string{}
	doneCh := make(chan struct{})

	callback := func(key string) {
		result = append(result, key)
	}

	onDone := func(keyexpr string) {
		doneCh <- struct{}{}
	}

	query, err := ps.LivelinessGet(keyExpr, callback, onDone)
	if err != nil {
		return nil, err
	}
	defer query.Close()

	// Waiting for the `onDone` callback to be called.
	// Since the timeout is enforced by Zenoh, we don't need context.WithTimeout.
	// We can simply wait for a value to appear in doneCh.
	log.Info("[LivelinessGetAllSynchronous] Waiting for the onDone callback")
	<-doneCh
	log.Info("[LivelinessGetAllSynchronous] Done waiting")

	return result, nil
}

// NewKVStoreSubscription creates a subscription to the given key that is stored in the KV store.
//
// This function differs from other NewXXXXSubscription functions in the following ways:
//   - It doesn't modify the key in any way, e.g. doesn't add the `in/` prefix we use for pub/sub.
//   - When the value corresponding to the given key changes, this function expects to receive `anypb.Any`,
//     not `pubsubpb.PubSubPacket`.
func (ps *Handle) NewKVStoreSubscription(key string, config pubsubinterface.TopicConfig, msgCallback func(string, *anypb.Any), deletionCallback func(string)) (pubsubinterface.Subscription, error) {
	log.Infof("KVStore Subscribe for key: %s", key)
	topicQos, err := topicConfigToZenohQos(config)
	if err != nil {
		return nil, err
	}

	subscription := &subscriptionHandle{
		topicName:     key,
		fullTopicName: key, // Not adding a prefix
		zenohHandle:   ps.zenohHandle,
		exemplar:      &anypb.Any{},
		callback: func(sub *subscriptionHandle, key string, bytes []byte) {
			if len(bytes) == 0 {
				deletionCallback(key)
				return
			}
			any := &anypb.Any{} // It's Any, not a PubSubPacket.
			if err := proto.Unmarshal(bytes, any); err != nil {
				log.Errorf("Failed to unmarshal packet: %v", err)
				return
			}
			msgCallback(key, any)
		},
	}
	subh := cgo.NewHandle(subscription)
	subscription.subHandle = subh

	if err := ps.zenohHandle.ImwCreateSubscription(key, subscription, topicQos); err != nil {
		return nil, err
	}

	return subscription, nil
}

// KVStore returns an interface to the KVStore associated with this pubsub
// instance.
func (ps *Handle) KVStore() kvstore.KVStore {
	return &kvStoreHandle{ps: ps, zenohHandle: ps.zenohHandle}
}

// KVStoreReplicated returns an interface to the replicated KVStore associated with this pubsub
// instance.
func (ps *Handle) KVStoreReplicated() kvstore.KVStore {
	return &kvStoreHandle{ps: ps, zenohHandle: ps.zenohHandle, keyPrefix: replicationKeyPrefix}
}

func (ps *Handle) ReplicatedKVStoreForWorkcell() (kvstore.KVStore, error) {
	localKVStore := ps.KVStore()
	any, err := localKVStore.Get(workcellInfoKey, nil /* timeout */)
	if err != nil {
		return nil, err
	}

	workcellInfo := &workcellinfopb.WorkcellInfo{}
	if err := any.UnmarshalTo(workcellInfo); err != nil {
		return nil, err
	}

	prefix := fmt.Sprintf("%s/%s", replicationKeyPrefix, workcellInfo.WorkcellName)
	return ps.KVStoreWithPrefix(prefix), nil
}

func (ps *Handle) GlobalReplicatedKVStore() kvstore.KVStore {
	return ps.KVStoreWithPrefix(fmt.Sprintf("%s/global", replicationKeyPrefix))
}

// KVStoreWithPrefix returns an interface to the KVStore associated with this pubsub
// but default key prefixes are overridden with the given prefix.
func (ps *Handle) KVStoreWithPrefix(prefix string) kvstore.KVStore {
	return &kvStoreHandle{ps: ps, zenohHandle: ps.zenohHandle, keyPrefix: prefix}
}

// Close the PubSub connection and free associated resources.
// Note: The underlying Zenoh session is kept alive by default for the lifetime
// of the process.
func (ps *Handle) Close() {
	ps.closeOnce.Do(func() {
		_ = ps.zenohHandle.ImwFini()
	})
}

type subscriptionHandle struct {
	topicName     string
	fullTopicName string
	zenohHandle   zenohHandle
	callback      func(sub *subscriptionHandle, topic string, bytes []byte)

	callbackPtr unsafe.Pointer
	exemplar    proto.Message

	subHandle cgo.Handle
	closeOnce sync.Once
}

func (s *subscriptionHandle) TopicName() string { return s.topicName }

func (s *subscriptionHandle) Close() {
	s.closeOnce.Do(func() {
		if err := s.zenohHandle.ImwDestroySubscription(s.fullTopicName, s); err != nil {
			panic(err)
		}
		s.subHandle.Delete()
	})
}

// livelinessSubscriptionHandle is a handle for a subscription to
// liveliness notifications.
type livelinessSubscriptionHandle struct {
	keyExpr     string
	zenohHandle zenohHandle
	callback    func(sub *livelinessSubscriptionHandle, key string, alive bool)

	callbackPtr unsafe.Pointer

	subHandle cgo.Handle
	closeOnce sync.Once
}

// Close unsubscribes from liveliness notifications.
func (s *livelinessSubscriptionHandle) Close() {
	s.closeOnce.Do(func() {
		if err := s.zenohHandle.ImwDestroyLivelinessSubscription(s.keyExpr, s); err != nil {
			panic(err)
		}
		s.subHandle.Delete()
	})
}

// livelinessQueryHandle is a handle for LivelinessGet queries.
type livelinessQueryHandle struct {
	callback func(key string)
	onDone   func(keyexpr string)

	queryHandle cgo.Handle
	closeOnce   sync.Once
}

type publisherHandle struct {
	topicName   string
	zenohHandle zenohHandle
	closeOnce   sync.Once
}

func (p *publisherHandle) TopicName() string { return p.topicName }

func (p *publisherHandle) PublishAny(msg *anypb.Any) error {
	packet := &pubsubpb.PubSubPacket{
		PublishTime: timestamppb.New(time.Now()),
		Payload:     msg,
	}

	return p.publishPacket(packet)
}

func (p *publisherHandle) Publish(msg proto.Message) error {
	packet := &pubsubpb.PubSubPacket{
		PublishTime: timestamppb.New(time.Now()),
		Payload:     &anypb.Any{},
	}

	if err := packet.GetPayload().MarshalFrom(msg); err != nil {
		return err
	}

	return p.publishPacket(packet)
}

func (p *publisherHandle) publishPacket(packet *pubsubpb.PubSubPacket) error {
	bytes, err := proto.Marshal(packet)
	if err != nil {
		return err
	}
	return p.zenohHandle.ImwPublish(addTopicPrefix(p.topicName), bytes)
}

func (p *publisherHandle) HasMatchingSubscribers() (bool, error) {
	return p.zenohHandle.ImwPublisherHasMatchingSubscribers(addTopicPrefix(p.topicName))
}

func (p *publisherHandle) Close() {
	p.closeOnce.Do(func() {
		if err := p.zenohHandle.ImwDestroyPublisher(addTopicPrefix(p.topicName)); err != nil {
			panic(err)
		}
	})
}

type kvStoreHandle struct {
	ps          *Handle
	zenohHandle zenohHandle
	keyPrefix   string
}

func (kv *kvStoreHandle) addKeyPrefix(key string) string {
	keyPrefix := defaultKeyPrefix
	if kv.keyPrefix != "" {
		keyPrefix = kv.keyPrefix
	}
	if key[0] == '/' {
		return keyPrefix + key
	}
	return keyPrefix + "/" + key
}

func (kv *kvStoreHandle) Set(key string, value proto.Message, highConsistency bool) error {
	valueAny, ok := value.(*anypb.Any)
	if !ok {
		var err error
		valueAny, err = anypb.New(value)
		if err != nil {
			return err
		}
	}

	return kv.SetAny(key, valueAny, highConsistency)
}

func (kv *kvStoreHandle) SetAny(key string, valueAny *anypb.Any, highConsistency bool) error {
	prefixedKey := kv.addKeyPrefix(key)
	log.Infof("KVStore Set for key: %s", prefixedKey)
	valueBytes, err := proto.Marshal(valueAny)
	if err != nil {
		return err
	}

	var initialValue *anypb.Any
	if highConsistency {
		timeout := highConsistencyInitialGetTimeout
		var err error
		initialValue, err = kv.Get(key, &timeout)
		if err != nil && !errors.Is(err, kvstore.ErrNotFound) {
			// If the initial read fails due to a transient error (e.g. deadline exceeded),
			// we intentionally leave initialValue empty (nil). This ensures that if
			// the key was already populated, our conflict check later will see the pre-existing
			// value and abort the operation. The caller can then retry the set idempotently.
			// This is preferred over skipping the conflict check.
			log.Warningf("Initial read failed during high consistency check for key %q: %v", key, err)
			initialValue = nil
		}
	}

	if err := kv.zenohHandle.ImwSet(kv.addKeyPrefix(key), valueBytes); err != nil {
		return err
	}

	if highConsistency {
		return kv.waitForHighConsistency(context.Background(), key, valueAny, initialValue)
	}
	return nil
}

// fetchCurrentState returns the current state of a key based on the output of a Get operation.
// Returning (nil, nil) represents an unset key (kvstore.ErrNotFound), while returning
// a non-nil error indicates a transient failure or unexpected state.
func fetchCurrentState(key string, currentValue *anypb.Any, err error) (*anypb.Any, error) {
	switch {
	case err == nil:
		// Value exists.
		return currentValue, nil
	case errors.Is(err, kvstore.ErrNotFound):
		// Value does not exist (unset).
		return nil, nil
	case errors.Is(err, kvstore.ErrDeadlineExceeded):
		// Transient network timeout reading value. Propagate to retry.
		return nil, err
	default:
		return nil, fmt.Errorf("failed to read current state for key %q during high consistency check: %w", key, err)
	}
}

// waitForHighConsistency polls the KVStore until the specified key converges to the given valueAny.
//
// It uses exponential backoff internally via the backoff library and fails if the operation
// takes longer than highConsistencyTimeout.
// It aborts with kvstore.ErrAborted if the value changes to a value other than the newly-written
// valueAny, indicating a race condition with other writers (using initialValue to know what the
// key started with).
//
// Returns kvstore.ErrDeadlineExceeded if the value did not converge in time.
func (kv *kvStoreHandle) waitForHighConsistency(parentCtx context.Context, key string, valueAny *anypb.Any, initialValue *anypb.Any) error {
	if parentCtx == nil {
		parentCtx = context.Background()
	}
	ctx, cancel := context.WithTimeout(parentCtx, highConsistencyTimeout)
	defer cancel()

	b := backoff.NewExponentialBackOff()
	b.InitialInterval = highConsistencyRetryDelayMin
	b.MaxInterval = highConsistencyRetryDelayMax
	b.Multiplier = highConsistencyRetryDelayFactor
	b.MaxElapsedTime = 0 // Handled by ctx timeout

	operation := func() error {
		timeout := highConsistencyGetTimeout
		currentValue, err := kv.Get(key, &timeout)

		currentState, err := fetchCurrentState(key, currentValue, err)
		if err != nil {
			if errors.Is(err, kvstore.ErrDeadlineExceeded) {
				return err // Retryable
			}
			return backoff.Permanent(err)
		}

		if proto.Equal(currentState, valueAny) {
			// Key value is committed.
			return nil
		}

		if !proto.Equal(currentState, initialValue) {
			return backoff.Permanent(fmt.Errorf("value for key %q was modified by another process while waiting for high consistency: %w", key, kvstore.ErrAborted))
		}

		return kvstore.ErrDeadlineExceeded
	}

	if err := backoff.Retry(operation, backoff.WithContext(b, ctx)); err != nil {
		// Check for permanent errors first before overriding with the timeout error.
		if !errors.Is(err, kvstore.ErrDeadlineExceeded) && !errors.Is(err, context.DeadlineExceeded) {
			return err
		}

		if errors.Is(ctx.Err(), context.DeadlineExceeded) || errors.Is(err, context.DeadlineExceeded) || errors.Is(err, kvstore.ErrDeadlineExceeded) {
			return fmt.Errorf("timeout waiting for high consistency for key %q: %w", key, kvstore.ErrDeadlineExceeded)
		}
		return err
	}
	return nil
}

type queryHandle struct {
	query func(keyexpr string, bytes []byte)
	done  func(keyexpr string)

	handle    cgo.Handle
	closeOnce sync.Once
}

func (q *queryHandle) Close() {
	q.closeOnce.Do(func() {
		q.handle.Delete()
	})
}

type queryableHandle struct {
	zenohHandle zenohHandle
	keyexpr     string
	callback    func(key string, query []byte, context unsafe.Pointer)
	handle      cgo.Handle
	closeOnce   sync.Once
}

func (q *queryableHandle) Reply(key string, context unsafe.Pointer, reply proto.Message) error {
	replyAny, ok := reply.(*anypb.Any)
	if !ok {
		var err error
		replyAny, err = anypb.New(reply)
		if err != nil {
			return err
		}
	}
	bytes, err := proto.Marshal(replyAny)
	if err != nil {
		return err
	}
	return q.zenohHandle.ImwQueryableReply(context, key, bytes)
}

func (q *queryableHandle) Close() {
	q.closeOnce.Do(func() {
		if err := q.zenohHandle.ImwDestroyQueryable(q.keyexpr, q); err != nil {
			panic(err)
		}
		q.handle.Delete()
	})
}

//export intrinsic_ImwQueryableStaticCallback
func intrinsic_ImwQueryableStaticCallback(keyexpr unsafe.Pointer, queryBytes unsafe.Pointer, queryBytesLen C.size_t, queryContext unsafe.Pointer, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	qh := h.Value().(*queryableHandle)
	qh.callback(C.GoString((*C.char)(keyexpr)), C.GoBytes(queryBytes, C.int(queryBytesLen)), queryContext)
}

func (ps *Handle) CreateQueryable(keyexpr string, callback func(string, []byte, unsafe.Pointer)) (*queryableHandle, error) {
	qh := &queryableHandle{
		zenohHandle: ps.zenohHandle,
		keyexpr:     keyexpr,
		callback:    callback,
	}
	qh.handle = cgo.NewHandle(qh)
	if err := ps.zenohHandle.ImwCreateQueryable(keyexpr, qh, false); err != nil {
		qh.handle.Delete()
		return nil, err
	}
	return qh, nil
}

func (kv *kvStoreHandle) GetAll(key string, valueCallback func(*anypb.Any), ondoneCallback func(string)) (kvstore.KVQuery, error) {
	queryCallback := func(keyexpr string, bytes []byte) {
		valueCallback(value(bytes))
	}
	return kv.query(kv.addKeyPrefix(key), queryCallback, ondoneCallback)
}

func (kv *kvStoreHandle) ListAllKeys(key string, keyCallback func(string), ondoneCallback func(string)) (kvstore.KVQuery, error) {
	queryCallback := func(keyexpr string, bytes []byte) {
		keyCallback(keyexpr)
	}
	return kv.query(kv.addKeyPrefix(key), queryCallback, ondoneCallback)
}

func value(bytes []byte) *anypb.Any {
	value := &anypb.Any{}
	if err := proto.Unmarshal(bytes, value); err != nil {
		panic(err)
	}
	return value
}

// query invokes the queryCallback for each given key that matches the expression.
// The key expression is used as-is, i.e. no prefixes are added to it.
//
// This function is not exported because the raw keys contain prefixes not known
// to the client code, such as `kv_store` or `kv_store_repl`.
func (kv *kvStoreHandle) query(rawKey string, queryCallback func(keyexpr string, bytes []byte), ondoneCallback func(string)) (kvstore.KVQuery, error) {
	var qh *queryHandle
	qh = &queryHandle{
		query: queryCallback,
		done: func(keyexpr string) {
			ondoneCallback(keyexpr)
		},
	}
	qh.handle = cgo.NewHandle(qh)

	if err := kv.zenohHandle.ImwQuery(rawKey, qh); err != nil {
		return nil, err
	}

	return qh, nil
}

//export intrinsic_ImwQueryStaticCallback
func intrinsic_ImwQueryStaticCallback(keyexpr unsafe.Pointer, responseBytes unsafe.Pointer, responseBytesLen C.size_t, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	callbacks := h.Value().(*queryHandle)
	callbacks.query(C.GoString((*C.char)(keyexpr)), C.GoBytes(responseBytes, C.int(responseBytesLen)))
}

//export intrinsic_ImwQueryDoneStaticCallback
func intrinsic_ImwQueryDoneStaticCallback(keyexpr unsafe.Pointer, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	callbacks := h.Value().(*queryHandle)
	callbacks.done(C.GoString((*C.char)(keyexpr)))
}

func (kv *kvStoreHandle) Get(key string, timeout *time.Duration) (*anypb.Any, error) {
	prefixedKey := kv.addKeyPrefix(key)
	log.Infof("KVStore Get for key: %s", prefixedKey)
	return kv.getRaw(prefixedKey, timeout)
}

// getRaw returns the value for the given key.
// The key is used as-is, i.e. no prefixes are added to it.
//
// This function is not exported because the raw keys contain prefixes not known
// to the client code, such as `kv_store` or `kv_store_repl`.
func (kv *kvStoreHandle) getRaw(rawKey string, timeout *time.Duration) (*anypb.Any, error) {
	ctx := context.Background()

	if timeout != nil {
		ctx, _ = context.WithTimeout(ctx, *timeout)
	}

	// queryResult represents the final outcome of processing a query.
	// That outcome may be a value obtained from the KV store, or an error.
	type queryResult struct {
		result *anypb.Any
		err    error
	}

	// Channel for query results.
	resultCh := make(chan *queryResult)

	// Channel for notifying the goroutine executing the query that the query can be
	// closed. The query can only be closed after it completes, so a value is pushed
	// to this channel at the end of onDoneCallback.
	canCloseQueryCh := make(chan struct{}, 1)

	var once sync.Once

	sendFirstQueryResult := func(result *anypb.Any, err error) {
		once.Do(func() {
			resultCh <- &queryResult{result: result, err: err}
		})
	}

	queryCallback := func(keyexpr string, bytes []byte) {
		// The first received result will be the one returned to the client.
		// When the `key` parameter is really a key (not a keyexpr with
		// wildcards), then this behavior is always correct.
		// If the `key` parameter contains wildcards, then the first value
		// passed to this callback will be returned to the caller of `Get`.
		sendFirstQueryResult(value(bytes), nil /* error */)
	}

	onDoneCallback := func(keyexpr string) {
		sendFirstQueryResult(
			nil, /* result */
			fmt.Errorf("%q not found: %w", rawKey, kvstore.ErrNotFound))
		canCloseQueryCh <- struct{}{}
	}

	// Starting the query in a separate goroutine. Callbacks will be called
	// in that goroutine.
	go func() {
		query, err := kv.query(rawKey, queryCallback, onDoneCallback)
		if err != nil {
			sendFirstQueryResult(nil /* result */, err)
			return
		}
		// When a value for the given key exists in the KV store,
		// kv.query is blocks until the query completes. But when there is
		// no value, kv.query may return before onDoneCallback is called.
		// We need to keep the query alive until all callbacks are called,
		// otherwise, we'll get a "use of invalid handle" error.
		<-canCloseQueryCh
		query.Close()
	}()

	for {
		select {
		case result := <-resultCh:
			return result.result, result.err
		case _ = <-ctx.Done():
			go sendFirstQueryResult(
				nil, /* result */
				fmt.Errorf("timeout waiting for %q: %w", rawKey, kvstore.ErrDeadlineExceeded))
		}
	}
}

func (kv *kvStoreHandle) Delete(key string) error {
	prefixedKey := kv.addKeyPrefix(key)
	log.Infof("KVStore Delete for key: %s", prefixedKey)

	cPrefixedKey := C.CString(prefixedKey)
	defer C.free(unsafe.Pointer(cPrefixedKey))

	if res := C.ZenohHandleImwDeleteKeyExpr(kv.zenohHandle.Ptr(), cPrefixedKey); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

func (kv *kvStoreHandle) AdminCloudCopy(sourceKey string, targetKey string, timeout time.Duration) error {
	if err := validZenohKey(sourceKey); err != nil {
		return err
	}
	if err := validZenohKey(targetKey); err != nil {
		return err
	}
	value, err := kv.Get(sourceKey, &timeout)
	if err != nil {
		log.Errorf("Failed to get value for %q", sourceKey)
		return err
	}

	conn, err := grpc.Dial(*adminSetProxyEndpoint, grpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		return fmt.Errorf("failed to create channel: %w", err)
	}
	defer conn.Close()

	client := adminsetpb.NewAdminSetServiceClient(conn)

	req := &adminsetpb.AdminSetRequest{
		Key:       targetKey,
		Value:     value,
		TimeoutMs: timeout.Milliseconds(),
	}

	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	_, err = client.AdminCopy(ctx, req)
	if err != nil {
		return fmt.Errorf("gRPC call failed: %w", err)
	}

	return nil
}

func (kv *kvStoreHandle) Subscribe(
	keyExpression string, config pubsubinterface.TopicConfig, exemplar proto.Message,
	msgCallback func(string, proto.Message),
	deletionCallback func(string),
	errCallback func(string, *anypb.Any, error),
) (pubsubinterface.Subscription, error) {
	typeCheckingCallback := func(key string, any *anypb.Any) {
		msg := exemplar.ProtoReflect().New().Interface()
		if err := any.UnmarshalTo(msg); err != nil {
			errCallback(key, any, err)
			return
		}
		msgCallback(key, msg)
	}

	return kv.ps.NewKVStoreSubscription(
		kv.addKeyPrefix(keyExpression),
		config,
		typeCheckingCallback,
		deletionCallback,
	)
}

func (kv *kvStoreHandle) SubscribeToRawValues(
	keyExpression string, config pubsubinterface.TopicConfig,
	msgCallback func(string, *anypb.Any),
	deletionCallback func(string),
) (pubsubinterface.Subscription, error) {
	return kv.ps.NewKVStoreSubscription(
		kv.addKeyPrefix(keyExpression),
		config,
		msgCallback,
		deletionCallback,
	)
}

func (kv *kvStoreHandle) GetWorkcellReplicationNamespace() (string, error) {
	key := fmt.Sprintf("%s/%s", defaultKeyPrefix, workcellInfoKey)
	any, err := kv.getRaw(key, nil /* timeout */)
	if err != nil {
		return "", err
	}

	workcellInfo := &workcellinfopb.WorkcellInfo{}
	if err := any.UnmarshalTo(workcellInfo); err != nil {
		return "", err
	}
	return workcellInfo.WorkcellName, nil
}

func (kv *kvStoreHandle) GetGlobalReplicationNamespace() string {
	return globalReplicationNamespace
}

type zenohHandle interface {
	ImwInit(config string) error
	ImwFini() error
	ImwDestroySessionWhenUnused() error
	ImwCreatePublisher(keyExpr string, qos string) error
	ImwDestroyPublisher(keyExpr string) error
	ImwPublish(keyExpr string, bytes []byte) error
	ImwPublisherHasMatchingSubscribers(keyExpr string) (bool, error)
	ImwCreateSubscription(keyExpr string, sub *subscriptionHandle, qos string) error
	ImwDestroySubscription(keyExpr string, sub *subscriptionHandle) error
	ImwDeclareLivelinessToken(keyExpr string) error
	ImwDropLivelinessToken(keyExpr string) error
	ImwCreateLivelinessSubscription(keyExpr string, notifyAboutExistingTokens bool, sub *livelinessSubscriptionHandle) error
	ImwDestroyLivelinessSubscription(keyExpr string, sub *livelinessSubscriptionHandle) error
	ImwLivelinessGet(keyExpr string, query *livelinessQueryHandle) error
	ImwSet(keyExpr string, value []byte) error
	ImwQuery(keyExpr string, query *queryHandle) error
	ImwCreateQueryable(keyExpr string, queryable *queryableHandle, isRosService bool) error
	ImwDestroyQueryable(keyExpr string, queryable *queryableHandle) error
	ImwQueryableReply(queryContext unsafe.Pointer, keyExpr string, reply []byte) error
	Ptr() unsafe.Pointer
}

type zenohHandleImpl struct {
	ptr unsafe.Pointer
}

var (
	globalZenohHandle *zenohHandleImpl
	zenohHandleMutex  sync.Mutex
)

func getZenohHandle() (zenohHandle, error) {
	zenohHandleMutex.Lock()
	defer zenohHandleMutex.Unlock()

	if globalZenohHandle == nil {
		ptr := C.NewZenohHandle()
		if ptr == nil {
			return nil, fmt.Errorf("failed to create Zenoh handle")
		}

		globalZenohHandle = &zenohHandleImpl{
			ptr: ptr,
		}
	}

	return globalZenohHandle, nil
}

// DestroySessionWhenUnused hints that the Zenoh session should be closed when
// no active PubSub handles or child entities (publishers, subscriptions,
// queryables) remain.
func DestroySessionWhenUnused() error {
	zenohHandleMutex.Lock()
	zh := globalZenohHandle
	zenohHandleMutex.Unlock()
	if zh == nil {
		return nil
	}
	return zh.ImwDestroySessionWhenUnused()
}

func (z *zenohHandleImpl) ImwDestroySessionWhenUnused() error {
	if res := C.ZenohHandleImwDestroySessionWhenUnused(z.ptr); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

// String type is no bueno here, pass a struct
func (z *zenohHandleImpl) ImwInit(config string) error {
	configString := C.CString(config)
	defer C.free(unsafe.Pointer(configString))
	if res := C.ZenohHandleImwInit(z.ptr, configString); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwFini() error {
	if res := C.ZenohHandleImwFini(z.ptr); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwCreatePublisher(keyExpr string, qos string) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))
	qosString := C.CString(qos)
	defer C.free(unsafe.Pointer(qosString))

	if res := C.ZenohHandleImwCreatePublisher(z.ptr, keyExprString, qosString); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

func (z *zenohHandleImpl) ImwDestroyPublisher(keyExpr string) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	if res := C.ZenohHandleImwDestroyPublisher(z.ptr, keyExprString); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

func (z *zenohHandleImpl) ImwPublish(keyExpr string, bytes []byte) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	// Avoid the malloc = copy here!
	bytesArray := C.CBytes(bytes)
	defer C.free(bytesArray)

	if res := C.ZenohHandleImwPublish(z.ptr, keyExprString, bytesArray, C.size_t(len(bytes))); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

func (z *zenohHandleImpl) ImwPublisherHasMatchingSubscribers(keyExpr string) (bool, error) {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	cbool := C.bool(false)
	if res := C.ZenohHandleImwPublisherHasMatchingSubscribers(z.ptr, keyExprString, &cbool); res != 0 {
		return false, errorFromImwRet(res)
	}

	return bool(cbool), nil
}

//export intrinsic_ImwSubscriptionCallback
func intrinsic_ImwSubscriptionCallback(keyexpr unsafe.Pointer, bytes unsafe.Pointer, bytesLen C.size_t, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	sub := h.Value().(*subscriptionHandle)
	sub.callback(sub, C.GoString((*C.char)(keyexpr)), C.GoBytes(bytes, C.int(bytesLen)))
}

func (z *zenohHandleImpl) ImwCreateSubscription(keyExpr string, sub *subscriptionHandle, qos string) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	qosString := C.CString(qos)
	defer C.free(unsafe.Pointer(qosString))

	if res := C.ZenohHandleImwCreateSubscription(z.ptr, inKeyExprString, C.zenoh_handle_imw_subscription_callback_fn(C.intrinsic_ImwSubscriptionCallback), qosString, unsafe.Pointer(&sub.subHandle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwDestroySubscription(keyExpr string, sub *subscriptionHandle) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwDestroySubscription(z.ptr, inKeyExprString, C.zenoh_handle_imw_subscription_callback_fn(C.intrinsic_ImwSubscriptionCallback), unsafe.Pointer(&sub.subHandle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwDeclareLivelinessToken(keyExpr string) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwDeclareLivelinessToken(z.ptr, inKeyExprString); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwDropLivelinessToken(keyExpr string) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwDropLivelinessToken(z.ptr, inKeyExprString); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

//export intrinsic_ImwLivelinessSubscriptionCallback
func intrinsic_ImwLivelinessSubscriptionCallback(keyexpr unsafe.Pointer, alive C.bool, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	sub := h.Value().(*livelinessSubscriptionHandle)
	sub.callback(sub, C.GoString((*C.char)(keyexpr)), bool(alive))
}

func (z *zenohHandleImpl) ImwCreateLivelinessSubscription(keyExpr string, notifyAboutExistingTokens bool, sub *livelinessSubscriptionHandle) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwCreateLivelinessSubscription(
		z.ptr,
		inKeyExprString,
		C.zenoh_handle_imw_liveliness_callback_fn(C.intrinsic_ImwLivelinessSubscriptionCallback),
		C.bool(notifyAboutExistingTokens),
		unsafe.Pointer(&sub.subHandle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwDestroyLivelinessSubscription(keyExpr string, sub *livelinessSubscriptionHandle) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwDestroyLivelinessSubscription(
		z.ptr,
		inKeyExprString,
		C.zenoh_handle_imw_liveliness_callback_fn(C.intrinsic_ImwLivelinessSubscriptionCallback),
		unsafe.Pointer(&sub.subHandle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

//export intrinsic_ImwLivelinessGetCallback
func intrinsic_ImwLivelinessGetCallback(key unsafe.Pointer, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	q := h.Value().(*livelinessQueryHandle)
	q.callback(C.GoString((*C.char)(key)))
}

//export intrinsic_ImwLivelinessOnDoneCallback
func intrinsic_ImwLivelinessOnDoneCallback(keyexpr unsafe.Pointer, userContext unsafe.Pointer) {
	if userContext == nil {
		return
	}
	h := *(*cgo.Handle)(userContext)
	q := h.Value().(*livelinessQueryHandle)
	q.onDone(C.GoString((*C.char)(keyexpr)))
}

func (z *zenohHandleImpl) ImwLivelinessGet(keyExpr string, query *livelinessQueryHandle) error {
	inKeyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(inKeyExprString))

	if res := C.ZenohHandleImwLivelinessGet(
		z.ptr,
		inKeyExprString,
		C.zenoh_handle_imw_liveliness_get_callback_fn(C.intrinsic_ImwLivelinessGetCallback),
		C.zenoh_handle_imw_liveliness_on_done_callback_fn(C.intrinsic_ImwLivelinessOnDoneCallback),
		unsafe.Pointer(&query.queryHandle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwSet(keyExpr string, value []byte) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	bytesArray := C.CBytes(value)
	defer C.free(bytesArray)

	if res := C.ZenohHandleImwSet(z.ptr, keyExprString, bytesArray, C.size_t(len(value))); res != 0 {
		return errorFromImwRet(res)
	}
	return nil
}

func (z *zenohHandleImpl) ImwQuery(keyExpr string, query *queryHandle) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	if res := C.ZenohHandleImwQuery(z.ptr, keyExprString, C.zenoh_handle_imw_query_callback_fn(C.intrinsic_ImwQueryStaticCallback), C.zenoh_handle_imw_query_on_done_fn(C.intrinsic_ImwQueryDoneStaticCallback), nil, 0, unsafe.Pointer(&query.handle), C.uint64_t(0), false); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwCreateQueryable(keyExpr string, queryable *queryableHandle, isRosService bool) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	if res := C.ZenohHandleImwCreateQueryable(z.ptr, keyExprString, C.zenoh_handle_imw_queryable_callback_fn(C.intrinsic_ImwQueryableStaticCallback), unsafe.Pointer(&queryable.handle), C.bool(isRosService)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwDestroyQueryable(keyExpr string, queryable *queryableHandle) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	if res := C.ZenohHandleImwDestroyQueryable(z.ptr, keyExprString, C.zenoh_handle_imw_queryable_callback_fn(C.intrinsic_ImwQueryableStaticCallback), unsafe.Pointer(&queryable.handle)); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) ImwQueryableReply(queryContext unsafe.Pointer, keyExpr string, reply []byte) error {
	keyExprString := C.CString(keyExpr)
	defer C.free(unsafe.Pointer(keyExprString))

	replyBytes := C.CBytes(reply)
	defer C.free(replyBytes)

	if res := C.ZenohHandleImwQueryableReply(z.ptr, queryContext, keyExprString, replyBytes, C.size_t(len(reply))); res != 0 {
		return errorFromImwRet(res)
	}

	return nil
}

func (z *zenohHandleImpl) Ptr() unsafe.Pointer {
	return z.ptr
}
