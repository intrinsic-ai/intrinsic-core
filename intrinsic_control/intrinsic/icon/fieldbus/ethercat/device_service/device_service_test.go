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

package deviceservice

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"os"
	"strings"
	"sync"
	testing "testing"

	"intrinsic/assets/data/fakedataassets"
	"intrinsic/testing/grpctest"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials/local"
	"google.golang.org/grpc/status"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"
	anypb "google.golang.org/protobuf/types/known/anypb"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	dagrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	ipb "intrinsic/assets/proto/id_go_proto"
	mpb "intrinsic/assets/proto/metadata_go_proto"
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"
	dscpb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_config_go_proto"
	dspb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_go_proto"
	esipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/esi_go_proto"
	ecatpb "intrinsic/icon/hal/lib/service_variable_value/v1/service_variable_config_go_proto"
)

// Required (apparently) so that flags are parsed.
func TestMain(m *testing.M) {
	flag.Parse()
	os.Exit(m.Run())
}

// ESI Helpers for Test Setup

// testEsiSubItem represents a sub-entry within a complex DataType.
type testEsiSubItem struct {
	SubIdx     int
	Name       string
	Type       string
	BitSize    int
	PdoMapping string // Optional: T, R, or RT
}

// testEsiDataType represents a complex EtherCAT DataType definition.
type testEsiDataType struct {
	Name     string
	BitSize  int
	SubItems []testEsiSubItem
}

// makeEsiDataTypeXml generates the XML string for a <DataType> block including its SubItems.
func makeEsiDataTypeXml(dt testEsiDataType) string {
	var subItemsXML strings.Builder
	for _, si := range dt.SubItems {
		pdoMapping := ""
		if si.PdoMapping != "" {
			pdoMapping = fmt.Sprintf("<Flags><PdoMapping>%s</PdoMapping></Flags>", si.PdoMapping)
		}
		subItemsXML.WriteString(fmt.Sprintf(`
                <SubItem>
                  <SubIdx>%d</SubIdx>
                  <Name>%s</Name>
                  <Type>%s</Type>
                  <BitSize>%d</BitSize>
                  %s
                </SubItem>`, si.SubIdx, si.Name, si.Type, si.BitSize, pdoMapping))
	}
	return fmt.Sprintf(`
              <DataType>
                <Name>%s</Name>
                <BitSize>%d</BitSize>%s
              </DataType>`, dt.Name, dt.BitSize, subItemsXML.String())
}

// testEsiObject represents a descriptive entry for an EtherCAT Object Dictionary item.
type testEsiObject struct {
	Index         string
	ObjName       string
	ObjType       string // Defaults to UINT
	ObjBitSize    int    // Defaults to 16
	ObjAccess     string // Defaults to ro
	ObjPdoMapping string // Optional: T, R, or RT
}

// makeEsiObjectXml generates the XML string for a single <Object> entry.
func makeEsiObjectXml(o testEsiObject) string {
	if o.ObjType == "" {
		o.ObjType = "UINT"
	}
	if o.ObjBitSize == 0 {
		o.ObjBitSize = 16
	}
	if o.ObjAccess == "" {
		o.ObjAccess = "ro"
	}
	pdoMapping := ""
	if o.ObjPdoMapping != "" {
		pdoMapping = fmt.Sprintf("<PdoMapping>%s</PdoMapping>", o.ObjPdoMapping)
	}
	return fmt.Sprintf(`
              <Object>
                <Index>%s</Index>
                <Name>%s</Name>
                <Type>%s</Type>
                <BitSize>%d</BitSize>
                <Flags><Access>%s</Access>%s</Flags>
              </Object>`, o.Index, o.ObjName, o.ObjType, o.ObjBitSize, o.ObjAccess, pdoMapping)
}

// testPdoEntry represents an entry within a PDO (Process Data Object).
type testPdoEntry struct {
	Index     string
	SubIndex  int
	BitLen    int
	EntryName string
	DataType  string // Optional
}

// makeEsiPdoXml generates the XML string for a <TxPdo> or <RxPdo> block.
func makeEsiPdoXml(pdoType string, index string, name string, fixed bool, sm int, entries ...testPdoEntry) string {
	fixedStr := "0"
	if fixed {
		fixedStr = "1"
	}
	smAttr := ""
	if sm != 0 {
		smAttr = fmt.Sprintf(` Sm="%d"`, sm)
	}

	var entriesXML strings.Builder
	for _, e := range entries {
		dataType := ""
		if e.DataType != "" {
			dataType = fmt.Sprintf("<DataType>%s</DataType>", e.DataType)
		}
		entriesXML.WriteString(fmt.Sprintf(`
          <Entry>
            <Index>%s</Index>
            <SubIndex>%d</SubIndex>
            <BitLen>%d</BitLen>
            <Name>%s</Name>
            %s
          </Entry>`, e.Index, e.SubIndex, e.BitLen, e.EntryName, dataType))
	}

	return fmt.Sprintf(`
        <%sPdo Fixed="%s"%s>
          <Index>%s</Index>
          <Name>%s</Name>%s
        </%sPdo>`, pdoType, fixedStr, smAttr, index, name, entriesXML.String(), pdoType)
}

// makeTestEsiXml wraps data types, objects and PDOs into a complete <EtherCATInfo> XML document for testing.
func makeTestEsiXml(dataTypes []string, objects []string, pdos []string) string {
	dataTypesSection := ""
	if len(dataTypes) > 0 {
		dataTypesSection = fmt.Sprintf("\n            <DataTypes>%s\n            </DataTypes>", strings.Join(dataTypes, ""))
	}
	return fmt.Sprintf(`<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">TestDevice</Type>
        <Name>Test Device</Name>
        <Profile>
          <Dictionary>%s
            <Objects>%s
            </Objects>
          </Dictionary>
        </Profile>%s
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>`, dataTypesSection, strings.Join(objects, ""), strings.Join(pdos, ""))
}

