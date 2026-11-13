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

package operations

import (
	"sort"
	"sync"
)

// Map safely manages long-running operations with concurrency.
type Map struct {
	mu  sync.Mutex
	ops map[string]*Operation
}

// NewMap creates a map for long-running operations that is safe for concurrent
// use. The (optional) provided operations are added to the map.
func NewMap(ops ...*Operation) *Map {
	m := make(map[string]*Operation)
	for _, op := range ops {
		m[op.Name()] = op
	}
	return &Map{
		ops: m,
	}
}

// Add adds the operation to the map.
func (m *Map) Add(op *Operation) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.ops[op.Name()] = op
}

// Get retrieves an entry from the map. Returns `nil` if the operation does not
// exist in the map.
func (m *Map) Get(name string) *Operation {
	m.mu.Lock()
	defer m.mu.Unlock()
	op, ok := m.ops[name]
	if !ok {
		return nil
	}
	return op
}

// GetAll returns all operations stored on the map as a slice with the same
// length as the map. If the map is empty the returned slice will also be empty.
// The returned slice is guaranteed to be ordered by operation creation date in
// descending order (newest first).
func (m *Map) GetAll() []*Operation {
	m.mu.Lock()
	defer m.mu.Unlock()

	sortedOps := make([]*Operation, 0, len(m.ops))
	for k := range m.ops {
		sortedOps = append(sortedOps, m.ops[k])
	}
	sort.Slice(sortedOps, func(i, j int) bool {
		return sortedOps[i].createTime.After(sortedOps[j].createTime)
	})
	return sortedOps
}

// Delete deletes an entry from the map. Returns false if the operation does not
// exist in the map or true if it does exist and is deleted.
func (m *Map) Delete(name string) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if _, ok := m.ops[name]; !ok {
		return false
	}
	delete(m.ops, name)
	return true
}
