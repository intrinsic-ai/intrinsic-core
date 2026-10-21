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

// Package list contains the inctl asset instance list command.
package list

import (
	"context"
	"fmt"
	"strings"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/assets/inctl/instance/common"
	"intrinsic/assets/tagutils"
	"intrinsic/assets/typeutils"
	"intrinsic/tools/inctl/util/printer"

	"github.com/spf13/cobra"

	atagpb "intrinsic/assets/proto/asset_tag_go_proto"
	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	aipb "intrinsic/assets/proto/v1/asset_instances_go_proto"
	depb "intrinsic/assets/proto/v1/dependency_go_proto"
)

type instanceRow struct {
	Name      string `json:"name"`
	Asset     string `json:"asset"`
	AssetType string `json:"assetType"`
}

func asInstanceRow(instance *aipb.AssetInstance) *instanceRow {
	return &instanceRow{
		Name:      instance.GetName(),
		Asset:     instance.GetAsset(),
		AssetType: typeutils.AssetTypeDisplayName(instance.GetMetadata().GetAssetType()),
	}
}

func (r *instanceRow) Tabulated(columns []string) []string {
	res := make([]string, len(columns))
	for i, col := range columns {
		switch col {
		case "name":
			res[i] = r.Name
		case "asset":
			res[i] = r.Asset
		case "assetType":
			res[i] = r.AssetType
		default:
			res[i] = ""
		}
	}
	return res
}

func columnsForView(view aipb.AssetInstanceView) []string {
	switch view {
	case aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_DETAIL, aipb.AssetInstanceView_ASSET_INSTANCE_VIEW_FULL:
		return []string{"name", "asset", "assetType"}
	default:
		return []string{"name", "asset"}
	}
}

func getInstanceRowCommandPrinter(cmd *cobra.Command, view aipb.AssetInstanceView) printer.CommandPrinter {
	ot := printer.GetFlagOutputType(cmd)
	if ot == printer.OutputTypeText {
		ot = printer.OutputTypeTAB
	}
	columns := columnsForView(view)
	cp, err := printer.NewPrinterOfType(ot, cmd, printer.WithDefaultsFromValue(&instanceRow{}, func([]string) []string {
		return columns
	}))
	if err != nil {
		cmd.PrintErrf("Error setting up output: %v\n", err)
		cp = printer.GetDefaultPrinter(cmd)
	}
	return cp
}

func listAssetInstances(ctx context.Context, client aipb.AssetInstancesClient, prtr printer.CommandPrinter, outputType printer.OutputType, view aipb.AssetInstanceView, filters []*aipb.ListAssetInstancesRequest_Filter) error {
	var pageToken string
	for {
		resp, err := client.ListAssetInstances(ctx, &aipb.ListAssetInstancesRequest{
			PageSize:      50,
			PageToken:     pageToken,
			View:          view,
			StrictFilters: filters,
		})
		if err != nil {
			return err
		}

		for _, instance := range resp.GetAssetInstances() {
			if outputType == printer.OutputTypeJSON || outputType == printer.OutputTypeNDJSON {
				prtr.Println(common.DisplayableInstance(instance))
			} else {
				prtr.Println(printer.SimpleView(asInstanceRow(instance)))
			}
		}

		pageToken = resp.GetNextPageToken()
		if pageToken == "" {
			break
		}
	}

	return printer.Flush(prtr)
}

