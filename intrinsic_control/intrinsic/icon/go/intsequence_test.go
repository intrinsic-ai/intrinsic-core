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

package intsequence

import (
	"sync"
	"testing"
)

func TestIntSequence_Generators(t *testing.T) {
	want := []int64{1, 2, 3, 4, 5, 6, 7, 8}
	var g, g2 Generator
	for _, w := range want {
		if got := g.Next(); got != w {
			t.Errorf("g.Next() = %d, want %d", got, w)
		}
		if got := g2.Next(); got != w {
			t.Errorf("g2.Next() = %d, want %d", got, w)
		}
	}
}

// fillSlice populates the first n elements of slice with calls to g.Next()
func fillSlice(g *Generator, n int, slice []int64) {
	for i := 0; i < n; i++ {
		slice[i] = g.Next()
	}
}

func TestIntSequence_ConcurrentNext(t *testing.T) {
	workerCount := 10000
	callsPerWorker := 20000
	workerSlices := make([][]int64, workerCount)
	for i := 0; i < workerCount; i++ {
		workerSlices[i] = make([]int64, callsPerWorker)
	}
	var wg sync.WaitGroup
	g := &Generator{}
	for i := 0; i < workerCount; i++ {
		wg.Add(1)
		slice := workerSlices[i]
		go func() {
			fillSlice(g, callsPerWorker, slice)
			wg.Done()
		}()
	}
	wg.Wait()

	// Use pigeon-hole principle to verify that each integer was generated
	// exactly once.
	max := int64(workerCount * callsPerWorker)
	seen := make([]bool, max)
	for i := 0; i < workerCount; i++ {
		for j := 0; j < callsPerWorker; j++ {
			v := workerSlices[i][j]
			if v < 1 || v > max {
				t.Errorf("got v = %d, want (v >= 1 && v <= %d)", v, max)
			}
			if seen[v-1] {
				t.Errorf("Next() generated duplicate value %d", v)
			}
			seen[v-1] = true
		}
	}
}