// TestDeviceService tests the creation of the DeviceService and the GetConfiguration call.
func TestDeviceService(t *testing.T) {
	bundleID := &ipb.Id{
		Package: "intrinsic_proto.fieldbus.ethercat.test",
		Name:    "test_bundle_1",
	}
	iface := "data://" + esiBundleDataAssetProtoName
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{
			VendorId:    0x0001,
			ProductCode: 0x0002,
			Revision:    0x0003,
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				iface: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{
							Id: bundleID,
						},
					},
				},
			},
		},
	}

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"path/to/esi/file.esi": {Data: makeTestEsiXml(nil, nil, nil)},
		},
	}

	bundleAny, err := anypb.New(bundle)
	if err != nil {
		t.Fatalf("Failed to marshal ESI bundle: %v", err)
	}
	validDataAsset := &dapb.DataAsset{
		Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
		Data:     bundleAny,
	}

	bundleAbsPath := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"/path/to/esi/file.esi": {Data: "test esi data"},
		},
	}
	bundleAbsPathAny, err := anypb.New(bundleAbsPath)
	if err != nil {
		t.Fatalf("Failed to marshal ESI bundle with absolute path: %v", err)
	}
	absPathDataAsset := &dapb.DataAsset{
		Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
		Data:     bundleAbsPathAny,
	}

	bundleDotDotPath := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"../file.esi": {Data: "test esi data"},
		},
	}
	bundleDotDotPathAny, err := anypb.New(bundleDotDotPath)
	if err != nil {
		t.Fatalf("Failed to marshal ESI bundle with .. in path: %v", err)
	}
	dotDotPathDataAsset := &dapb.DataAsset{
		Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
		Data:     bundleDotDotPathAny,
	}

	wrongProtoDataAsset := &dapb.DataAsset{
		Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
		Data:     &anypb.Any{Value: []byte("invalid data"), TypeUrl: "type.googleapis.com/google.protobuf.Duration"},
	}

	emptyResolvedConfig := &dspb.ResolvedConfiguration{}

	type testArgs struct {
		config     *dscpb.DeviceServiceConfig
		dataAssets []*dapb.DataAsset
	}

	tests := []struct {
		desc            string
		testArgs        testArgs
		wantResponse    *dspb.GetConfigurationResponse
		wantErr         error
		wantErrCode     codes.Code
		wantErrContains string
	}{
		{
			desc: "valid config",
			testArgs: testArgs{
				config:     config,
				dataAssets: []*dapb.DataAsset{validDataAsset},
			},
			wantResponse: &dspb.GetConfigurationResponse{
				DeviceServiceConfig:   config,
				EsiBundle:             bundle,
				ResolvedConfiguration: emptyResolvedConfig,
			},
		},
		{
			desc: "data asset not found",
			testArgs: testArgs{
				config:     config,
				dataAssets: []*dapb.DataAsset{},
			},
			wantErrCode: codes.NotFound,
		},
		{
			desc: "wrong proto type in data asset",
			testArgs: testArgs{
				config:     config,
				dataAssets: []*dapb.DataAsset{wrongProtoDataAsset},
			},
			wantErr: ErrEsiUnmarshal,
		},
		{
			desc: "bundle with absolute path",
			testArgs: testArgs{
				config:     config,
				dataAssets: []*dapb.DataAsset{absPathDataAsset},
			},
			wantErr: ErrEsiInvalidPath,
		},
		{
			desc: "bundle with .. at the start of the path",
			testArgs: testArgs{
				config:     config,
				dataAssets: []*dapb.DataAsset{dotDotPathDataAsset},
			},
			wantErr: ErrEsiInvalidPath,
		},
		{
			desc: "nil config",
			testArgs: testArgs{
				config: nil,
			},
			wantErr: ErrConfigNil,
		},
		{
			desc: "no device identifier",
			testArgs: testArgs{
				config: &dscpb.DeviceServiceConfig{},
			},
			wantErr: ErrNoDeviceIdentifier,
		},
		{
			desc: "no esi bundle data asset id in config",
			testArgs: testArgs{
				config: &dscpb.DeviceServiceConfig{
					DeviceIdentifier: &dscpb.DeviceIdentifier{},
				},
			},
			wantErrContains: "interface not found in resolved dependency",
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()
			var client dagrpcpb.DataAssetsClient
			if tc.testArgs.dataAssets != nil {
				fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets(tc.testArgs.dataAssets))
				client = fakeDA.Client
			}

			service, err := NewDeviceService(ctx, tc.testArgs.config, client)
			if tc.wantErrCode != codes.OK {
				if status.Code(err) != tc.wantErrCode {
					t.Fatalf("NewDeviceService(...) error code = %v, want %v (err: %v)", status.Code(err), tc.wantErrCode, err)
				}
				return
			}
			if tc.wantErrContains != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErrContains) {
					t.Fatalf("NewDeviceService(...) error = %v, want error containing %q", err, tc.wantErrContains)
				}
				return
			}
			if tc.wantErr != nil {
				if err == nil {
					t.Fatalf("NewDeviceService(...) = nil error, want non-nil error matching %v", tc.wantErr)
				}
				if !errors.Is(err, tc.wantErr) {
					t.Fatalf("NewDeviceService(...) = %v, want error to wrap %v", err, tc.wantErr)
				}
			} else if err != nil {
				t.Fatalf("NewDeviceService(...) = %v, want nil error", err)
			}
			// If we expected an error, and got it, we are done with this test case.
			if tc.wantErr != nil {
				return
			}
			// If tc.wantErr was nil, err must also be nil at this point. Proceed to check response.
			if tc.wantResponse != nil {
				if service == nil {
					t.Fatalf("NewDeviceService() returned nil service, expected a valid service")
				}

				gotConfig, err := service.GetConfiguration(context.Background(), &dspb.GetConfigurationRequest{})
				if err != nil {
					t.Fatalf("GetConfiguration() returned unexpected error: %v", err)
				}

				if diff := cmp.Diff(tc.wantResponse, gotConfig, protocmp.Transform()); diff != "" {
					t.Errorf("GetConfiguration() returned diff (-want +got):\n%s", diff)
				}
			}
		})
	}
}

