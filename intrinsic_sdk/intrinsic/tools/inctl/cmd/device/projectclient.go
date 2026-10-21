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

package device

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"path/filepath"

	"intrinsic/tools/inctl/auth/auth"

	"github.com/intrinsic-ai/insrc/incode/cloud/devicemanager/shared"

	"google.golang.org/grpc"

	clustermanagergrpcpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
	clustermanagerpb "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto"
)

var (
	// These will be returned on corresponding http error codes, since they are errors that are
	// expected and can be printed with better UX than just the number.
	errNotFound     = fmt.Errorf("Not found")
	errBadGateway   = fmt.Errorf("Bad Gateway")
	errUnauthorized = fmt.Errorf("Unauthorized")
)

// authedClient injects an api key for the project into every request.
type authedClient struct {
	client       *http.Client
	baseURL      url.URL
	projectName  string
	organization string
	grpcConn     *grpc.ClientConn
	grpcClient   clustermanagergrpcpb.ClustersServiceClient
}

// newClient returns a http.Client compatible that injects auth for the project into every request.
func newClient(ctx context.Context, projectName string, orgName string, clusterName string) (authedClient, error) {
	// create a cloud connection to the cluster via the relay with a callback to get the token source
	opts := []auth.ConnectionOptsFunc{
		auth.WithProject(projectName), auth.WithOrg(orgName), auth.WithCluster(clusterName),
	}
	conn, err := auth.NewCloudConnection(ctx, opts...)
	if err != nil {
		return authedClient{}, err
	}
	// create a http client from the cloud connection
	cl, err := auth.NewCloudClient(ctx, opts...)
	if err != nil {
		return authedClient{}, err
	}

	return authedClient{
		client: cl,
		baseURL: url.URL{
			Scheme: "https",
			Host:   fmt.Sprintf("www.endpoints.%s.cloud.goog", projectName),
			Path:   "/api/devices/",
		},
		projectName:  projectName,
		organization: orgName,
		grpcConn:     conn,
		grpcClient:   clustermanagergrpcpb.NewClustersServiceClient(conn),
	}, nil
}

// close closes the grpc connection if it exists.
func (c *authedClient) close() error {
	if c.grpcConn != nil {
		return c.grpcConn.Close()
	}
	return nil
}

func (c *authedClient) getStatusNetwork(ctx context.Context, clusterName, deviceID string) (map[string]shared.StatusInterface, error) {
	req := &clustermanagerpb.GetStatusRequest{
		ClusterId: clusterName,
		DeviceId:  deviceID,
	}
	resp, err := c.grpcClient.GetStatus(ctx, req)
	if err != nil {
		return nil, err
	}
	statusNetwork := map[string]shared.StatusInterface{}
	for in, ifa := range resp.GetInterfaces() {
		statusNetwork[in] = shared.StatusInterface{
			IPAddress: ifa.GetAddresses(),
		}
	}
	return statusNetwork, nil
}

func translateNetworkConfig(n *clustermanagerpb.IntOSNetworkConfig) map[string]shared.Interface {
	configMap := map[string]shared.Interface{}
	for name, inf := range n.GetInterfaces() {
		ns := inf.GetNameservers()
		configMap[name] = shared.Interface{
			DHCP4:    inf.GetDhcp4(),
			Gateway4: inf.GetGateway4(),
			DHCP6:    &inf.Dhcp6,
			Gateway6: inf.GetGateway6(),
			MTU:      int64(inf.GetMtu()),
			Nameservers: shared.Nameservers{
				Search:    ns.GetSearch(),
				Addresses: ns.GetAddresses(),
			},
			Addresses: inf.GetAddresses(),
			Realtime:  inf.GetRealtime(),
			EtherType: int64(inf.GetEtherType()),
		}
	}
	return configMap
}

func translateToNetworkConfig(n map[string]shared.Interface) *clustermanagerpb.IntOSNetworkConfig {
	c := &clustermanagerpb.IntOSNetworkConfig{
		Interfaces: make(map[string]*clustermanagerpb.IntOSInterfaceConfig),
	}
	for name, inf := range n {
		dhcp6 := false
		if inf.DHCP6 != nil {
			dhcp6 = *inf.DHCP6
		}
		conf := &clustermanagerpb.IntOSInterfaceConfig{
			Dhcp4:    inf.DHCP4,
			Gateway4: inf.Gateway4,
			Dhcp6:    dhcp6,
			Gateway6: inf.Gateway6,
			Mtu:      int32(inf.MTU),
			Nameservers: &clustermanagerpb.NameserverConfig{
				Search:    inf.Nameservers.Search,
				Addresses: inf.Nameservers.Addresses,
			},
			Addresses: inf.Addresses,
			Realtime:  inf.Realtime,
		}
		switch inf.EtherType {
		default:
			conf.EtherType = clustermanagerpb.IntOSInterfaceConfig_ETHER_TYPE_UNSPECIFIED
		case shared.EtherTypeEtherCAT:
			conf.EtherType = clustermanagerpb.IntOSInterfaceConfig_ETHER_TYPE_ETHERCAT
		case shared.EtherTypeIPRealtime:
			conf.EtherType = clustermanagerpb.IntOSInterfaceConfig_ETHER_TYPE_REALTIME
		}
		c.Interfaces[name] = conf
	}
	return c
}

func (c *authedClient) getNetworkConfig(ctx context.Context, clusterName, deviceID string) (map[string]shared.Interface, error) {
	req := &clustermanagerpb.GetNetworkConfigRequest{
		Cluster: clusterName,
		Device:  deviceID,
	}
	resp, err := c.grpcClient.GetNetworkConfig(ctx, req)
	if err != nil {
		return nil, err
	}
	return translateNetworkConfig(resp), nil
}

// postDevice acts similar to [http.Post] but takes a context and injects base path of the device manager for the project.
func (c *authedClient) postDevice(ctx context.Context, cluster, deviceID, subPath string, body io.Reader) (*http.Response, error) {
	reqURL := c.baseURL

	reqURL.Path = filepath.Join(reqURL.Path, subPath)
	reqURL.RawQuery = url.Values{"device-id": []string{deviceID}, "cluster": []string{cluster}}.Encode()

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, reqURL.String(), body)
	if err != nil {
		return nil, err
	}

	return c.client.Do(req)
}

// getDevice acts similar to [http.Get] but takes a context and injects base path of the device manager for the project.
func (c *authedClient) getDevice(ctx context.Context, cluster, deviceID, subPath string) (*http.Response, error) {
	reqURL := c.baseURL

	reqURL.Path = filepath.Join(reqURL.Path, subPath)
	reqURL.RawQuery = url.Values{"device-id": []string{deviceID}, "cluster": []string{cluster}}.Encode()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL.String(), nil)
	if err != nil {
		return nil, err
	}

	return c.client.Do(req)
}

// getJSON acts similar to [GetDevice] but also does [json.Decode] and enforces [http.StatusOK].
func (c *authedClient) getJSON(ctx context.Context, cluster, deviceID, subPath string, value any) error {
	resp, err := c.getDevice(ctx, cluster, deviceID, subPath)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		if resp.StatusCode == http.StatusNotFound {
			return errNotFound
		}
		if resp.StatusCode == http.StatusBadGateway {
			return errBadGateway
		}
		if resp.StatusCode == http.StatusUnauthorized {
			return errUnauthorized
		}

		return fmt.Errorf("get status code: %v", resp.StatusCode)
	}

	return json.NewDecoder(resp.Body).Decode(value)
}
