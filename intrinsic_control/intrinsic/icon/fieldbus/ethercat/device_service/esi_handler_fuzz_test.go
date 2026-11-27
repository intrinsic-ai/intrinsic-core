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
	"testing"

	dscpb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_config_go_proto"
	esipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/esi_go_proto"
	ecatpb "intrinsic/icon/hal/lib/service_variable_value/v1/service_variable_config_go_proto"
)

func seedBasicEsiXML() string {
	return makeTestEsiXml(
		nil,
		[]string{
			makeEsiObjectXml(testEsiObject{Index: "#x6041", ObjName: "Status word", ObjType: "UINT", ObjBitSize: 16, ObjPdoMapping: "T"}),
			makeEsiObjectXml(testEsiObject{Index: "#x6040", ObjName: "Control word", ObjType: "UINT", ObjBitSize: 16, ObjPdoMapping: "R"}),
			makeEsiObjectXml(testEsiObject{Index: "#x6064", ObjName: "Position actual value", ObjType: "DINT", ObjBitSize: 32, ObjPdoMapping: "T"}),
			makeEsiObjectXml(testEsiObject{Index: "#x607A", ObjName: "Target position", ObjType: "DINT", ObjBitSize: 32, ObjPdoMapping: "R"}),
		},
		[]string{
			makeEsiPdoXml("Tx", "#x1A00", "TxPdo1", true, 3,
				testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word"},
				testPdoEntry{Index: "#x6064", SubIndex: 0, BitLen: 32, EntryName: "Position actual value"},
			),
			makeEsiPdoXml("Rx", "#x1600", "RxPdo1", true, 2,
				testPdoEntry{Index: "#x6040", SubIndex: 0, BitLen: 16, EntryName: "Control word"},
				testPdoEntry{Index: "#x607A", SubIndex: 0, BitLen: 32, EntryName: "Target position"},
			),
		},
	)
}

func seedPaddingEsiXML() string {
	return makeTestEsiXml(
		nil,
		[]string{
			makeEsiObjectXml(testEsiObject{Index: "#x6041", ObjName: "Status word", ObjType: "UINT", ObjBitSize: 16, ObjPdoMapping: "T"}),
		},
		[]string{
			makeEsiPdoXml("Tx", "#x1A00", "TxPdo1", true, 3,
				testPdoEntry{Index: "#x0000", SubIndex: 0, BitLen: 16, EntryName: "Padding"},
				testPdoEntry{Index: "#x6041", SubIndex: 0, BitLen: 16, EntryName: "Status word"},
			),
		},
	)
}

func FuzzIndexIsConsistent(f *testing.F) {
	f.Add([]byte(seedBasicEsiXML()), uint32(1), uint32(2), uint32(3))
	f.Add([]byte(seedPaddingEsiXML()), uint32(1), uint32(2), uint32(3))
	f.Add([]byte("<EtherCATInfo/>"), uint32(1), uint32(2), uint32(3))
	f.Add([]byte{}, uint32(0), uint32(0), uint32(0))

	f.Fuzz(func(t *testing.T, esiXML []byte, vendorID, productCode, revision uint32) {
		if len(esiXML) > 32768 {
			t.Skip()
		}
		bundle := &esipb.EsiBundle{
			Files: map[string]*esipb.Esi{"device.xml": {Data: string(esiXML)}},
		}
		s := &DeviceService{esiBundle: bundle}
		if err := s.loadAndIndexESI(context.Background(), bundle, vendorID, productCode, revision); err != nil {
			return
		}
		for name, addrs := range s.objectNameIndex {
			for _, addr := range addrs {
				if _, ok := s.objectIndex[addr]; !ok {
					t.Fatalf("objectNameIndex[%q] references missing object %+v", name, addr)
				}
			}
		}
		for pdoIdx, meta := range s.pdoIndex {
			if meta == nil {
				t.Fatalf("pdoIndex[#x%X] is nil", pdoIdx)
			}
			if meta.Index != pdoIdx {
				t.Fatalf("pdoIndex[#x%X].Index = #x%X", pdoIdx, meta.Index)
			}
			for entryName, addr := range meta.NameToAddr {
				if _, ok := s.objectIndex[addr]; !ok {
					t.Fatalf("pdo #x%X NameToAddr[%q] references missing object %+v", pdoIdx, entryName, addr)
				}
			}
		}
	})
}