func TestDiscoveryLogic(t *testing.T) {
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6040", ObjName: "Control word", ObjAccess: "rw", ObjPdoMapping: "RT"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6041", ObjName: "Status word", ObjPdoMapping: "T"}),
		makeEsiObjectXml(testEsiObject{Index: "#x603F", ObjName: "Error code", ObjPdoMapping: "T"}),
		makeEsiObjectXml(testEsiObject{Index: "#x100", ObjName: "Ambiguous Object", ObjPdoMapping: "T"}),
		makeEsiObjectXml(testEsiObject{Index: "#x101", ObjName: "Ambiguous Object", ObjPdoMapping: "T"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6071", ObjName: "Target torque", ObjAccess: "rw", ObjPdoMapping: "R"}),
		makeEsiObjectXml(testEsiObject{Index: "#x2000", ObjName: "Duplicate Name", ObjPdoMapping: "RT"}),
		makeEsiObjectXml(testEsiObject{Index: "#x3000", ObjName: "Duplicate Name", ObjPdoMapping: "RT"}),
		makeEsiObjectXml(testEsiObject{Index: "#x1008", ObjName: "Manufacturer Device Name", ObjType: "STRING(16)", ObjBitSize: 128}),
	}

	pdos := []string{
		makeEsiPdoXml("Tx", "#x1A00", "Default TxPDO", false, 3, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word", DataType: "UINT"}),
		makeEsiPdoXml("Tx", "#x1A03", "Duplicate PDO", false, 0, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word", DataType: "UINT"}),
		makeEsiPdoXml("Tx", "#x1A04", "Duplicate PDO", false, 0, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word", DataType: "UINT"}),
		makeEsiPdoXml("Rx", "#x1600", "Default RxPDO", false, 2,
			testPdoEntry{Index: "#x6040", SubIndex: 0, BitLen: 16, EntryName: "Control word", DataType: "UINT"},
			testPdoEntry{Index: "#x2000", SubIndex: 0, BitLen: 16, DataType: "UINT"},
		),
		makeEsiPdoXml("Tx", "#x1A01", "Optional TxPDO", false, 0, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word", DataType: "UINT"}),
	}
	// Exclude #x1A00 from #x1A01 manually since makeEsiPdo doesn't support adding Excludes.
	pdos[4] = strings.Replace(pdos[4], "<Name>Optional TxPDO</Name>", "<Name>Optional TxPDO</Name><Exclude>#x1A00</Exclude>", 1)

	pdos = append(pdos, makeEsiPdoXml("Tx", "#x1A02", "Fixed TxPDO", true, 3, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word", DataType: "UINT"}))

	esiData := makeTestEsiXml(nil, objects, pdos)

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: esiData},
		},
	}

	tests := []struct {
		desc     string
		mappings []*dscpb.InterfaceMapping
		wantEbi  *dspb.ResolvedConfiguration_EbiPdoInstructions
		wantMap  map[string]*dspb.ResolvedPdoVariable
		wantErr  string
	}{
		{
			desc: "Standard Name",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Status word"},
							},
						},
					},
				},
			},
			// Expect basic name resolution within an explicitly named PDO.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default TxPDO::Status word": {PdoIndex: 0x1A00, Index: 0x6041, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Status word"},
			},
		},
		{
			desc: "Hex Prefix (#x)",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "#x1A00", Object: "#x6041.0"},
							},
						},
					},
				},
			},
			// Expect numeric address resolution using the #x prefix.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"#x1A00::#x6041.0": {PdoIndex: 0x1A00, Index: 0x6041, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Status word"},
			},
		},
		{
			desc: "Hex Prefix (0x)",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "0x1A00", Object: "0x6041.0"},
							},
						},
					},
				},
			},
			// Expect numeric address resolution using the 0x prefix.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"0x1A00::0x6041.0": {PdoIndex: 0x1A00, Index: 0x6041, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Status word"},
			},
		},
		{
			desc: "Implicit PDO",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Object: "Status word"},
							},
						},
					},
				},
			},
			// Expect engine to pick the default-active PDO (#x1A02) containing the variable.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"::Status word": {PdoIndex: 0x1A02, Index: 0x6041, SubIndex: 0, PdoEniName: "Fixed TxPDO", ObjectEniName: "Status word"},
			},
		},
		{
			desc: "Add to Dynamic",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								ErrorCodeReference: &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Error code"},
							},
						},
					},
				},
			},
			// Expect an EBI instruction to add the variable to a non-fixed (dynamic) PDO.
			wantEbi: &dspb.ResolvedConfiguration_EbiPdoInstructions{
				ObjectsToAdd: []*dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition{
					{PdoIndex: 0x1A00, ObjectIndex: 0x603F, ObjectSubIndex: 0, DataType: "UINT", BitSize: 16, Name: "Error code"},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default TxPDO::Error code": {PdoIndex: 0x1A00, Index: 0x603F, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Error code"},
			},
		},
		{
			desc: "Enable Optional",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "Optional TxPDO", Object: "Status word"},
							},
						},
					},
				},
			},
			// Expect PDO #x1A01 to be enabled and its exclusive peer #x1A00 to be disabled.
			wantEbi: &dspb.ResolvedConfiguration_EbiPdoInstructions{
				PdoExclusionsToAdd:    []uint32{0x1A00},
				PdoExclusionsToRemove: []uint32{0x1A01},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Optional TxPDO::Status word": {PdoIndex: 0x1A01, Index: 0x6041, SubIndex: 0, PdoEniName: "Optional TxPDO", ObjectEniName: "Status word"},
			},
		},
		{
			desc: "Name Override",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Object: "name:0x100"},
							},
						},
					},
				},
			},
			wantErr: "could not be resolved",
		},
		{
			desc: "Fixed PDO error",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								ErrorCodeReference: &dscpb.VariableReference{Pdo: "Fixed TxPDO", Object: "Error code"},
							},
						},
					},
				},
			},
			wantErr: "cannot add variable to fixed PDO",
		},
		{
			desc: "Not Mappable error",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Object: "Manufacturer Device Name"},
							},
						},
					},
				},
			},
			wantErr: "not mappable to PDO",
		},
		{
			desc: "Ambiguous PDO Name",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "Duplicate PDO", Object: "Status word"},
							},
						},
					},
				},
			},
			wantErr: "ambiguous PDO name",
		},
		{
			desc: "Ambiguous Object Name",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Object: "Ambiguous Object"},
							},
						},
					},
				},
			},
			wantErr: "ambiguous object name",
		},
		{
			desc: "Direction Mismatch",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Target torque"},
							},
						},
					},
				},
			},
			wantErr: "not mappable to PDO",
		},
		{
			desc: "Bi-directional mapping (RT)",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								// Control word is "RT". We explicitly ask for it in a TxPDO.
								StatusWordReference: &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Control word"},
							},
						},
					},
				},
			},
			// Expect RT object to be added to a TxPDO when explicitly requested.
			wantEbi: &dspb.ResolvedConfiguration_EbiPdoInstructions{
				ObjectsToAdd: []*dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition{
					{PdoIndex: 0x1A00, ObjectIndex: 0x6040, ObjectSubIndex: 0, DataType: "UINT", BitSize: 16, Name: "Control word"},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default TxPDO::Control word": {PdoIndex: 0x1A00, Index: 0x6040, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Control word"},
			},
		},
		{
			desc: "Ambiguity with PDO context",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								// "Duplicate Name" exists globally at two different addresses.
								// One is in Default RxPDO (#x1600), the other is not.
								// Current logic will fail due to ambiguity.
								StatusWordReference: &dscpb.VariableReference{Pdo: "Default RxPDO", Object: "Duplicate Name"},
							},
						},
					},
				},
			},
			// Expect resolution to succeed by scoping the search to the requested PDO context.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default RxPDO::Duplicate Name": {PdoIndex: 0x1600, Index: 0x2000, SubIndex: 0, PdoEniName: "Default RxPDO", ObjectEniName: "Duplicate Name"},
			},
		},
		{
			desc: "ProcessVariableDeviceData Resolution",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_BusVariableDeviceData{
							BusVariableDeviceData: &dscpb.ProcessVariableDeviceData{
								BusVariableWrites: []*dscpb.BusVariableWrite{
									{
										VariableReference: &dscpb.VariableReference{Pdo: "Default RxPDO", Object: "Control word"},
										Value: &dscpb.BusVariableValue{
											Value: &dscpb.BusVariableValue_Uint16Value{Uint16Value: 123},
										},
									},
								},
							},
						},
					},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default RxPDO::Control word": {PdoIndex: 0x1600, Index: 0x6040, SubIndex: 0, PdoEniName: "Default RxPDO", ObjectEniName: "Control word"},
			},
		},
		{
			desc: "ProcessVariableDeviceData Type Mismatch",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_BusVariableDeviceData{
							BusVariableDeviceData: &dscpb.ProcessVariableDeviceData{
								BusVariableWrites: []*dscpb.BusVariableWrite{
									{
										VariableReference: &dscpb.VariableReference{Pdo: "Default RxPDO", Object: "Control word"},
										Value: &dscpb.BusVariableValue{
											// Control word is UINT (16 bits), but we provide uint8
											Value: &dscpb.BusVariableValue_Uint8Value{Uint8Value: 123},
										},
									},
								},
							},
						},
					},
				},
			},
			wantErr: "type mismatch",
		},
		{
			desc: "PDO Conflict Error",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								StatusWordReference: &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Status word"},
								ErrorCodeReference:  &dscpb.VariableReference{Pdo: "Optional TxPDO", Object: "Error code"},
							},
						},
					},
				},
			},
			// Expect an error because Optional TxPDO excludes Default TxPDO.
			wantErr: "PDO conflict detected",
		},
		{
			desc: "JointDeviceData Resolution",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_JointDeviceData{
							JointDeviceData: &dscpb.JointDeviceData{
								JointPositionCommandReference: &dscpb.VariableReference{Pdo: "Default RxPDO", Object: "Control word"},
								JointPositionStateReference:   &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Status word"},
								JointVelocityStateReference:   &dscpb.VariableReference{Pdo: "Default TxPDO", Object: "Error code"},
							},
						},
					},
				},
			},
			// Expect resolution of multiple joint-related variables across different PDOs.
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"Default RxPDO::Control word": {PdoIndex: 0x1600, Index: 0x6040, SubIndex: 0, PdoEniName: "Default RxPDO", ObjectEniName: "Control word"},
				"Default TxPDO::Status word":  {PdoIndex: 0x1A00, Index: 0x6041, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Status word"},
				"Default TxPDO::Error code":   {PdoIndex: 0x1A00, Index: 0x603F, SubIndex: 0, PdoEniName: "Default TxPDO", ObjectEniName: "Error code"},
			},
			wantEbi: &dspb.ResolvedConfiguration_EbiPdoInstructions{
				ObjectsToAdd: []*dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition{
					{PdoIndex: 0x1A00, ObjectIndex: 0x603F, ObjectSubIndex: 0, DataType: "UINT", BitSize: 16, Name: "Error code"},
				},
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()
			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier: &dscpb.DeviceIdentifier{
					VendorId:    1,
					ProductCode: 2,
					Revision:    3,
				},
				InterfaceMappings: tc.mappings,
			}

			fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
				{
					Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
					Data:     mustMarshalAny(bundle),
				},
			}))
			config.EsiBundle = &rdpb.ResolvedDependency{
				Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
					"data://" + esiBundleDataAssetProtoName: {
						Protocol: &rdpb.ResolvedDependency_Interface_Data_{
							Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
						},
					},
				},
			}

			service, err := NewDeviceService(ctx, config, fakeDA.Client)
			if tc.wantErr != "" && err != nil {
				if !strings.Contains(strings.ToLower(err.Error()), strings.ToLower(tc.wantErr)) {
					t.Fatalf("NewDeviceService error = %v, want error containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("NewDeviceService failed: %v", err)
			}

			resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
			if tc.wantErr != "" {
				if err == nil {
					t.Fatalf("GetConfiguration matched no error, want error containing %q", tc.wantErr)
				}
				if !strings.Contains(strings.ToLower(err.Error()), strings.ToLower(tc.wantErr)) {
					t.Fatalf("GetConfiguration error = %v, want error containing %q", err, tc.wantErr)
				}
				return
			}

			if err != nil {
				t.Fatalf("GetConfiguration failed: %v", err)
			}

			resolved := resp.GetResolvedConfiguration()
			if diff := cmp.Diff(tc.wantEbi, resolved.GetEbiPdoInstructions(), protocmp.Transform()); diff != "" {
				t.Errorf("EBI instructions diff (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.wantMap, resolved.GetPdoMappings(), protocmp.Transform()); diff != "" {
				t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
			}
		})
	}
}

func mustMarshalAny(m proto.Message) *anypb.Any {
	a, err := anypb.New(m)
	if err != nil {
		panic(err)
	}
	return a
}

func TestMultiFileResolution(t *testing.T) {
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6041", ObjName: "External Status Word", ObjPdoMapping: "T"}),
	}
	pdos := []string{
		makeEsiPdoXml("Tx", "#x1A00", "Module TxPDO", false, 3, testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "External Status Word", DataType: "UINT"}),
	}
	moduleEsi := fmt.Sprintf(`<?xml version="1.0" encoding="utf-8"?>
<EtherCATModule>
  <Modules>
    <Module>
      <Type ModuleIdent="1">TestModule</Type>
      <Name>Test Module</Name>%s
    </Module>
  </Modules>
</EtherCATModule>`, strings.Join(pdos, ""))

	dictEsi := fmt.Sprintf(`<?xml version="1.0" encoding="utf-8"?>
<Dictionary>
  <Objects>%s
  </Objects>
</Dictionary>`, strings.Join(objects, ""))

	mainEsi := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device>
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">MainDevice</Type>
        <Name>Main Device</Name>
        <Profile><DictionaryFile>dict.xml</DictionaryFile></Profile>
      </Device>
    </Devices>
  </Descriptions>
  <InfoReference>module.xml</InfoReference>
