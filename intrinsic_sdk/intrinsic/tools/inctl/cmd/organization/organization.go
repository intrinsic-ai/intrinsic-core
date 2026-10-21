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

// Package organization provides commands for viewing and managing your organizations.
package organization

import (
	"bytes"
	"fmt"
	"slices"
	"strings"

	"intrinsic/tools/inctl/cmd/root"
	"intrinsic/tools/inctl/util/cobrautil"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/viper"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

var vipr = viper.New()

// organizationCmd is the `inctl organization` command.
var organizationCmd = cobrautil.ParentOfNestedSubcommands("organization", "Manage your Flowstate organizations.")

var (
	flagDebugRequests   bool
	flagName            string
	flagEmail           string
	flagInvitationToken string
	flagRoleCSV         string
	flagRole            string
	flagParent          string
	flagShowDeleted     bool
	flagOrgDisplayName  string
	flagYes             bool
)

func init() {
	organizationCmd.Aliases = []string{"organizations"}
	connectInit()
	root.RootCmd.AddCommand(organizationCmd)
}

const (
	orgPrefix = "organizations/"
)

func addPrefix(s string, prefix string) string {
	if strings.HasPrefix(s, prefix) {
		return s
	}
	return prefix + s
}

func addPrefixes(s []string, prefix string) []string {
	ps := slices.Clone(s)
	for i := range ps {
		ps[i] = addPrefix(ps[i], prefix)
	}
	return ps
}

// checkOrgNotIntrinsic makes sure the organization is not a reserved name.
func checkOrgNotIntrinsic() error {
	if vipr.GetString(orgutil.KeyOrganization) == "intrinsic" {
		return fmt.Errorf("the current organization cannot be 'intrinsic' for this command")
	}
	return nil
}

// processOrgFlag parses the org flag. Errors if the organization is not given.
func processOrgFlag() (string, error) {
	org := vipr.GetString(orgutil.KeyOrganization)
	if org == "" {
		return "", fmt.Errorf("the --org flag is required for this command")
	}
	if err := checkOrgNotIntrinsic(); err != nil {
		return "", err
	}
	// The `inctl organization` command does not support @project.
	// This is to avoid confusion with the `--env` flag and enforce
	// the notion that `inctl organization` is a global command.
	if strings.Contains(org, "@") {
		return "", fmt.Errorf("`--org=<org>@<project>` syntax is not supported and not required by `inctl organization`")
	}
	return org, nil
}

// resolveOrgArgOrFlag resolves the organization from an optional positional argument or falls back to --org / Viper config.
func resolveOrgArgOrFlag(args []string) (string, error) {
	if len(args) > 0 && args[0] != "" {
		org := args[0]
		if strings.Contains(org, "@") {
			return "", fmt.Errorf("`<org>@<project>` syntax is not supported and not required by `inctl organization`")
		}
		return org, nil
	}
	return processOrgFlag()
}

func protoPrint(p proto.Message) {
	ms, err := protojson.MarshalOptions{
		Multiline:         true,
		UseProtoNames:     true,
		EmitUnpopulated:   true,
		EmitDefaultValues: true,
	}.Marshal(p)
	if err != nil {
		fmt.Println(err)
	}
	fmt.Println(string(ms))
}

// marshalProtoSlice marshals a slice of protobuf messages directly to JSON array bytes
// using protojson so all proto fields are serialized with their official proto names.
func marshalProtoSlice[T proto.Message](items []T) ([]byte, error) {
	mo := protojson.MarshalOptions{UseProtoNames: true}
	var buf bytes.Buffer
	buf.WriteByte('[')
	for i, item := range items {
		if i > 0 {
			buf.WriteByte(',')
		}
		b, err := mo.Marshal(item)
		if err != nil {
			return nil, err
		}
		buf.Write(b)
	}
	buf.WriteByte(']')
	return buf.Bytes(), nil
}
