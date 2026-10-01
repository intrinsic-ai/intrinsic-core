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

// Package throttle provides constants and primitives to limit concurrency and rates.
package throttle

import (
	"context"

	"golang.org/x/sync/errgroup"
	"golang.org/x/time/rate"
	"google.golang.org/grpc"
)

// The following constants provide a single source of truth for safe limits when communicating with
// backend services during Asset operations (such as installation, release, and deployment). They
// are used as defaults across CLI commands and client wrappers to prevent overwhelming cloud
// services or on-premises workcells.
const (
	// CloudRateLimit is a safe rate limit for communicating with cloud services.
	CloudRateLimit = 20.0

	// CloudBurst is a safe burst size for communicating with cloud services.
	CloudBurst = 2

	// OnPremRateLimit is a safe rate limit for communicating with on-premises cluster services.
	OnPremRateLimit = 20.0

	// OnPremBurst is a safe burst size for communicating with on-premises cluster services.
	OnPremBurst = 2

	// LocalProcessingConcurrency is a safe maximum concurrency for local processing operations.
	LocalProcessingConcurrency = 16

	// ReleaseProcessingConcurrency is a safe maximum concurrency for processing operations during
	// Asset release, which is currently invoked in parallel across multiple processes.
	ReleaseProcessingConcurrency = 1
)

// ConcurrencyLimiter limits concurrent function calls across potentially nested call graphs.
//
// In a nested call graph where functions executed by Do make further calls to Do on the same
// limiter, a naive semaphore would deadlock whenever all slots are occupied by parent functions
// waiting for their child Do calls to finish. To prevent this while still bounding active work to
// the limiter's capacity, Do attaches a concurrency token to the context passed to each function.
// When a parent function blocks on a nested Do call, up to one child function at a time can
// "borrow" the waiting parent's concurrency slot (while other child functions can run in parallel
// if free slots are available in the limiter).
//
// Example usage:
//
//	cl := throttle.NewConcurrencyLimiter(4)
//	err := cl.Do(ctx,
//		func(ctx context.Context) error {
//			// Shares the limiter with the outer Do call without risking deadlock.
//			return cl.Do(ctx, childFn1, childFn2)
//		},
//		...,
//	)
//
// A nil or zero-value ConcurrencyLimiter is valid and acts as an unconstrained limiter.
type ConcurrencyLimiter struct {
	sem chan struct{}
}

// NewConcurrencyLimiter creates a ConcurrencyLimiter that allows up to limit concurrent operations.
//
// If limit <= 0, concurrency is unconstrained.
func NewConcurrencyLimiter(limit int) *ConcurrencyLimiter {
	if limit <= 0 {
		return nil
	}

	return &ConcurrencyLimiter{
		sem: make(chan struct{}, limit),
	}
}

// Do calls functions within the limiter's concurrency limit and waits for all of them to complete.
//
// Each function is launched in an errgroup goroutine and blocks until it acquires a concurrency
// slot before executing. If ctx carries a token from an enclosing Do call on cl, a child function
// can either acquire a free slot from cl or borrow the waiting caller's slot (at most one child at
// a time), guaranteeing forward progress without exceeding the concurrency limit.
//
// If ctx is canceled or any function returns an error, the context passed to the functions is
// canceled, any queued functions that have not yet acquired a concurrency slot are skipped, and Do
// returns the first error. Calling code should not rely on functions passed to Do being called to
// release resources acquired outside of Do.
func (cl *ConcurrencyLimiter) Do(ctx context.Context, fns ...func(ctx context.Context) error) error {
	g, gCtx := errgroup.WithContext(ctx)
	if cl != nil && cl.sem != nil {
		g.SetLimit(cap(cl.sem))
	}
	for _, fn := range fns {
		g.Go(func() error {
			tok, fnCtx, err := cl.acquire(gCtx)
			if err != nil {
				return err
			}
			defer tok.release()
			return fn(fnCtx)
		})
	}
	return g.Wait()
}