</EtherCATInfo>`

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"main.xml":   {Data: mainEsi},
			"dict.xml":   {Data: dictEsi},
			"module.xml": {Data: moduleEsi},
		},
	}

	ctx := context.Background()
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
		InterfaceMappings: []*dscpb.InterfaceMapping{
			{
				DeviceData: &dscpb.DeviceData{
					Data: &dscpb.DeviceData_Ds402DeviceData{
						Ds402DeviceData: &dscpb.Ds402DeviceData{
							StatusWordReference: &dscpb.VariableReference{Pdo: "Module TxPDO", Object: "External Status Word"},
						},
					},
				},
			},
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				"data://" + esiBundleDataAssetProtoName: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
					},
				},
			},
		},
	}

	fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
		{
			Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
			Data:     mustMarshalAny(bundle),
		},
	}))

	service, err := NewDeviceService(ctx, config, fakeDA.Client)
	if err != nil {
		t.Fatalf("NewDeviceService failed: %v", err)
	}

	resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
	if err != nil {
		t.Fatalf("GetConfiguration failed: %v", err)
	}

	wantMap := map[string]*dspb.ResolvedPdoVariable{
		"Module TxPDO::External Status Word": {
			PdoIndex: 0x1A00, Index: 0x6041, SubIndex: 0, PdoEniName: "Module TxPDO", ObjectEniName: "External Status Word",
		},
	}

	if diff := cmp.Diff(wantMap, resp.GetResolvedConfiguration().GetPdoMappings(), protocmp.Transform()); diff != "" {
		t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
	}
}

func TestRealWorldStyleResolution(t *testing.T) {
	// Mimic VIPA MDP structure with backslashes and EtherCATModule root as found in
	// g..gl3/third_party/......._eni_builder_sdk/UI/EniBuilder/Run/EtherCAT/Vipa 053-1EC00 MDP.xml
	mainEsi := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.3">
  <Vendor><Id>45054</Id><Name>VIPA GmbH</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device>
        <Type ProductCode="#x0531EC00" RevisionNo="#x00010001">VIPA 053-1EC00</Type>
        <Name>VIPA 053-1EC00 EtherCAT Fieldbus coupler (MDP)</Name>
      </Device>
    </Devices>
  </Descriptions>
  <InfoReference>VIPA 053-1EC00\VIPA 053-1EC00 Modules.xml</InfoReference>
</EtherCATInfo>`

	moduleEsi := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATModule Version="1.3">
  <Modules>
    <Module>
      <Type ModuleIdent="#x00019F82">021-1BB00</Type>
      <Name>VIPA 021-1BB00, DI 2xDC 24V</Name>
      <TxPdo Fixed="1" Mandatory="1" Sm="3">
        <Index>#x1a00</Index>
        <Name>Inputs</Name>
        <Entry>
          <Index>#x6000</Index>
          <SubIndex>1</SubIndex>
          <BitLen>1</BitLen>
          <Name>DI 0</Name>
          <DataType>BOOL</DataType>
        </Entry>
      </TxPdo>
    </Module>
  </Modules>
