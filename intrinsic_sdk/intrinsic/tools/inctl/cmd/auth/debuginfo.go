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
	"os"
	"strings"

	"intrinsic/config/environments"
	"intrinsic/tools/inctl/auth/auth"
	"intrinsic/tools/inctl/util/accounts/accounts"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/spf13/cobra"
	"github.com/spf13/viper"

	emptypb "google.golang.org/protobuf/types/known/emptypb"
)

var printDebugInfoParams = viper.New()

func init() {
	AuthCmd.AddCommand(printDebugInfoCmd)
}

// TODO(b/410760339): Once the environments package is removed, resolve the portal
// and accounts domains for --env via inctl/auth (defaulting to prod).
func domainsForEnv(e string) []string {
	return []string{
		environments.PortalDomain(e),
		environments.AccountsDomain(e),
		"edge.intrinsic.ai",
		"ghcr.io",
	}
}

// TODO(b/410760339, b/530456416): Once inctl auth login persists Environment in
// OrgInfo (and the local project->env index), read env from OrgInfo/index instead of
// deriving it from project via environments.FromAnyProject.
func resolveDebugEnv(vipr *viper.Viper, fallbackProject string) string {
	if e := vipr.GetString(orgutil.KeyEnvironment); e != "" {
		return e
	}
	if p := vipr.GetString(orgutil.KeyProject); p != "" {
		return environments.FromAnyProject(p)
	}
	if fallbackProject != "" {
		return environments.FromAnyProject(fallbackProject)
	}
	return environments.Prod
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
		flagEnv := printDebugInfoParams.GetString(orgutil.KeyEnvironment)
		var fallbackProject string
		if printDebugInfoParams.GetString(orgutil.KeyProject) == "" && flagOrg != "" {
			if info, err := authStore.ReadOrgInfo(flagOrg); err == nil {
				fallbackProject = info.Project
			}
		}
		e := resolveDebugEnv(printDebugInfoParams, fallbackProject)
		ctx := cmd.Context()
		for _, domain := range domainsForEnv(e) {
			debugDomain(ctx, domain)
		}
		debugAuthStore(ctx, flagOrg, flagEnv)
		return nil
	},
}, printDebugInfoParams, orgutil.WithOrgExistsCheck(func() bool {
	return checkOrgExists && printDebugInfoParams.GetString(orgutil.KeyEnvironment) == ""
}))

func debugAuthStore(ctx context.Context, org, flagEnv string) {
	fmt.Printf("Configuration for org %s: ", org)
	orgInfo, err := authStore.ReadOrgInfo(org)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	fmt.Println("OK")
	fmt.Printf(" Organization: %s\n", orgInfo.Organization)
	fmt.Printf(" Project: %s\n", orgInfo.Project)

	// TODO(b/410760339, b/530456416): Read orgInfo.Environment directly once persisted by
	// inctl auth login instead of deriving it via environments.FromAnyProject.
	resolvedEnv := flagEnv
	if resolvedEnv == "" {
		resolvedEnv = environments.FromAnyProject(orgInfo.Project)
	}

	fmt.Printf("Project configuration %s (env %s): ", orgInfo.Project, resolvedEnv)
	store, err := authStore.GetEnvConfiguration(resolvedEnv)
	// TODO(b/530456416): Remove fallback to per-project configuration.
	if errors.Is(err, os.ErrNotExist) && orgInfo.Project != "" {
		store, err = authStore.GetProjectConfiguration(orgInfo.Project)
	}
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
	debugAccountsDiscovery(ctx, cred.APIKey, resolvedEnv)
	// Token exchange uses the HTTP/JSON gRPC-gateway (/api/v1/accountstokens:idtoken)
	// hosted on the portal domain (flowstate*.intrinsic.ai), which proxies requests
	// to AccountsTokensService in the accounts service.
	// TODO(b/410760339): Resolve the token exchange gateway domain via inctl/auth once
	// the environments package is removed.
	debugUserRecord(ctx, environments.PortalDomain(resolvedEnv), cred.APIKey)
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

func debugAccountsDiscovery(ctx context.Context, apiKey, env string) {
	// TODO(b/410760339): Resolve the accounts domain via inctl/auth once the
	// environments package is removed.
	fmt.Printf("Organizations Discovery (%q):\n", environments.AccountsDomain(env))
	fmt.Printf(" Connection: ")
	client, closer, err := accounts.DialDiscoveryV1Client(ctx, env, apiKey)
	if err != nil {
		fmt.Printf("ERROR (%v)\n", err)
		return
	}
	defer closer.Close()
	fmt.Println("OK")
	fmt.Printf(" ListOrganizations: ")
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
