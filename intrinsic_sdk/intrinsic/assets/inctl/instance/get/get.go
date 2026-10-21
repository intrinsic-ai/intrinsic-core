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

// Package get contains the inctl asset instance get command.
package get

import (
	"context"
	"fmt"
	"strings"
	"text/template"

	"intrinsic/assets/clientutils"
	"intrinsic/assets/cmdutils"
	"intrinsic/assets/inctl/instance/common"
	"intrinsic/assets/typeutils"
	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/printer"
	"intrinsic/util/proto/protoio"

	"github.com/spf13/cobra"

	aipb "intrinsic/assets/proto/v1/asset_instances_go_proto"
)

type getAssetInstanceWrapper struct {
	common.TypedResolvingMessage[*aipb.AssetInstance]
}

var assetInstanceTmpl = template.Must(template.New("assetInstance").Funcs(template.FuncMap{
	"indent":          indent,
	"stableTextProto": protoio.StableTextProto,
	"resolver":        protoio.WithWriteResolver,
}).Parse(
	`Name:  {{ .Name }}
Asset: {{.Asset }}
Type:  {{.Type }}
{{- with .Details }}
Details:
{{- with .Service }}
  Service:
{{- with .GrpcConnection }}
    Grpc Address: {{ .Address }}
{{- with .Metadata }}
    Grpc Metadata:
{{- range . }}
      {{ .Key }}: {{ .Value }}
{{- end }}
{{- end }}
{{- end }}
{{- with .ServiceInspectionTopic }}
    Service Inspection Topic: {{ . }}
{{- end }}
    Requires Scheduling Config: {{ .RequiresSchedulingConfig }}
{{- end }}
{{- end }}
{{- with .Config }}
Config:
{{ stableTextProto . (resolver .) | indent 2 }}
{{- end }}
`))

func indent(spaces int, s string) string {
	prefix := strings.Repeat(" ", spaces)
	var res []string
	for _, line := range strings.Split(s, "\n") {
		if line != "" {
			res = append(res, prefix+line)
		}
	}
	return strings.Join(res, "\n")
}

func (w *getAssetInstanceWrapper) String() string {
	var sb strings.Builder
	var config common.ResolvingMessage
	if w.Typed().GetConfig() != nil {
		config = common.NewResolvingMessage(w, w.Typed().GetConfig())
	}
	if err := assetInstanceTmpl.Execute(&sb, struct {
		Name    string
		Asset   string
		Type    string
		Details *aipb.AssetInstance_Details
		Config  common.ResolvingMessage
	}{
		Name:    w.Typed().GetName(),
		Asset:   w.Typed().GetAsset(),
		Type:    typeutils.AssetTypeDisplayName(w.Typed().GetMetadata().GetAssetType()),
		Details: w.Typed().GetDetails(),
		Config:  config,
	}); err != nil {
		return fmt.Sprintf("Error executing template: %v", err)
	}

	return strings.TrimSuffix(sb.String(), "\n")
}

func getAndPrintAssetInstance(ctx context.Context, client aipb.AssetInstancesClient, name string, view aipb.AssetInstanceView, prtr printer.Printer) error {
	resp, err := client.GetAssetInstance(ctx, &aipb.GetAssetInstanceRequest{
		Name: name,
		View: view,
	})
	if err != nil {
		return err
	}
	prtr.Print(&getAssetInstanceWrapper{
		TypedResolvingMessage: common.DisplayableInstance(resp),
	})
	return nil
}

// Command returns the get command.
func Command() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	var flagView string
	cmd := &cobra.Command{
		Use:   "get <name>",
		Short: "Get asset instance details",
		Example: `
  Get details of an asset instance in a solution:
  $ inctl asset instance get my_instance --org my_organization --solution my_solution_id

  Get details of an asset instance specifying the cluster:
  $ inctl asset instance get my_instance --project my_project --cluster my_cluster

  Get details of an asset instance specifying the address:
  $ inctl asset instance get my_instance --project my_project --address my_address

  Get basic details of an asset instance:
  $ inctl asset instance get my_instance --view basic --org my_organization --solution my_solution_id

  Get full details of an asset instance including configuration:
  $ inctl asset instance get my_instance --view full --org my_organization --solution my_solution_id
`,
		Args: cobra.ExactArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			name := args[0]
			ctx := cmd.Context()

			view, err := common.ParseView(flagView)
			if err != nil {
				return err
			}

			ctx, conn, _, err := clientutils.DialClusterFromInctl(ctx, flags)
			if err != nil {
				return err
			}
			defer conn.Close()

			client := aipb.NewAssetInstancesClient(conn)
			prtr, err := printer.NewPrinter(root.FlagOutput)
			if err != nil {
				return err
			}

			err = getAndPrintAssetInstance(ctx, client, name, view, prtr)
			if err != nil {
				return fmt.Errorf("could not get asset instance %q: %w", name, err)
			}

			return nil
		},
	}

	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagsProjectOrg()
	cmd.Flags().StringVar(&flagView, "view", "", "Specify the information returned in the request. One of: basic, detail, full.")
	return cmd
}