</EtherCATModule>`

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"main.xml": {Data: mainEsi},
			`VIPA 053-1EC00\VIPA 053-1EC00 Modules.xml`: {Data: moduleEsi},
		},
	}

	ctx := context.Background()
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 45054, ProductCode: 0x0531EC00, Revision: 0x00010001},
		InterfaceMappings: []*dscpb.InterfaceMapping{
			{
				DeviceData: &dscpb.DeviceData{
					Data: &dscpb.DeviceData_Ds402DeviceData{
						Ds402DeviceData: &dscpb.Ds402DeviceData{
							StatusWordReference: &dscpb.VariableReference{Pdo: "Inputs", Object: "DI 0"},
						},
					},
				},
			},
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				"data://" + esiBundleDataAssetProtoName: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "vipa_bundle"}},
					},
				},
			},
		},
	}

	fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
		{
			Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "vipa_bundle"}}},
			Data:     mustMarshalAny(bundle),
		},
	}))

	service, err := NewDeviceService(ctx, config, fakeDA.Client)
	if err != nil {
		t.Fatalf("NewDeviceService failed: %v", err)
	}

	resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
	if err != nil {
		t.Fatalf("GetConfiguration failed: %v", err)
	}

	wantMap := map[string]*dspb.ResolvedPdoVariable{
		"Inputs::DI 0": {
			PdoIndex: 0x1A00, Index: 0x6000, SubIndex: 1, PdoEniName: "Inputs", ObjectEniName: "DI 0",
		},
	}

	if diff := cmp.Diff(wantMap, resp.GetResolvedConfiguration().GetPdoMappings(), protocmp.Transform()); diff != "" {
		t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
	}
}

func TestDs402HomingResolution(t *testing.T) {
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6060", ObjName: "Modes of operation", ObjType: "INT", ObjBitSize: 8, ObjPdoMapping: "RT"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6061", ObjName: "Modes of operation display", ObjType: "INT", ObjBitSize: 8, ObjPdoMapping: "T"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6098", ObjName: "Homing method", ObjType: "INT", ObjBitSize: 8, ObjPdoMapping: "RT"}),
	}
	pdos := []string{
		makeEsiPdoXml("Rx", "#x1600", "RxPDO", false, 2),
		makeEsiPdoXml("Tx", "#x1A00", "TxPDO", false, 3),
	}
	esiData := makeTestEsiXml(nil, objects, pdos)

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: esiData},
		},
	}

	tests := []struct {
		desc    string
		homing  *dscpb.HomingReference
		wantEbi *dspb.ResolvedConfiguration_EbiPdoInstructions
		wantMap map[string]*dspb.ResolvedPdoVariable
		wantErr string
	}{
		{
			desc: "Hybrid Manual Homing Mapping (PDO + implicit SDO)",
			homing: &dscpb.HomingReference{
				Configuration: &dscpb.HomingReference_Manual_{
					Manual: &dscpb.HomingReference_Manual{
						// Only map mode-related variables to PDOs.
						// Homing method is NOT mapped, implying SDO access.
						ModesOfOperation:        &dscpb.VariableReference{Pdo: "RxPDO", Object: "Modes of operation"},
						ModesOfOperationDisplay: &dscpb.VariableReference{Pdo: "TxPDO", Object: "Modes of operation display"},
					},
				},
			},
			wantEbi: &dspb.ResolvedConfiguration_EbiPdoInstructions{
				ObjectsToAdd: []*dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition{
					{PdoIndex: 0x1600, ObjectIndex: 0x6060, ObjectSubIndex: 0, DataType: "INT", BitSize: 8, Name: "Modes of operation"},
					{PdoIndex: 0x1A00, ObjectIndex: 0x6061, ObjectSubIndex: 0, DataType: "INT", BitSize: 8, Name: "Modes of operation display"},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"RxPDO::Modes of operation":         {PdoIndex: 0x1600, Index: 0x6060, SubIndex: 0, PdoEniName: "RxPDO", ObjectEniName: "Modes of operation"},
				"TxPDO::Modes of operation display": {PdoIndex: 0x1A00, Index: 0x6061, SubIndex: 0, PdoEniName: "TxPDO", ObjectEniName: "Modes of operation display"},
			},
		},
		{
			desc: "Auto Homing returns Error",
			homing: &dscpb.HomingReference{
				Configuration: &dscpb.HomingReference_Auto_{
					Auto: &dscpb.HomingReference_Auto{},
				},
			},
			wantErr: "not implemented yet",
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()
			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
				InterfaceMappings: []*dscpb.InterfaceMapping{
					{
						DeviceData: &dscpb.DeviceData{
							Data: &dscpb.DeviceData_Ds402DeviceData{
								Ds402DeviceData: &dscpb.Ds402DeviceData{
									HomingReference: tc.homing,
								},
							},
						},
					},
				},
			}

			fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
				{
					Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
					Data:     mustMarshalAny(bundle),
				},
			}))
			config.EsiBundle = &rdpb.ResolvedDependency{
				Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
					"data://" + esiBundleDataAssetProtoName: {
						Protocol: &rdpb.ResolvedDependency_Interface_Data_{
							Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
						},
					},
				},
			}

			service, err := NewDeviceService(ctx, config, fakeDA.Client)
			if tc.wantErr != "" && err != nil {
				if !strings.Contains(strings.ToLower(err.Error()), strings.ToLower(tc.wantErr)) {
					t.Fatalf("NewDeviceService error = %v, want error containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("NewDeviceService failed: %v", err)
			}

			resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
			if tc.wantErr != "" {
				if err == nil {
					t.Fatalf("GetConfiguration matched no error, want error containing %q", tc.wantErr)
				}
				if !strings.Contains(strings.ToLower(err.Error()), strings.ToLower(tc.wantErr)) {
					t.Fatalf("GetConfiguration error = %v, want error containing %q", err, tc.wantErr)
				}
				return
			}

			if err != nil {
				t.Fatalf("GetConfiguration failed: %v", err)
			}

			resolved := resp.GetResolvedConfiguration()
			if diff := cmp.Diff(tc.wantEbi, resolved.GetEbiPdoInstructions(), protocmp.Transform()); diff != "" {
				t.Errorf("EBI instructions diff (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.wantMap, resolved.GetPdoMappings(), protocmp.Transform()); diff != "" {
				t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestDataTypeResolution(t *testing.T) {
	// Define a complex data type with sub-items using helpers
	dataTypes := []string{
		makeEsiDataTypeXml(testEsiDataType{
			Name:    "ComplexType",
			BitSize: 32,
			SubItems: []testEsiSubItem{
				{SubIdx: 1, Name: "SubItem1", Type: "UINT", BitSize: 16, PdoMapping: "T"},
				{SubIdx: 2, Name: "SubItem2", Type: "UINT", BitSize: 16, PdoMapping: "T"},
			},
		}),
	}
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6000", ObjName: "MainObject", ObjType: "ComplexType", ObjBitSize: 32}),
	}
	pdos := []string{
		makeEsiPdoXml("Tx", "#x1A00", "TxPDO", false, 3,
			testPdoEntry{Index: "#x6000", SubIndex: 1, BitLen: 16, EntryName: "SubItem1"},
			testPdoEntry{Index: "#x6000", SubIndex: 2, BitLen: 16, EntryName: "SubItem2"},
		),
	}
	esiData := makeTestEsiXml(dataTypes, objects, pdos)

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: esiData},
		},
	}

	ctx := context.Background()
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
		InterfaceMappings: []*dscpb.InterfaceMapping{
			{
				DeviceData: &dscpb.DeviceData{
					Data: &dscpb.DeviceData_Ds402DeviceData{
						Ds402DeviceData: &dscpb.Ds402DeviceData{
							// Test resolving by sub-item name
							StatusWordReference: &dscpb.VariableReference{Object: "SubItem1"},
							// Test resolving by address with sub-index
							ErrorCodeReference: &dscpb.VariableReference{Object: "#x6000.2"},
						},
					},
				},
			},
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				"data://" + esiBundleDataAssetProtoName: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "complex_bundle"}},
					},
				},
			},
		},
	}

	fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
		{
			Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "complex_bundle"}}},
			Data:     mustMarshalAny(bundle),
		},
	}))

	service, err := NewDeviceService(ctx, config, fakeDA.Client)
	if err != nil {
		t.Fatalf("NewDeviceService failed: %v", err)
	}

	resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
	if err != nil {
		t.Fatalf("GetConfiguration failed: %v", err)
	}

	wantMap := map[string]*dspb.ResolvedPdoVariable{
		"::SubItem1": {
			PdoIndex: 0x1A00, Index: 0x6000, SubIndex: 1, PdoEniName: "TxPDO", ObjectEniName: "SubItem1",
		},
		"::#x6000.2": {
			PdoIndex: 0x1A00, Index: 0x6000, SubIndex: 2, PdoEniName: "TxPDO", ObjectEniName: "SubItem2",
		},
	}

	if diff := cmp.Diff(wantMap, resp.GetResolvedConfiguration().GetPdoMappings(), protocmp.Transform()); diff != "" {
		t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
	}
}

func TestPreferredDirectionResolution(t *testing.T) {
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6000", ObjName: "Ambiguous Object", ObjPdoMapping: "R"}),
		makeEsiObjectXml(testEsiObject{Index: "#x7000", ObjName: "Ambiguous Object", ObjPdoMapping: "T"}),
	}
	pdos := []string{
		makeEsiPdoXml("Rx", "#x1600", "RxPDO", false, 2, testPdoEntry{Index: "#x6000", SubIndex: 0, BitLen: 16, EntryName: "Ambiguous Object"}),
		makeEsiPdoXml("Tx", "#x1A00", "TxPDO", false, 3, testPdoEntry{Index: "#x7000", SubIndex: 0, BitLen: 16, EntryName: "Ambiguous Object"}),
	}
	esiData := makeTestEsiXml(nil, objects, pdos)

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: esiData},
		},
	}

	tests := []struct {
		desc     string
		mappings []*dscpb.InterfaceMapping
		wantMap  map[string]*dspb.ResolvedPdoVariable
	}{
		{
			desc: "Pick Rx address for Control word (Command)",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								// DS402 Control word is resolved with PreferredDir=Rx.
								// It should pick #x6000 (Mapping=R) even though #x7000 has the same name.
								ControlWordReference: &dscpb.VariableReference{Object: "Ambiguous Object"},
							},
						},
					},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"::Ambiguous Object": {PdoIndex: 0x1600, Index: 0x6000, SubIndex: 0, PdoEniName: "RxPDO", ObjectEniName: "Ambiguous Object"},
			},
		},
		{
			desc: "Pick Tx address for Status word (State)",
			mappings: []*dscpb.InterfaceMapping{
				{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{
								// DS402 Status word is resolved with PreferredDir=Tx.
								// It should pick #x7000 (Mapping=T).
								StatusWordReference: &dscpb.VariableReference{Object: "Ambiguous Object"},
							},
						},
					},
				},
			},
			wantMap: map[string]*dspb.ResolvedPdoVariable{
				"::Ambiguous Object": {PdoIndex: 0x1A00, Index: 0x7000, SubIndex: 0, PdoEniName: "TxPDO", ObjectEniName: "Ambiguous Object"},
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()
			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier:  &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
				InterfaceMappings: tc.mappings,
			}

			fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
				{
					Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
					Data:     mustMarshalAny(bundle),
				},
			}))
			config.EsiBundle = &rdpb.ResolvedDependency{
				Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
					"data://" + esiBundleDataAssetProtoName: {
						Protocol: &rdpb.ResolvedDependency_Interface_Data_{
							Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
						},
					},
				},
			}

			service, err := NewDeviceService(ctx, config, fakeDA.Client)
			if err != nil {
				t.Fatalf("NewDeviceService failed: %v", err)
			}

			resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
			if err != nil {
				t.Fatalf("GetConfiguration failed: %v", err)
			}

			if diff := cmp.Diff(tc.wantMap, resp.GetResolvedConfiguration().GetPdoMappings(), protocmp.Transform()); diff != "" {
				t.Errorf("Variable mappings diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestDeepNestingError(t *testing.T) {
	nestedEsi := `<EtherCATInfo Version="1.4"><InfoReference>deep.xml</InfoReference></EtherCATInfo>`
	mainEsi := `<EtherCATInfo><Vendor><Id>1</Id></Vendor><Descriptions><Devices><Device><Type ProductCode="#x00000002" RevisionNo="#x00000003">M</Type></Device></Devices></Descriptions><InfoReference>nested.xml</InfoReference></EtherCATInfo>`

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"main.xml":   {Data: mainEsi},
			"nested.xml": {Data: nestedEsi},
		},
	}

	ctx := context.Background()
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				"data://" + esiBundleDataAssetProtoName: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
					},
				},
			},
		},
	}

	fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
		{
			Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
			Data:     mustMarshalAny(bundle),
		},
	}))

	_, err := NewDeviceService(ctx, config, fakeDA.Client)
	if err == nil {
		t.Fatal("NewDeviceService succeeded, want error for deep nesting")
	}
	if !strings.Contains(err.Error(), "unsupported nested InfoReferences") {
		t.Errorf("Error %v does not contain expected message", err)
	}
}

func TestOpModeConfiguration(t *testing.T) {
	// ESI with DC support enabled (AssignActivate > 0).
	esiDc := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">TestDevice</Type>
        <Name>Test Device</Name>
        <Dc>
          <OpMode>
            <Name>DC Sync 1</Name>
            <Desc>DC Mode 1</Desc>
            <AssignActivate>#x0300</AssignActivate>
          </OpMode>
          <OpMode>
            <Name>SM Sync</Name>
            <Desc>SM Mode</Desc>
            <AssignActivate>#x0000</AssignActivate>
          </OpMode>
        </Dc>
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>`

	// ESI with multiple DC modes.
	esiMultiDc := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">TestDevice</Type>
        <Name>Multi DC Device</Name>
        <Dc>
          <OpMode>
            <Name>Free Run</Name>
            <Desc>Free Run Mode</Desc>
            <AssignActivate>0</AssignActivate>
          </OpMode>
          <OpMode>
            <Name>DC Mode A</Name>
            <Desc>Description A</Desc>
            <AssignActivate>#x0300</AssignActivate>
          </OpMode>
          <OpMode>
            <Name>DC Mode B</Name>
            <Desc>Description B</Desc>
            <AssignActivate>#x0700</AssignActivate>
          </OpMode>
        </Dc>
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>`

	// ESI without DC support (no OpModes or AssignActivate=0).
	esiNoDc := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">TestDevice</Type>
        <Name>Test Device No DC</Name>
        <Dc>
           <OpMode>
            <Name>SM Sync only</Name>
            <Desc>SM Mode</Desc>
            <AssignActivate>0</AssignActivate>
          </OpMode>
        </Dc>
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>`

	// ESI with absolutely no OpModes (legacy/simple device).
	esiEmpty := `<?xml version="1.0" encoding="utf-8"?>
<EtherCATInfo Version="1.4">
  <Vendor><Id>1</Id><Name>TestVendor</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x00000002" RevisionNo="#x00000003">TestDevice</Type>
        <Name>Simple Device</Name>
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>`

	tests := []struct {
		desc    string
		esiData string
		// Input config
		selectedOpMode string
		hasDs402       bool
		// Expectations
		wantSupported []*dspb.OpModeInfo
		wantActive    string
		wantErr       string
	}{
		{
			desc:           "DC supported, no selection, no DS402 -> Default to first available (DC Sync 1)",
			esiData:        esiDc,
			selectedOpMode: "",
			hasDs402:       false,
			wantSupported: []*dspb.OpModeInfo{
				{Name: "DC Sync 1", Description: "DC Mode 1", IsDc: true},
				{Name: "SM Sync", Description: "SM Mode", IsDc: false},
			},
			// Default logic: if no Ds402, pick first available.
			// In this case, "DC Sync 1" is first.
			wantActive: "DC Sync 1",
		},
		{
			desc:           "DC supported, no selection, HAS DS402 -> Default to first DC mode (DC Sync 1)",
			esiData:        esiDc,
			selectedOpMode: "",
			hasDs402:       true,
			wantSupported: []*dspb.OpModeInfo{
				{Name: "DC Sync 1", Description: "DC Mode 1", IsDc: true},
				{Name: "SM Sync", Description: "SM Mode", IsDc: false},
			},
			wantActive: "DC Sync 1",
		},
		{
			desc:           "Multi DC, no selection, HAS DS402 -> Prefer first DC mode (DC Mode A)",
			esiData:        esiMultiDc,
			selectedOpMode: "",
			hasDs402:       true,
			wantSupported: []*dspb.OpModeInfo{
				{Name: "Free Run", Description: "Free Run Mode", IsDc: false},
				{Name: "DC Mode A", Description: "Description A", IsDc: true},
				{Name: "DC Mode B", Description: "Description B", IsDc: true},
			},
			// Should skip "Free Run" and pick "DC Mode A" because it's the first DC mode.
			wantActive: "DC Mode A",
		},
		{
			desc:           "Multi DC, no selection, NO DS402 -> Prefer first available (Free Run)",
			esiData:        esiMultiDc,
			selectedOpMode: "",
			hasDs402:       false,
			wantSupported: []*dspb.OpModeInfo{
				{Name: "Free Run", Description: "Free Run Mode", IsDc: false},
				{Name: "DC Mode A", Description: "Description A", IsDc: true},
				{Name: "DC Mode B", Description: "Description B", IsDc: true},
			},
			// Should pick "Free Run".
			wantActive: "Free Run",
		},
		{
			desc:           "Explicit selection of SM Sync",
			esiData:        esiDc,
			selectedOpMode: "SM Sync",
			hasDs402:       true, // Presence of DS402 shouldn't override explicit selection
			wantSupported: []*dspb.OpModeInfo{
				{Name: "DC Sync 1", Description: "DC Mode 1", IsDc: true},
				{Name: "SM Sync", Description: "SM Mode", IsDc: false},
			},
			wantActive: "SM Sync",
		},
		{
			desc:           "Invalid selection",
			esiData:        esiDc,
			selectedOpMode: "NonExistentMode",
			wantErr:        "is not supported by this device",
		},
		{
			desc:           "No DC supported, auto selection",
			esiData:        esiNoDc,
			selectedOpMode: "",
			hasDs402:       true, // Even with DS402, if no DC mode exists, fall back to first available.
			wantSupported: []*dspb.OpModeInfo{
				{Name: "SM Sync only", Description: "SM Mode", IsDc: false},
			},
			wantActive: "SM Sync only",
		},
		{
			desc:           "Empty OpModes (Simple Device)",
			esiData:        esiEmpty,
			selectedOpMode: "",
			wantSupported:  nil, // No OpModes
			wantActive:     "",  // No effective OpMode
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()

			// Construct config with potential DS402 mapping
			mappings := []*dscpb.InterfaceMapping{}
			if tc.hasDs402 {
				mappings = append(mappings, &dscpb.InterfaceMapping{
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_Ds402DeviceData{
							Ds402DeviceData: &dscpb.Ds402DeviceData{}, // Empty but present
						},
					},
				})
			}

			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
				SyncConfig: &dscpb.SyncConfig{
					SelectedOpModeName: tc.selectedOpMode,
				},
				InterfaceMappings: mappings,
			}

			bundle := &esipb.EsiBundle{
				Files: map[string]*esipb.Esi{
					"device.xml": {Data: tc.esiData},
				},
			}

			fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
				{
					Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
					Data:     mustMarshalAny(bundle),
				},
			}))
			config.EsiBundle = &rdpb.ResolvedDependency{
				Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
					"data://" + esiBundleDataAssetProtoName: {
						Protocol: &rdpb.ResolvedDependency_Interface_Data_{
							Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
						},
					},
				},
			}

			service, err := NewDeviceService(ctx, config, fakeDA.Client)
			if err != nil {
				if tc.wantErr != "" {
					if !strings.Contains(err.Error(), tc.wantErr) {
						t.Fatalf("NewDeviceService error = %v, want error containing %q", err, tc.wantErr)
					}
					return
				}
				t.Fatalf("NewDeviceService failed: %v", err)
			}

			resp, err := service.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
			if tc.wantErr != "" {
				if err == nil {
					t.Fatalf("GetConfiguration matched no error, want error containing %q", tc.wantErr)
				}
				if !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("GetConfiguration error = %v, want error containing %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatalf("GetConfiguration failed: %v", err)
			}

			resolved := resp.GetResolvedConfiguration()
			if diff := cmp.Diff(tc.wantSupported, resolved.GetSupportedOpModes(), protocmp.Transform()); diff != "" {
				t.Errorf("SupportedOpModes diff (-want +got):\n%s", diff)
			}
			if diff := cmp.Diff(tc.wantActive, resolved.GetActiveOpModeName(), protocmp.Transform()); diff != "" {
				t.Errorf("ActiveOpModeName diff (-want +got):\n%s", diff)
			}
		})
	}
}

