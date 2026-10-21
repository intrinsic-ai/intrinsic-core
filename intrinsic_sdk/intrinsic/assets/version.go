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

// Package version provides utilities for working with and looking up versions of Assets.
package version

import (
	"context"
	"errors"
	"fmt"
	"strings"

	"intrinsic/assets/idutils"

	"google.golang.org/protobuf/proto"

	idpb "intrinsic/assets/proto/id_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
)

var (
	errIDNotFound = errors.New("there is no currently installed Asset with ID")
	errAmbiguous  = errors.New("could not disambiguate ID")
)

// Autofill updates an unspecified version in an IdVersion proto to be the only
// available version of the specified Id proto.  An error is returned if there
// is not exactly one version installed.
func Autofill(ctx context.Context, client iagrpcpb.InstalledAssetsClient, idOrIDVersion *idpb.IdVersion) error {
	if idOrIDVersion.GetVersion() != "" {
		return nil
	}
	versions, err := List(ctx, client, idOrIDVersion.GetId())
	if err != nil {
		return err
	}
	id, err := idutils.IDFromProto(idOrIDVersion.GetId())
	if err != nil {
		return err
	}
	if len(versions) == 0 {
		return fmt.Errorf("%w %q", errIDNotFound, id)
	} else if len(versions) > 1 {
		return fmt.Errorf("%w %q as there are multiple installed versions that match: %v", errAmbiguous, id, strings.Join(versions, ","))
	}
	idOrIDVersion.Version = versions[0]
	return nil
}

// List returns all installed versions of a particular Asset ID.
func List(ctx context.Context, client iagrpcpb.InstalledAssetsClient, id *idpb.Id) ([]string, error) {
	var versions []string
	nextPageToken := ""
	for {
		resp, err := client.ListInstalledAssets(ctx, &iapb.ListInstalledAssetsRequest{
			PageToken: nextPageToken,
		})
		if err != nil {
			return nil, fmt.Errorf("could not retrieve currently installed Assets: %w", err)
		}
		for _, r := range resp.GetInstalledAssets() {
			if proto.Equal(id, r.GetMetadata().GetIdVersion().GetId()) {
				versions = append(versions, r.GetMetadata().GetIdVersion().GetVersion())
			}
		}
		nextPageToken = resp.GetNextPageToken()
		if nextPageToken == "" {
			break
		}
	}
	return versions, nil
}