// parseFilter parses a single filter string (e.g., "asset_type=skill,id=ai.intrinsic.move_robot")
// into a ListAssetInstancesRequest_Filter proto.
// Supports subindexing for fulfills:
// - fulfills.requires=some.service (accumulates)
// - fulfills.requires_object (sets to empty message)
func parseFilter(fs string) (*aipb.ListAssetInstancesRequest_Filter, error) {
	filter := &aipb.ListAssetInstancesRequest_Filter{}
	pairs := strings.Split(fs, ",")
	for _, pair := range pairs {
		kv := strings.SplitN(pair, "=", 2)
		key := kv[0]
		var val string
		if len(kv) == 2 {
			val = kv[1]
		}

		keyParts := strings.Split(key, ".")
		switch keyParts[0] {
		case "asset_type":
			if len(kv) != 2 {
				return nil, fmt.Errorf("asset_type requires a value")
			}
			t := typeutils.AssetTypeFromCodeName(val)
			if t == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
				t = typeutils.AssetTypeFromDisplayName(val)
			}
			if t == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
				return nil, fmt.Errorf("unknown asset_type: %q", val)
			}
			filter.AssetType = t
		case "id":
			if len(kv) != 2 {
				return nil, fmt.Errorf("id requires a value")
			}
			id, err := idutils.IDProtoFromString(val)
			if err != nil {
				return nil, fmt.Errorf("invalid id %q: %w", val, err)
			}
			filter.Id = id
		case "asset":
			if len(kv) != 2 {
				return nil, fmt.Errorf("asset requires a value")
			}
			v := val
			filter.Asset = &v
		case "asset_tag":
			if len(kv) != 2 {
				return nil, fmt.Errorf("asset_tag requires a value")
			}
			tag := tagutils.AssetTagFromName(val)
			if tag == atagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
				tag = tagutils.AssetTagFromDisplayName(val)
			}
			if tag == atagpb.AssetTag_ASSET_TAG_UNSPECIFIED {
				return nil, fmt.Errorf("unknown asset_tag: %q", val)
			}
			filter.AssetTag = tag
		case "fulfills":
			if filter.Fulfills == nil {
				filter.Fulfills = &depb.Dependency{}
			}
			if len(keyParts) < 2 {
				return nil, fmt.Errorf("invalid fulfills filter: %q", key)
			}
			switch keyParts[1] {
			case "requires":
				if len(kv) != 2 {
					return nil, fmt.Errorf("fulfills.requires requires a value")
				}
				filter.Fulfills.Requires = append(filter.Fulfills.Requires, val)
			case "requires_object":
				filter.Fulfills.RequiresObject = &depb.Dependency_ObjectConstraint{}
			default:
				return nil, fmt.Errorf("unknown fulfills field: %q", keyParts[1])
			}
		default:
			return nil, fmt.Errorf("unknown filter key: %q", key)
		}
	}
	return filter, nil
}

func parseFilters(filterStrings []string) ([]*aipb.ListAssetInstancesRequest_Filter, error) {
	var filters []*aipb.ListAssetInstancesRequest_Filter
	for _, fs := range filterStrings {
		filter, err := parseFilter(fs)
		if err != nil {
			return nil, err
		}
		filters = append(filters, filter)
	}
	return filters, nil
}

// Command returns the list command.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	var flagView string
	var flagFilters []string
	cmd := &cobra.Command{
		Use:   "list",
		Short: "List asset instances",
		Example: `
  List all asset instances in a solution:
  $ inctl asset instance list --org my_organization --solution my_solution_id

  List asset instances specifying the cluster:
  $ inctl asset instance list --project my_project --cluster my_cluster

  List asset instances specifying the address:
  $ inctl asset instance list --project my_project --address my_address

  List asset instances with json output:
  $ inctl asset instance list --output json --org my_organization --solution my_solution_id

  Filter asset instances by type:
  $ inctl asset instance list --filter asset_type=skill --org my_organization --solution my_solution_id

  Filter asset instances by type and ID (AND):
  $ inctl asset instance list --filter asset_type=skill,id=ai.intrinsic.move_robot --org my_organization --solution my_solution_id

  Multiple filters are ORed:
  $ inctl asset instance list --filter asset_type=skill --filter asset_type=service --org my_organization --solution my_solution_id

  Filter by dependency requirement (fulfills.requires):
  $ inctl asset instance list --filter fulfills.requires=grpc://some.Service --org my_organization --solution my_solution_id
`,
		Args: cobra.NoArgs,
		RunE: func(cmd *cobra.Command, args []string) error {
			ctx := cmd.Context()

			view, err := common.ParseView(flagView)
			if err != nil {
				return err
			}

			filters, err := parseFilters(flagFilters)
			if err != nil {
				return err
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			client := aipb.NewAssetInstancesClient(conn)
			prtr := getInstanceRowCommandPrinter(cmd, view)

			err = listAssetInstances(ctx, client, prtr, printer.GetFlagOutputType(cmd), view, filters)
			if err != nil {
				return fmt.Errorf("could not list asset instances: %w", err)
			}

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	cmd.Flags().StringVar(&flagView, "view", "", "Specify the information returned in the request. One of: basic, detail, full.")
	cmd.Flags().StringArrayVar(&flagFilters, "filter", nil, "Filter results. Can be specified multiple times. Format: key=value,key=value. Supported keys: asset_type, id, asset, asset_tag, fulfills.requires, fulfills.requires_object")
	return cmd
}