func TestSDOResolution(t *testing.T) {
	ctx := context.Background()
	vendorID, productCode, revision := 0x00000001, 0x00000002, 0x00000003

	// Setup ESI with a Dictionary
	esiObjects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x6040", ObjName: "Control word", ObjType: "UINT", ObjBitSize: 16, ObjAccess: "rw"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6041", ObjName: "Status word", ObjType: "UINT", ObjBitSize: 16, ObjAccess: "ro"}),
		makeEsiObjectXml(testEsiObject{Index: "#x6060", ObjName: "Modes of operation", ObjType: "SINT", ObjBitSize: 8, ObjAccess: "rw"}),
	}
	esiXML := makeTestEsiXml(nil, esiObjects, nil)
	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.esi": {Data: esiXML},
		},
	}

	bundleID := &ipb.Id{Name: "esi_sdo", Package: "test"}
	fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
		{
			Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
			Data:     mustMarshalAny(bundle),
		},
	}))

	tests := []struct {
		name       string
		mappings   []*dscpb.InterfaceMapping
		wantSdos   map[string]*dspb.ResolvedSdoVariable
		wantErrSub string
	}{
		{
			name: "Successful SDO reads and writes",
			mappings: []*dscpb.InterfaceMapping{
				{
					Interface: &dscpb.Interface{InterfaceName: "sdo_iface"},
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_SdoDeviceData{
							SdoDeviceData: &dscpb.ServiceVariableDeviceData{
								SdoWrites: []*dscpb.ServiceVariableWrite{
									{Object: "Control word", Value: &dscpb.BusVariableValue{Value: &dscpb.BusVariableValue_Uint16Value{Uint16Value: 0x0006}}},
								},
								SdoReads: []*dscpb.ServiceVariableRead{
									{Object: "Status word", Type: ecatpb.ServiceVariableType_UINT16_SDO_TYPE, Alias: "status"},
									{Object: "#x6060.0", Type: ecatpb.ServiceVariableType_INT8_SDO_TYPE, Alias: "mode"},
								},
							},
						},
					},
				},
			},
			wantSdos: map[string]*dspb.ResolvedSdoVariable{
				"Control word": {Index: 0x6040, SubIndex: 0},
				"Status word":  {Index: 0x6041, SubIndex: 0},
				"#x6060.0":     {Index: 0x6060, SubIndex: 0},
			},
		},
		{
			name: "Raw numeric address bypasses dictionary",
			mappings: []*dscpb.InterfaceMapping{
				{
					Interface: &dscpb.Interface{InterfaceName: "sdo_raw"},
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_SdoDeviceData{
							SdoDeviceData: &dscpb.ServiceVariableDeviceData{
								SdoReads: []*dscpb.ServiceVariableRead{
									{Object: "0x1234.5", Type: ecatpb.ServiceVariableType_UINT32_SDO_TYPE, Alias: "raw"},
								},
							},
						},
					},
				},
			},
			wantSdos: map[string]*dspb.ResolvedSdoVariable{
				"0x1234.5": {Index: 0x1234, SubIndex: 5},
			},
		},
		{
			name: "Type mismatch in SDO write",
			mappings: []*dscpb.InterfaceMapping{
				{
					Interface: &dscpb.Interface{InterfaceName: "sdo_fail_write"},
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_SdoDeviceData{
							SdoDeviceData: &dscpb.ServiceVariableDeviceData{
								SdoWrites: []*dscpb.ServiceVariableWrite{
									{Object: "Modes of operation", Value: &dscpb.BusVariableValue{Value: &dscpb.BusVariableValue_Uint16Value{Uint16Value: 123}}},
								},
							},
						},
					},
				},
			},
			wantErrSub: "type mismatch for SDO write \"Modes of operation\"",
		},
		{
			name: "Type mismatch in SDO read",
			mappings: []*dscpb.InterfaceMapping{
				{
					Interface: &dscpb.Interface{InterfaceName: "sdo_fail_read"},
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_SdoDeviceData{
							SdoDeviceData: &dscpb.ServiceVariableDeviceData{
								SdoReads: []*dscpb.ServiceVariableRead{
									{Object: "Control word", Type: ecatpb.ServiceVariableType_UINT8_SDO_TYPE, Alias: "bad_type"},
								},
							},
						},
					},
				},
			},
			wantErrSub: "type mismatch for SDO read \"Control word\"",
		},
		{
			name: "Unknown object name",
			mappings: []*dscpb.InterfaceMapping{
				{
					Interface: &dscpb.Interface{InterfaceName: "sdo_fail_name"},
					DeviceData: &dscpb.DeviceData{
						Data: &dscpb.DeviceData_SdoDeviceData{
							SdoDeviceData: &dscpb.ServiceVariableDeviceData{
								SdoReads: []*dscpb.ServiceVariableRead{
									{Object: "NonExistentObject", Type: ecatpb.ServiceVariableType_UINT16_SDO_TYPE},
								},
							},
						},
					},
				},
			},
			wantErrSub: "could not be resolved from ESI dictionary",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier: &dscpb.DeviceIdentifier{
					VendorId:    int32(vendorID),
					ProductCode: int32(productCode),
					Revision:    int32(revision),
				},
				EsiBundle: &rdpb.ResolvedDependency{
					Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
						"data://" + esiBundleDataAssetProtoName: {
							Protocol: &rdpb.ResolvedDependency_Interface_Data_{
								Data: &rdpb.ResolvedDependency_Interface_Data{Id: bundleID},
							},
						},
					},
				},
				InterfaceMappings: tt.mappings,
			}

			s, err := NewDeviceService(ctx, config, fakeDA.Client)
			if tt.wantErrSub != "" {
				if err == nil {
					t.Errorf("NewDeviceService() expected error containing %q, got nil", tt.wantErrSub)
				} else if !strings.Contains(err.Error(), tt.wantErrSub) {
					t.Errorf("NewDeviceService() error = %v, want substring %q", err, tt.wantErrSub)
				}
				return
			}

			if err != nil {
				t.Fatalf("NewDeviceService() failed: %v", err)
			}

			resp, err := s.GetConfiguration(ctx, &dspb.GetConfigurationRequest{})
			if err != nil {
				t.Fatalf("GetConfiguration() failed: %v", err)
			}

			gotSdos := resp.GetResolvedConfiguration().GetSdoMappings()
			if diff := cmp.Diff(tt.wantSdos, gotSdos, protocmp.Transform()); diff != "" {
				t.Errorf("SDO mappings mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestIntermediateBitLengths(t *testing.T) {
	objects := []string{
		makeEsiObjectXml(testEsiObject{Index: "#x2000", ObjName: "Uint24Var", ObjType: "UNSIGNED24", ObjBitSize: 24, ObjPdoMapping: "R"}),
		makeEsiObjectXml(testEsiObject{Index: "#x2001", ObjName: "Int48Var", ObjType: "INTEGER48", ObjBitSize: 48, ObjPdoMapping: "R"}),
	}
	pdos := []string{
		makeEsiPdoXml("Rx", "#x1600", "RxPDO", false, 0,
			testPdoEntry{Index: "#x2000", SubIndex: 0, BitLen: 24, EntryName: "Uint24Var"},
			testPdoEntry{Index: "#x2001", SubIndex: 0, BitLen: 48, EntryName: "Int48Var"}),
	}
	esiData := makeTestEsiXml(nil, objects, pdos)

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: esiData},
		},
	}

	tests := []struct {
		desc    string
		write   *dscpb.BusVariableWrite
		wantErr string
	}{
		{
			desc: "Uint24 Valid Value",
			write: &dscpb.BusVariableWrite{
				VariableReference: &dscpb.VariableReference{Pdo: "RxPDO", Object: "Uint24Var"},
				Value: &dscpb.BusVariableValue{
					Value: &dscpb.BusVariableValue_Uint32Value{Uint32Value: 0xFFFFFF},
				},
			},
		},
		{
			desc: "Uint24 Overflow",
			write: &dscpb.BusVariableWrite{
				VariableReference: &dscpb.VariableReference{Pdo: "RxPDO", Object: "Uint24Var"},
				Value: &dscpb.BusVariableValue{
					Value: &dscpb.BusVariableValue_Uint32Value{Uint32Value: 0x1000000},
				},
			},
			wantErr: "exceeds maximum for 24 bits",
		},
		{
			desc: "Int48 Valid Value",
			write: &dscpb.BusVariableWrite{
				VariableReference: &dscpb.VariableReference{Pdo: "RxPDO", Object: "Int48Var"},
				Value: &dscpb.BusVariableValue{
					Value: &dscpb.BusVariableValue_Int64Value{Int64Value: (int64(1) << 47) - 1},
				},
			},
		},
		{
			desc: "Int48 Overflow",
			write: &dscpb.BusVariableWrite{
				VariableReference: &dscpb.VariableReference{Pdo: "RxPDO", Object: "Int48Var"},
				Value: &dscpb.BusVariableValue{
					Value: &dscpb.BusVariableValue_Int64Value{Int64Value: (int64(1) << 47)},
				},
			},
			wantErr: "out of bounds for 48 signed bits",
		},
	}

	for _, tc := range tests {
		t.Run(tc.desc, func(t *testing.T) {
			ctx := context.Background()
			config := &dscpb.DeviceServiceConfig{
				DeviceIdentifier: &dscpb.DeviceIdentifier{VendorId: 1, ProductCode: 2, Revision: 3},
				InterfaceMappings: []*dscpb.InterfaceMapping{
					{
						DeviceData: &dscpb.DeviceData{
							Data: &dscpb.DeviceData_BusVariableDeviceData{
								BusVariableDeviceData: &dscpb.ProcessVariableDeviceData{
									BusVariableWrites: []*dscpb.BusVariableWrite{tc.write},
								},
							},
						},
					},
				},
			}

			fakeDA := fakedataassets.StartServer(ctx, t, fakedataassets.WithDataAssets([]*dapb.DataAsset{
				{
					Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: &ipb.Id{Name: "test_bundle"}}},
					Data:     mustMarshalAny(bundle),
				},
			}))
			config.EsiBundle = &rdpb.ResolvedDependency{
				Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
					"data://" + esiBundleDataAssetProtoName: {
						Protocol: &rdpb.ResolvedDependency_Interface_Data_{
							Data: &rdpb.ResolvedDependency_Interface_Data{Id: &ipb.Id{Name: "test_bundle"}},
						},
					},
				},
			}

			_, err := NewDeviceService(ctx, config, fakeDA.Client)
			if tc.wantErr != "" {
				if err == nil {
					t.Fatalf("Expected error containing %q, got nil", tc.wantErr)
				}
				if !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("Expected error containing %q, got %v", tc.wantErr, err)
				}
				return
			}
			if err != nil {
				t.Fatalf("Unexpected error: %v", err)
			}
		})
	}
}

