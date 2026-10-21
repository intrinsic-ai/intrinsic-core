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

// Package flag enhances core flag package with additional functionality.
package flag

import (
	"flag"
	"fmt"
	"strings"
)

type stringList []string

func (sl *stringList) String() string {
	return strings.Join(*sl, ",")
}

func (sl *stringList) Get() any {
	return []string(*sl)
}

func (sl *stringList) Set(s string) error {
	*sl = stringList(strings.Split(s, ","))
	return nil
}

// StringList registers a flag of type []string which splits values on commas.
func StringList(name string, value []string, usage string) *[]string {
	var v []string = value
	flag.Var((*stringList)(&v), name, usage)
	return &v
}

type multiString []string

func (ms *multiString) String() string {
	if len(*ms) == 0 {
		return ""
	}
	return fmt.Sprint(*ms)
}

func (ms *multiString) Get() any {
	return []string(*ms)
}

func (ms *multiString) Set(value string) error {
	*ms = append(*ms, value)
	return nil
}

// MultiString registers a flag of type []string with appending semantics.
func MultiString(name string, value []string, usage string) *[]string {
	var v []string = value
	flag.Var((*multiString)(&v), name, usage)
	return &v
}