// acquire obtains a concurrency slot for a function in Do and returns the acquired token along
// with a child context carrying that token.
//
// For a top-level Do call (no parent token in ctx), parentSem is nil (disabling that select case)
// and acquire blocks until a slot is available in cl.sem or ctx is canceled.
//
// For a nested Do call (where ctx carries the parent's token), acquire waits on both cl.sem and the
// parent token's 1-slot sem, returning a token backed by whichever slot becomes available first.
func (cl *ConcurrencyLimiter) acquire(ctx context.Context) (*token, context.Context, error) {
	// Check ctx.Err() first because Go's select chooses pseudo-randomly if both a semaphore channel
	// and ctx.Done() are ready at the same time.
	if err := ctx.Err(); err != nil {
		return nil, nil, err
	}
	if cl == nil || cl.sem == nil {
		return nil, ctx, nil
	}

	// Look for a parent token in the context. If found, we can also acquire ("borrow") the slot from
	// that token.
	var parentSem chan struct{}
	if tok, ok := ctx.Value(cl).(*token); ok {
		parentSem = tok.sem
	}

	// Wait until we can either borrow the parent token's slot or acquire a new slot from the limiter.
	var tok *token
	select {
	case cl.sem <- struct{}{}:
		tok = newToken(func() { <-cl.sem })
	case parentSem <- struct{}{}:
		tok = newToken(func() { <-parentSem })
	case <-ctx.Done():
		return nil, nil, ctx.Err()
	}
	return tok, context.WithValue(ctx, cl, tok), nil
}

// token represents a single acquired concurrency slot.
//
// A nil token is valid and represents an unconstrained operation whose release is a no-op.
type token struct {
	releaseFn func()
	// sem is a 1-element buffered channel that is only used if the holder makes a nested call to Do,
	// allowing at most one child function at a time to borrow this token's concurrency slot while the
	// holder waits for the nested Do call to complete.
	sem chan struct{}
}

func newToken(releaseFn func()) *token {
	return &token{
		releaseFn: releaseFn,
		sem:       make(chan struct{}, 1),
	}
}

func (t *token) release() {
	if t != nil && t.releaseFn != nil {
		t.releaseFn()
	}
}

// rateLimitedConn wraps a grpc.ClientConnInterface to rate-limit outbound RPC calls.
type rateLimitedConn struct {
	grpc.ClientConnInterface
	limiter *rate.Limiter
}

// ConnectionWithRateLimit wraps conn to enforce rate limits on all Invoke and NewStream calls.
//
// limit specifies the maximum request rate per second. If limit <= 0, conn is returned directly.
// burst specifies the maximum burst size permitted by the rate limiter.
//
// If conn is nil, ConnectionWithRateLimit returns nil.
func ConnectionWithRateLimit(conn grpc.ClientConnInterface, limit float64, burst int) grpc.ClientConnInterface {
	if conn == nil || limit <= 0 {
		return conn
	}

	return ConnectionWithRateLimiter(conn, rate.NewLimiter(rate.Limit(limit), burst))
}

// ConnectionWithRateLimiter wraps conn with limiter to enforce rate limits on all Invoke and
// NewStream calls.
//
// If conn or limiter is nil, conn is returned directly.
func ConnectionWithRateLimiter(conn grpc.ClientConnInterface, limiter *rate.Limiter) grpc.ClientConnInterface {
	if conn == nil || limiter == nil {
		return conn
	}

	return &rateLimitedConn{
		ClientConnInterface: conn,
		limiter:             limiter,
	}
}

func (c *rateLimitedConn) Invoke(ctx context.Context, method string, args any, reply any, opts ...grpc.CallOption) error {
	if err := c.limiter.Wait(ctx); err != nil {
		return err
	}
	return c.ClientConnInterface.Invoke(ctx, method, args, reply, opts...)
}

func (c *rateLimitedConn) NewStream(ctx context.Context, desc *grpc.StreamDesc, method string, opts ...grpc.CallOption) (grpc.ClientStream, error) {
	if err := c.limiter.Wait(ctx); err != nil {
		return nil, err
	}
	return c.ClientConnInterface.NewStream(ctx, desc, method, opts...)
}