type fakeRetryDataAssetsServer struct {
	dagrpcpb.UnimplementedDataAssetsServer
	mu        sync.Mutex
	calls     int
	failUntil int
	failCode  codes.Code
	dataAsset *dapb.DataAsset
}

func (s *fakeRetryDataAssetsServer) GetDataAsset(ctx context.Context, req *dagrpcpb.GetDataAssetRequest) (*dapb.DataAsset, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	idx := s.calls
	s.calls++
	if idx < s.failUntil {
		code := s.failCode
		if code == codes.OK {
			code = codes.Unavailable
		}
		return nil, status.Error(code, "test transient error")
	}
	if s.dataAsset != nil {
		return s.dataAsset, nil
	}
	return nil, status.Error(codes.NotFound, "data asset not found")
}

func (s *fakeRetryDataAssetsServer) callCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.calls
}

func startRetryTestServer(t *testing.T, srv dagrpcpb.DataAssetsServer) string {
	t.Helper()
	server := grpc.NewServer()
	dagrpcpb.RegisterDataAssetsServer(server, srv)
	return grpctest.StartServerT(t, server)
}

func TestFetchESIBundle_NativeGrpcRetry_Success(t *testing.T) {
	ctx := context.Background()
	bundleID := &ipb.Id{
		Package: "intrinsic_proto.fieldbus.ethercat.test",
		Name:    "test_bundle_retry",
	}
	iface := "data://" + esiBundleDataAssetProtoName
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{
			VendorId:    0x0001,
			ProductCode: 0x0002,
			Revision:    0x0003,
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				iface: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{
							Id: bundleID,
						},
					},
				},
			},
		},
	}

	bundle := &esipb.EsiBundle{
		Files: map[string]*esipb.Esi{
			"device.xml": {Data: makeTestEsiXml(nil, nil, nil)},
		},
	}
	bundleAny, err := anypb.New(bundle)
	if err != nil {
		t.Fatalf("Failed to marshal bundle: %v", err)
	}
	validDataAsset := &dapb.DataAsset{
		Metadata: &mpb.Metadata{IdVersion: &ipb.IdVersion{Id: bundleID}},
		Data:     bundleAny,
	}

	// Fail attempt 1 with `Unavailable`, succeed on attempt 2
	fakeServer := &fakeRetryDataAssetsServer{
		failUntil: 1,
		failCode:  codes.Unavailable,
		dataAsset: validDataAsset,
	}
	srvAddr := startRetryTestServer(t, fakeServer)

	opts := append(
		DialOptions(),
		grpc.WithTransportCredentials(local.NewCredentials()),
	)
	conn, err := grpc.NewClient(srvAddr, opts...)
	if err != nil {
		t.Fatalf("grpc.NewClient failed: %v", err)
	}
	t.Cleanup(func() { conn.Close() })
	daClient := dagrpcpb.NewDataAssetsClient(conn)

	svc, err := NewDeviceService(ctx, config, daClient)
	if err != nil {
		t.Fatalf("NewDeviceService failed unexpectedly: %v", err)
	}
	if svc == nil {
		t.Fatalf("Expected non-nil DeviceService")
	}
	if got := fakeServer.callCount(); got != 2 {
		t.Errorf("Expected exactly 2 calls to GetDataAsset (1 failure + 1 success retry), got %d", got)
	}
}

