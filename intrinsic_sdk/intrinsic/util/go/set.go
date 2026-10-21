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

// Package set provides a generic set implementation.
package set

import (
	"iter"
	"maps"
)

// Set is a generic set implementation.
type Set[T comparable] struct {
	m map[T]struct{}
}

// New creates a new Set from the given values.
func New[T comparable](values ...T) Set[T] {
	s := Set[T]{m: make(map[T]struct{})}
	s.AddAll(values...)
	return s
}

// AddAll inserts all elements into the set.
func (s Set[T]) AddAll(values ...T) {
	for _, v := range values {
		s.Add(v)
	}
}

// Add inserts an element into the set
func (s Set[T]) Add(value T) {
	s.m[value] = struct{}{}
}

// Remove deletes an element from the set
func (s Set[T]) Remove(value T) {
	delete(s.m, value)
}

// Contains returns true if the item is in the set.
func (s Set[T]) Contains(item T) bool {
	_, ok := s.m[item]
	return ok
}

// All returns an iterator over the elements of the set.
func (s Set[T]) All() iter.Seq[T] {
	return maps.Keys(s.m)
}

// Size returns the number of elements in the set
func (s Set[T]) Size() int {
	return len(s.m)
}

// Equals returns true if the sets are equal.
func (s Set[T]) Equals(other Set[T]) bool {
	if s.Size() != other.Size() {
		return false
	}
	for v := range s.All() {
		if !other.Contains(v) {
			return false
		}
	}
	return true
}

// Union returns an iterator over elements from both s and other.
func (s Set[T]) Union(other Set[T]) iter.Seq[T] {
	return func(yield func(T) bool) {
		for k := range s.All() {
			if !yield(k) {
				return
			}
		}
		for k := range other.All() {
			if !s.Contains(k) {
				if !yield(k) {
					return
				}
			}
		}
	}
}

// Intersection returns an iterator over elements that are in both s and other.
func (s Set[T]) Intersection(other Set[T]) iter.Seq[T] {
	return func(yield func(T) bool) {
		for k := range s.All() {
			if other.Contains(k) {
				if !yield(k) {
					return
				}
			}
		}
	}
}

// Difference returns an iterator over elements that are in s but not in other.
func (s Set[T]) Difference(other Set[T]) iter.Seq[T] {
	return func(yield func(T) bool) {
		for k := range s.All() {
			if !other.Contains(k) {
				if !yield(k) {
					return
				}
			}
		}
	}
}
