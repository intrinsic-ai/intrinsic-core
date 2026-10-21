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

package auth

import (
	"context"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"strings"

	"intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	accdiscoverv1grpcpb "intrinsic/kubernetes/accounts/service/api/v1/discoveryapi_go_proto"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

var printDebugInfoParams = viper.New()

func init() {
	AuthCmd.AddCommand(printDebugInfoCmd)
}

var domains = []string{
	"flowstate.intrinsic.ai",
	"accounts.intrinsic.ai",
	"edge.intrinsic.ai",
	"ghcr.io",
}

var printDebugInfoCmdHelp = `
Prints debug information to diagnose issues with authentication.
`

var printDebugInfoCmd = orgutil.WrapCmd(&cobra.Command{
	Use:   "debuginfo",
	Short: "Prints debug information to diagnose issues with authentication.",
	Long:  printDebugInfoCmdHelp,
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		flagOrg := printDebugInfoParams.GetString(orgutil.KeyOrganization)
		ctx := cmd.Context()
		for _, domain := range domains {
			debugDomain(ctx, domain)
		}
		debugAuthStore(ctx, flagOrg)
		return nil
	},
}, printDebugInfoParams, orgutil.WithOrgExistsCheck(func() bool { return checkOrgExists }))

func debugAuthStore(ctx context.Context, org string) {
	fmt.Printf("Configuration for org %s: ", org)
	orgInfo, err := authStore.ReadOrgInfo(org)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" Organization: %s\n", orgInfo.Organization)
	fmt.Printf(" Project: %s\n", orgInfo.Project)
	fmt.Printf("Project configuration %s: ", orgInfo.Project)
	store, err := authStore.GetConfiguration(orgInfo.Project)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Print("Default credentials: ")
	cred, err := store.GetDefaultCredentials()
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf("API Key Length: %d\n", len(cred.APIKey))
	debugAccountsDiscovery(ctx, cred.APIKey, "intrinsic-accounts-prod")
	debugUserRecord(ctx, "flowstate.intrinsic.ai", cred.APIKey)
}

func debugDomain(ctx context.Context, domain string) {
	debugDNS(ctx, domain)
	err := debugTLS(ctx, domain, false)
	if err != nil { // try again without verification
		debugTLS(ctx, domain, true)
	}
}

func debugTLS(ctx context.Context, domain string, skipVerify bool) error {
	fmt.Printf("TLS (%q, skipVerify=%t): ", domain, skipVerify)
	conf := &tls.Config{
		InsecureSkipVerify: skipVerify, // NOLINT
	}
	conn, err := tls.Dial("tcp", domain+":443", conf)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return err
	}
	defer conn.Close()
	fmt.Println("OK")
	certs := conn.ConnectionState().PeerCertificates
	for idx, cert := range certs {
		fmt.Printf(" Certificate %d:\n", idx)
		fmt.Printf("  Subject: %v\n", cert.Subject)
		fmt.Printf("  Issuer Name: %v\n", cert.Issuer)
		fmt.Printf("  Expiry: %s \n", cert.NotAfter.Format("2006-January-02"))
		fmt.Printf("  Common Name: %s \n", cert.Issuer.CommonName)
	}
	return nil
}

func debugDNS(ctx context.Context, domain string) {
	// debug DNS
	fmt.Printf("DNS (%q): ", domain)
	r := net.Resolver{}
	addrs, err := r.LookupHost(ctx, domain)
	if err != nil {
		fmt.Printf("ERROR (%v)", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf("DNS (%q): Addresses: %v\n", domain, addrs)
}

func debugUserRecord(ctx context.Context, addr string, apiKey string) {
	fmt.Printf("Token Exchange (%q): ", addr)
	cl, err := auth.NewTokensServiceClient(&http.Client{}, addr)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" Exchanging Token: ")
	resp, err := cl.Token(ctx, apiKey)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" Decoding token: ")
	pl, err := decodePayload(resp)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	dat := map[string]any{}
	err = json.Unmarshal(pl, &dat)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" JWT: \n")
	for k, v := range dat {
		fmt.Printf("  %s: %+v\n", k, v)
	}
}

func decodePayload(jwtk string) ([]byte, error) {
	parts := strings.Split(jwtk, ".")
	if len(parts) != 3 {
		return nil, errors.New("invalid JWT, token must have 3 parts")
	}
	d, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		return nil, fmt.Errorf("failed to decode JWT payload section: %v", err)
	}
	return d, nil
}

func debugAccountsDiscovery(ctx context.Context, apiKey, accProject string) {
	fmt.Printf("Organizations Discovery (%q):\n", environments.Domain(accProject))
	fmt.Printf(" Connection: ")
	conn, err := auth.NewCloudConnection(ctx, auth.WithProject(accProject), auth.WithAPIKey(apiKey))
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	defer conn.Close()
	fmt.Println("OK")
	fmt.Printf(" ListOrganizations: ")
	client := accdiscoverv1grpcpb.NewAccountsDiscoveryServiceClient(conn)
	resp, err := client.ListOrganizations(ctx, &emptypb.Empty{})
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" Organizations (%d):\n", len(resp.GetOrganizations()))
	for _, org := range resp.GetOrganizations() {
		fmt.Printf("  %s on %s\n", org.GetName(), org.GetProject())
	}
}
