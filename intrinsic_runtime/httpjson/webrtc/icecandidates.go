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

// Package webrtc implements WebRTC signaling and data channel pub/sub transport for real-time client communication.
package webrtc

import (
	"context"
	"crypto/tls"
	"encoding/json"
	"fmt"
	"log/slog"
	"net"
	"net/http"
	"os"
	"strings"
	"time"

	"intrinsic/cloud/devicemanager/shared"

	pion "github.com/pion/webrtc/v4"
)

// The internal DNS name for the xfa-config control plane service
const xfaConfigBaseURL = "https://xfa-config-control-plane.default.svc.cluster.local:443"

// xfaConfigClient is a shared package-level HTTP client configured to bypass
// TLS verification for the local control plane service. It is defined at
// the package level to facilitate TCP connection reuse and connection pooling.
var xfaConfigClient = &http.Client{
	Transport: &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true}, // NOLINT
	},
	Timeout: 5 * time.Second,
}

// GetICEServers returns the STUN and TURN server configurations.
func GetICEServers(portalDomain string, creds *TURNCredentials) []pion.ICEServer {
	var iceServers []pion.ICEServer

	stunServers := getStunIceServers()
	iceServers = append(iceServers, stunServers...)

	turnServers := getTurnIceServers(portalDomain, creds)
	iceServers = append(iceServers, turnServers...)

	return iceServers
}

// DiscoverHostIPs gathers Pod and host interface IPs.
// Failures in host IP discovery are logged as warnings and do not prevent returning other IPs.
func DiscoverHostIPs(ctx context.Context) []string {
	var hostIPs []string

	inClusterIPs := getInClusterIPs()
	hostIPs = append(hostIPs, inClusterIPs...)

	ips, err := fetchHostIPs(ctx)
	if err != nil {
		slog.WarnContext(ctx, "Failed to query host IPs from xfa-config-service; continuing with other candidates", "error", err)
	} else {
		hostIPs = append(hostIPs, ips...)
	}

	// Deduplicate host IPs to avoid advertising the same IP multiple times.
	seen := make(map[string]bool)
	var uniqueIPs []string
	for _, ip := range hostIPs {
		if !seen[ip] {
			seen[ip] = true
			uniqueIPs = append(uniqueIPs, ip)
		}
	}
	return uniqueIPs
}

// getStunIceServers constructs the STUN (Server Reflexive) ICE Servers list.
//
// This is usable when the HMI is:
//   - on the internet
//   - on a 1P developer's machine (using port forwarding)
//
// Returns Google's public STUN server (stun:stun.l.google.com:19302).
func getStunIceServers() []pion.ICEServer {
	return []pion.ICEServer{
		{
			URLs: []string{"stun:stun.l.google.com:19302"},
		},
	}
}

// getTurnIceServers constructs the TURN (Relayed) ICE Servers list.
//
// This is usable no matter where the HMI is located, but it's best used when there is
// a firewall preventing the HMI from making a direct connection to the IPC.
//
// Returns authenticated TURN servers using TCP and UDP using provided credentials.
func getTurnIceServers(portalDomain string, creds *TURNCredentials) []pion.ICEServer {
	if creds == nil || portalDomain == "" {
		return nil
	}
	return []pion.ICEServer{
		{
			URLs:       []string{fmt.Sprintf("turn:turn.global.%s:443?transport=tcp", portalDomain)},
			Username:   creds.Username,
			Credential: creds.Password,
		},
		{
			URLs:       []string{fmt.Sprintf("turn:turn.global.%s:443", portalDomain)},
			Username:   creds.Username,
			Credential: creds.Password,
		},
	}
}

// getInClusterIPs retrieves the local Pod IP addresses for pod-to-pod communication.
//
// This is useful when the HMI is:
//   - a pod on-prem
//
// Retrieves the local Pod IP addresses from the POD_IPS environment variable (injected via Kubernetes Downward API).
func getInClusterIPs() []string {
	var hostIPs []string
	if podIPs := os.Getenv("POD_IPS"); podIPs != "" {
		for _, ip := range strings.Split(podIPs, ",") {
			if trimmed := strings.TrimSpace(ip); trimmed != "" {
				hostIPs = append(hostIPs, trimmed)
			}
		}
	}
	return hostIPs
}

// fetchHostIPs fetches host candidates.
//
// This is useful when the HMI is:
//   - on a downlink network with the IPC
//   - on the uplink network with the IPC
//
// Queries the local xfa-config-service to retrieve host IPs.
func fetchHostIPs(ctx context.Context) ([]string, error) {
	baseURL := xfaConfigBaseURL
	if envURL := os.Getenv("XFA_CONFIG_BASE_URL"); envURL != "" {
		baseURL = envURL
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, fmt.Sprintf("%s/v1alpha1/status", baseURL), nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	resp, err := xfaConfigClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("failed to query xfa-config-service: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("xfa-config-service returned unexpected status: %d", resp.StatusCode)
	}

	var status shared.Status
	if err := json.NewDecoder(resp.Body).Decode(&status); err != nil {
		return nil, fmt.Errorf("failed to decode status response: %w", err)
	}

	var hostIPs []string
	for _, iface := range status.Network {
		for _, ipWithSubnet := range iface.IPAddress {
			// Strip the subnet mask if present (e.g. "192.168.1.10/24" -> "192.168.1.10")
			ipStr := ipWithSubnet
			if idx := strings.Index(ipStr, "/"); idx != -1 {
				ipStr = ipStr[:idx]
			}
			parsed := net.ParseIP(strings.TrimSpace(ipStr))
			if parsed == nil || parsed.IsLoopback() {
				continue
			}
			hostIPs = append(hostIPs, parsed.String())
		}
	}

	return hostIPs, nil
}

// rewriteHostCandidateToLocalIPs transforms a gathered ICE candidate based on pre-discovered local host IPs.
// If the candidate is a host candidate and localIPs are provided, it returns a slice of virtual
// Srflx candidates mapped to each local IP. Otherwise, it returns the candidate as-is.
func rewriteHostCandidateToLocalIPs(candidate *pion.ICECandidate, localIPs []string) []*pion.ICECandidate {
	if candidate == nil {
		return nil
	}

	if candidate.Typ == pion.ICECandidateTypeHost && candidate.Protocol == pion.ICEProtocolUDP && len(localIPs) > 0 {
		var mapped []*pion.ICECandidate
		for _, address := range localIPs {
			candCopy := *candidate
			candCopy.Typ = pion.ICECandidateTypeSrflx
			candCopy.Address = address
			mapped = append(mapped, &candCopy)
		}
		return mapped
	}
	return []*pion.ICECandidate{candidate}
}
