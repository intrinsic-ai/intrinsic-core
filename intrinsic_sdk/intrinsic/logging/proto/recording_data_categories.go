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

// Package recordingdatacategories provides embedded text protobuf data
// for the recording data categories single source of truth.
package recordingdatacategories

import (
	_ "embed"
	"sync"

	recordingcategoriespb "intrinsic/logging/proto/recording_data_categories_go_proto"

	"google.golang.org/protobuf/encoding/prototext"
)

//go:embed recording_data_categories.pbtxt
var recordingDataCategoriesBytes []byte

var (
	recordingDataCategories *recordingcategoriespb.RecordingDataCategories
	once                    sync.Once
)

// GetRecordingDataCategories returns the unmarshaled RecordingDataCategories.
// The result is cached after the first call.
func GetRecordingDataCategories() *recordingcategoriespb.RecordingDataCategories {
	once.Do(func() {
		recordingDataCategories = &recordingcategoriespb.RecordingDataCategories{}
		if err := prototext.Unmarshal(recordingDataCategoriesBytes, recordingDataCategories); err != nil {
			panic("failed to unmarshal recording data categories: " + err.Error())
		}
	})
	return recordingDataCategories
}