func buildSingleMappingConfig(pdo, object string, kind uint8, raw uint64) *dscpb.DeviceServiceConfig {
	ref := &dscpb.VariableReference{Pdo: pdo, Object: object}
	var dd *dscpb.DeviceData
	switch kind % 7 {
	case 0:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_Ds402DeviceData{
				Ds402DeviceData: &dscpb.Ds402DeviceData{
					StatusWordReference:  ref,
					ControlWordReference: &dscpb.VariableReference{Object: "Control word"},
				},
			},
		}
	case 1:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_JointDeviceData{
				JointDeviceData: &dscpb.JointDeviceData{
					JointPositionCommandReference: &dscpb.VariableReference{Object: "Target position"},
					JointPositionStateReference:   ref,
				},
			},
		}
	case 2:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_AdioDeviceData{
				AdioDeviceData: &dscpb.AdioDeviceData{
					AdioVariable: &dscpb.AdioDeviceData_DigitalInput{
						DigitalInput: &dscpb.DigitalVariableReference{VariableReference: ref},
					},
				},
			},
		}
	case 3:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_ForceTorqueDeviceData{
				ForceTorqueDeviceData: &dscpb.ForceTorqueDeviceData{
					ForceX: ref,
				},
			},
		}
	case 4:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_BusVariableDeviceData{
				BusVariableDeviceData: &dscpb.ProcessVariableDeviceData{
					BusVariableWrites: []*dscpb.BusVariableWrite{
						{
							VariableReference: ref,
							Value: &dscpb.BusVariableValue{
								Value: &dscpb.BusVariableValue_Uint32Value{Uint32Value: uint32(raw)},
							},
						},
					},
				},
			},
		}
	case 5:
		// BusVariableWrite with nil VariableReference.
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_BusVariableDeviceData{
				BusVariableDeviceData: &dscpb.ProcessVariableDeviceData{
					BusVariableWrites: []*dscpb.BusVariableWrite{
						{
							Value: &dscpb.BusVariableValue{
								Value: &dscpb.BusVariableValue_Uint32Value{Uint32Value: uint32(raw)},
							},
						},
					},
				},
			},
		}
	default:
		dd = &dscpb.DeviceData{
			Data: &dscpb.DeviceData_SdoDeviceData{
				SdoDeviceData: &dscpb.ServiceVariableDeviceData{
					SdoReads: []*dscpb.ServiceVariableRead{
						{
							Object: object,
							Type:   ecatpb.ServiceVariableType_UINT16_SDO_TYPE,
						},
					},
				},
			},
		}
	}
	return &dscpb.DeviceServiceConfig{
		DeviceIdentifier: &dscpb.DeviceIdentifier{
			VendorId:    1,
			ProductCode: 2,
			Revision:    3,
		},
		InterfaceMappings: []*dscpb.InterfaceMapping{
			{DeviceData: dd},
		},
	}
}

func FuzzResolveConfigurationNeverPanics(f *testing.F) {
	f.Add([]byte(seedBasicEsiXML()), "", "Status word", uint8(0), uint64(0))
	f.Add([]byte(seedBasicEsiXML()), "#x1A00", "0x6064.0", uint8(1), uint64(0))
	f.Add([]byte(seedBasicEsiXML()), "", "name:Status word", uint8(2), uint64(0))
	f.Add([]byte(seedPaddingEsiXML()), "", "Padding", uint8(2), uint64(0))
	f.Add([]byte(seedBasicEsiXML()), "", "0x6041.0", uint8(5), uint64(1))
	f.Add([]byte(seedBasicEsiXML()), "", "0x6041.0", uint8(6), uint64(1))

	f.Fuzz(func(t *testing.T, esiXML []byte, pdo, object string, kind uint8, raw uint64) {
		if len(esiXML) > 32768 || len(pdo) > 256 || len(object) > 256 {
			t.Skip()
		}
		bundle := &esipb.EsiBundle{
			Files: map[string]*esipb.Esi{"device.xml": {Data: string(esiXML)}},
		}
		cfg := buildSingleMappingConfig(pdo, object, kind, raw)
		s := &DeviceService{config: cfg, esiBundle: bundle}
		ctx := context.Background()
		if err := s.loadAndIndexESI(ctx, bundle, 1, 2, 3); err != nil {
			return
		}
		if err := s.computeResolvedConfiguration(ctx); err != nil {
			return
		}
		if s.resolvedConfiguration == nil {
			t.Fatalf("computeResolvedConfiguration succeeded with nil resolvedConfiguration")
		}
		for key, v := range s.resolvedConfiguration.GetPdoMappings() {
			if _, ok := s.pdoIndex[v.GetPdoIndex()]; !ok {
				t.Fatalf("PdoMappings[%q] references unknown PDO #x%X", key, v.GetPdoIndex())
			}
			addr := objectAddress{Index: v.GetIndex(), SubIndex: v.GetSubIndex()}
			if _, ok := s.objectIndex[addr]; !ok {
				t.Fatalf("PdoMappings[%q] references unknown object %+v", key, addr)
			}
		}
	})
}