func TestFetchESIBundle_NativeGrpcRetry_NonRetryable(t *testing.T) {
	ctx := context.Background()
	bundleID := &ipb.Id{
		Package: "intrinsic_proto.fieldbus.ethercat.test",
		Name:    "test_bundle_non_retryable",
	}
	iface := "data://" + esiBundleDataAssetProtoName
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{
			VendorId:    0x0001,
			ProductCode: 0x0002,
			Revision:    0x0003,
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				iface: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{
							Id: bundleID,
						},
					},
				},
			},
		},
	}

	// Return `NotFound` (non-retryable)
	fakeServer := &fakeRetryDataAssetsServer{
		failUntil: 1,
		failCode:  codes.NotFound,
	}
	srvAddr := startRetryTestServer(t, fakeServer)

	opts := append(
		DialOptions(),
		grpc.WithTransportCredentials(local.NewCredentials()),
	)
	conn, err := grpc.NewClient(srvAddr, opts...)
	if err != nil {
		t.Fatalf("grpc.NewClient failed: %v", err)
	}
	t.Cleanup(func() { conn.Close() })
	daClient := dagrpcpb.NewDataAssetsClient(conn)

	svc, err := NewDeviceService(ctx, config, daClient)
	if err == nil {
		t.Fatalf("Expected error from NewDeviceService, got nil")
	}
	if svc != nil {
		t.Fatalf("Expected nil DeviceService on failure, got %v", svc)
	}
	if !errors.Is(err, ErrEsiBundleNotFound) {
		t.Errorf("Expected error to wrap ErrEsiBundleNotFound, got: %v", err)
	}
	if status.Code(err) != codes.NotFound {
		t.Errorf("Expected status code codes.NotFound, got: %v (code: %v)", err, status.Code(err))
	}
	if got := fakeServer.callCount(); got != 1 {
		t.Errorf("Expected exactly 1 call (no retries for NotFound), got %d", got)
	}
}

func TestFetchESIBundle_NativeGrpcRetry_Exhausted(t *testing.T) {
	ctx := context.Background()
	bundleID := &ipb.Id{
		Package: "intrinsic_proto.fieldbus.ethercat.test",
		Name:    "test_bundle_exhaust",
	}
	iface := "data://" + esiBundleDataAssetProtoName
	config := &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{
			VendorId:    0x0001,
			ProductCode: 0x0002,
			Revision:    0x0003,
		},
		EsiBundle: &rdpb.ResolvedDependency{
			Interfaces: map[string]*rdpb.ResolvedDependency_Interface{
				iface: {
					Protocol: &rdpb.ResolvedDependency_Interface_Data_{
						Data: &rdpb.ResolvedDependency_Interface_Data{
							Id: bundleID,
						},
					},
				},
			},
		},
	}

	// Always fail with `Unavailable`
	fakeServer := &fakeRetryDataAssetsServer{
		failUntil: 100,
		failCode:  codes.Unavailable,
	}
	srvAddr := startRetryTestServer(t, fakeServer)

	// Use a test service config with 5 attempts and fast backoff (10ms) to test exhaustion without slow sleep.
	fastRetryConfig := fmt.Sprintf(`{
		"methodConfig": [{
			"name": [{"service": "%s"}],
			"retryPolicy": {
				"maxAttempts": 5,
				"initialBackoff": "0.01s",
				"maxBackoff": "0.02s",
				"backoffMultiplier": 1.5,
				"retryableStatusCodes": ["UNAVAILABLE", "RESOURCE_EXHAUSTED"]
			}
		}]
	}`, dagrpcpb.DataAssets_ServiceDesc.ServiceName)
	conn, err := grpc.NewClient(
		srvAddr,
		grpc.WithDefaultServiceConfig(fastRetryConfig),
		grpc.WithTransportCredentials(local.NewCredentials()),
	)
	if err != nil {
		t.Fatalf("grpc.NewClient failed: %v", err)
	}
	t.Cleanup(func() { conn.Close() })
	daClient := dagrpcpb.NewDataAssetsClient(conn)

	svc, err := NewDeviceService(ctx, config, daClient)
	if err == nil {
		t.Fatalf("Expected error from NewDeviceService, got nil")
	}
	if svc != nil {
		t.Fatalf("Expected nil DeviceService on failure, got %v", svc)
	}
	if !errors.Is(err, ErrEsiBundleNotFound) {
		t.Errorf("Expected error to wrap ErrEsiBundleNotFound, got: %v", err)
	}
	if status.Code(err) != codes.Unavailable {
		t.Errorf("Expected status code codes.Unavailable, got: %v (code: %v)", err, status.Code(err))
	}
	if got := fakeServer.callCount(); got != 5 {
		t.Errorf("Expected exactly 5 calls to GetDataAsset (all attempts exhausted), got %d", got)
	}
}
