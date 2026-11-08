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

// Package deviceservice implements a DeviceService rpc service that resolves logical EtherCAT
// variable references into concrete hardware addresses and EBI instructions.
package deviceservice

import (
	"context"
	"errors"
	"fmt"
	"path"
	"regexp"
	"strings"

	"intrinsic/assets/dependencies/utils"

	log "github.com/golang/glog"
	"google.golang.org/grpc"

	dagrpcpb "intrinsic/assets/data/proto/v1/data_assets_go_proto"
	dscpb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_config_go_proto"
	dspb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_go_proto"
	esipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/esi_go_proto"
	ecatpb "intrinsic/icon/hal/lib/service_variable_value/v1/service_variable_config_go_proto"
)

var (
	// ErrConfigNil indicates that the user-provided configuration is nil.
	ErrConfigNil = errors.New("config must not be nil")
	// ErrNoDeviceIdentifier indicates that the configuration is missing the mandatory vendor/product ID.
	ErrNoDeviceIdentifier = errors.New("config must have a device identifier")
	// ErrEsiBundleNotFound indicates the requested ESI bundle data asset could not be retrieved.
	ErrEsiBundleNotFound = errors.New("ESI data asset(s) not found")
	// ErrEsiUnmarshal indicates that the ESI XML data is malformed or incompatible.
	ErrEsiUnmarshal = errors.New("unmarshalling ESI data asset")
	// ErrEsiInvalidPath indicates that an ESI bundle contains invalid paths that might violate security constraints.
	ErrEsiInvalidPath = errors.New("ESI bundle contains invalid path")
	// ErrEsiDeepNesting indicates that an ESI file contains nested InfoReferences beyond the supported depth.
	ErrEsiDeepNesting = errors.New("ESI bundle contains unsupported nested InfoReferences")

	esiBundleMsg                esipb.EsiBundle
	esiBundleDataAssetProtoName = string(esiBundleMsg.ProtoReflect().Descriptor().FullName())

	addressPattern = regexp.MustCompile(`^(?i)(?:0x|#x)?([0-9a-f]+)\.(?:0x|#x)?([0-9a-f]+)$`)
)

const (
	// variableReferenceSeparator is the string used to concatenate PDO and Object
	// references for use as unique keys in mapping tables.
	variableReferenceSeparator = "::"

	// `defaultServiceConfigTemplate` defines the gRPC service config template for resilient `DataAssets` calls.
	// It retries on `UNAVAILABLE` and `RESOURCE_EXHAUSTED` for ~8 seconds total with exponential backoff and jitter.
	defaultServiceConfigTemplate = `{
		"methodConfig": [{
			"name": [{"service": "%s"}],
			"retryPolicy": {
				"maxAttempts": 5,
				"initialBackoff": "1s",
				"maxBackoff": "3s",
				"backoffMultiplier": 1.5,
				"retryableStatusCodes": ["UNAVAILABLE", "RESOURCE_EXHAUSTED"]
			}
		}]
	}`
)

// DialOptions returns the recommended gRPC dial options for client connections used by DeviceService,
// enabling native client retry behavior.
func DialOptions() []grpc.DialOption {
	serviceConfig := fmt.Sprintf(defaultServiceConfigTemplate, dagrpcpb.DataAssets_ServiceDesc.ServiceName)
	return []grpc.DialOption{
		grpc.WithDefaultServiceConfig(serviceConfig),
	}
}

// DeviceService implements the DeviceService rpc service.
// It maintains indices derived from the ESI to perform rapid variable resolution.
type DeviceService struct {
	// config holds the user-provided service configuration.
	config *dscpb.DeviceServiceConfig
	// esiBundle contains the ESI bundle loaded as a data asset.
	esiBundle *esipb.EsiBundle

	// objectIndex maps object addresses (Index.SubIndex) to their metadata.
	objectIndex map[objectAddress]*objectMetadata
	// objectNameIndex maps localized name strings to one or more object addresses.
	objectNameIndex map[string][]objectAddress
	// pdoIndex maps PDO indices to their metadata.
	pdoIndex map[uint32]*pdoMetadata

	// activePdos tracks PDOs used in the current resolution request.
	activePdos map[uint32]bool
	// exclusionsToAdd tracks default-active PDOs that must be disabled due to exclusions.
	exclusionsToAdd []uint32
	// exclusionsToRemove tracks default-inactive PDOs that must be enabled.
	exclusionsToRemove []uint32
	// objectsToAdd tracks variables that must be appended to a dynamic PDO mapping.
	objectsToAdd []*dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition
	// pdoMappings stores the final mapping of "pdo::object" to resolution metadata.
	pdoMappings map[string]*dspb.ResolvedPdoVariable
	// sdoMappings stores the final mapping of SDO "object" strings to resolution metadata.
	sdoMappings map[string]*dspb.ResolvedSdoVariable

	// supportedOpModes lists the synchronization modes supported by the device (from ESI).
	supportedOpModes []*dspb.OpModeInfo

	// resolvedConfiguration holds the pre-computed configuration response data.
	resolvedConfiguration *dspb.ResolvedConfiguration
}

// `fetchESIBundle` retrieves the ESI bundle data asset from the `DataAsset` service with exponential retry backoff.
//
// Parameters:
//   - ctx: The context for the RPC call.
//   - config: The config containing the EsiBundle dependency.
//   - daClient: The client used to fetch the asset data.
//
// Returns:
//   - The populated EsiBundle proto.
//   - An error if the asset is missing, unmarshalling fails, or paths are invalid.
func fetchESIBundle(ctx context.Context, config *dscpb.DeviceServiceConfig, daClient dagrpcpb.DataAssetsClient) (*esipb.EsiBundle, error) {
	iface := "data://" + esiBundleDataAssetProtoName
	anyProto, err := utils.GetDataPayload(ctx, config.GetEsiBundle(), iface, utils.WithDataAssetsClient(daClient))
	if err != nil {
		return nil, fmt.Errorf("get ESI bundle data asset for interface %q failed: %w: %w", iface, ErrEsiBundleNotFound, err)
	}

	esiBundle := &esipb.EsiBundle{}
	if err := anyProto.UnmarshalTo(esiBundle); err != nil {
		return nil, fmt.Errorf("%w: %w", ErrEsiUnmarshal, err)
	}

	if len(esiBundle.GetFiles()) == 0 {
		return nil, fmt.Errorf("ESI bundle has no files: %w", ErrEsiUnmarshal)
	}
	for p := range esiBundle.GetFiles() {
		if path.IsAbs(p) {
			return nil, fmt.Errorf("ESI bundle contains absolute path %q: %w", p, ErrEsiInvalidPath)
		}
		cleaned := path.Clean(p)
		if cleaned == ".." || strings.HasPrefix(cleaned, "../") {
			return nil, fmt.Errorf("ESI bundle contains path %q resolving to outside bundle: %w", p, ErrEsiInvalidPath)
		}
	}

	return esiBundle, nil
}

// NewDeviceService constructs a new service instance and performs ESI indexing.
//
// Parameters:
//   - ctx: Request context.
//   - config: Service configuration containing device IDs and bundle references.
//   - daClient: Client for the Data Asset service.
//
// Returns:
//   - An initialized DeviceService ready to process GetConfiguration calls.
//   - An error if the config is invalid, indexing fails, or the device isn't found in the ESI.
func NewDeviceService(ctx context.Context, config *dscpb.DeviceServiceConfig, daClient dagrpcpb.DataAssetsClient) (*DeviceService, error) {
	if config == nil {
		return nil, ErrConfigNil
	}
	if config.GetDeviceIdentifier() == nil {
		return nil, ErrNoDeviceIdentifier
	}
	esiBundle, err := fetchESIBundle(ctx, config, daClient)
	if err != nil {
		return nil, fmt.Errorf("failed to fetch ESI bundle: %w", err)
	}

	s := &DeviceService{config: config, esiBundle: esiBundle}

	di := config.GetDeviceIdentifier()
	if err := s.loadAndIndexESI(ctx, esiBundle, uint32(di.VendorId), uint32(di.ProductCode), uint32(di.Revision)); err != nil {
		return nil, fmt.Errorf("failed to index ESI: %w", err)
	}

	if err := s.computeResolvedConfiguration(ctx); err != nil {
		return nil, fmt.Errorf("failed to resolve configuration: %w", err)
	}

	log.InfoContextf(ctx, "Device service started with config: %v", config)
	return s, nil
}

// GetConfiguration returns the configuration including the resolved VariableReferences, EBI instructions and metadata.
//
// Parameters:
//   - ctx: Request context.
//   - req: The GetConfigurationRequest (currently empty).
//
// Returns:
//   - A response containing the ESI bundle and the fully resolved ResolvedConfiguration.
//   - An error if resolution fails, a variable cannot be found, or PDO conflicts are detected.
func (s *DeviceService) GetConfiguration(ctx context.Context, req *dspb.GetConfigurationRequest) (*dspb.GetConfigurationResponse, error) {
	if s.esiBundle == nil {
		return nil, fmt.Errorf("no ESI bundle loaded")
	}

	return &dspb.GetConfigurationResponse{
		DeviceServiceConfig:   s.config,
		EsiBundle:             s.esiBundle,
		ResolvedConfiguration: s.resolvedConfiguration,
	}, nil
}

// computeResolvedConfiguration resolves all VariableReferences and detects ESI-related errors at startup.
func (s *DeviceService) computeResolvedConfiguration(ctx context.Context) error {
	// Initialize/Reset internal state.
	s.activePdos = make(map[uint32]bool)
	s.exclusionsToAdd = nil
	s.exclusionsToRemove = nil
	s.objectsToAdd = nil
	s.pdoMappings = make(map[string]*dspb.ResolvedPdoVariable)
	s.sdoMappings = make(map[string]*dspb.ResolvedSdoVariable)

	// Resolve all mapped interfaces.
	for _, im := range s.config.GetInterfaceMappings() {
		if ds402 := im.GetDeviceData().GetDs402DeviceData(); ds402 != nil {
			if err := s.resolveDs402Device(ctx, ds402); err != nil {
				return err
			}
		}

		if joint := im.GetDeviceData().GetJointDeviceData(); joint != nil {
			if err := s.resolveJointDevice(ctx, joint); err != nil {
				return err
			}
		}

		if adio := im.GetDeviceData().GetAdioDeviceData(); adio != nil {
			if err := s.resolveAdioDevice(ctx, adio); err != nil {
				return err
			}
		}

		if ft := im.GetDeviceData().GetForceTorqueDeviceData(); ft != nil {
			if err := s.resolveForceTorqueDevice(ctx, ft); err != nil {
				return err
			}
		}

		if busVar := im.GetDeviceData().GetBusVariableDeviceData(); busVar != nil {
			if err := s.resolveBusVariableWrite(ctx, busVar); err != nil {
				return err
			}
		}

		if sdo_data := im.GetDeviceData().GetSdoDeviceData(); sdo_data != nil {
			if err := s.resolveSdoDeviceData(ctx, sdo_data); err != nil {
				return err
			}
		}

		// Handle other DeviceData types here in the future
	}

	// Validate that no mutually exclusive PDOs were activated across different variables.
	for pdoIdx := range s.activePdos {
		meta, ok := s.pdoIndex[pdoIdx]
		if !ok {
			continue
		}
		for _, excludeIdx := range meta.Excludes {
			if s.activePdos[excludeIdx] {
				otherMeta := s.pdoIndex[excludeIdx]
				return fmt.Errorf("PDO conflict detected: %q (#x%04X) and %q (#x%04X) are mutually exclusive", meta.Name, pdoIdx, otherMeta.Name, excludeIdx)
			}
		}
	}

	var instructions *dspb.ResolvedConfiguration_EbiPdoInstructions
	if len(s.exclusionsToAdd) > 0 || len(s.exclusionsToRemove) > 0 || len(s.objectsToAdd) > 0 {
		instructions = &dspb.ResolvedConfiguration_EbiPdoInstructions{
			PdoExclusionsToAdd:    s.exclusionsToAdd,
			PdoExclusionsToRemove: s.exclusionsToRemove,
			ObjectsToAdd:          s.objectsToAdd,
		}
	}

	// Determine active OpMode.
	activeOpMode, err := s.resolveActiveOpMode(ctx)
	if err != nil {
		return err
	}

	s.resolvedConfiguration = &dspb.ResolvedConfiguration{
		EbiPdoInstructions: instructions,
		PdoMappings:        s.pdoMappings,
		SdoMappings:        s.sdoMappings,
		SupportedOpModes:   s.supportedOpModes,
		ActiveOpModeName:   activeOpMode,
	}

	return nil
}

// resolveDs402Device resolves all variables required for a DS402 drive.
func (s *DeviceService) resolveDs402Device(ctx context.Context, ds402 *dscpb.Ds402DeviceData) error {
	if err := s.resolveVariable(ctx, ds402.GetStatusWordReference(), PdoDirectionTx); err != nil {
		return err
	}
	if err := s.resolveVariable(ctx, ds402.GetErrorCodeReference(), PdoDirectionTx); err != nil {
		return err
	}
	if err := s.resolveVariable(ctx, ds402.GetControlWordReference(), PdoDirectionRx); err != nil {
		return err
	}
	if ds402.GetDigitalOutputsReference() != nil {
		if err := s.resolveVariable(ctx, ds402.GetDigitalOutputsReference(), PdoDirectionRx); err != nil {
			return err
		}
	}

	// Handle Homing configuration.
	if homing := ds402.GetHomingReference(); homing != nil {
		switch config := homing.GetConfiguration().(type) {
		case *dscpb.HomingReference_Disabled_:
			// Do nothing.
		case *dscpb.HomingReference_Auto_:
			return fmt.Errorf("homing 'auto' configuration is not implemented yet")
		case *dscpb.HomingReference_Manual_:
			m := config.Manual
			// Homing parameters are typically set via SDO/Rx during init, but some
			// can be mapped to PDOs. We treat them as Rx (Commands) or Tx (Status)
			// based on their DS402 nature.
			homingVars := []struct {
				ref *dscpb.VariableReference
				dir PdoDirection
			}{
				{m.GetHomingMethod(), PdoDirectionRx},
				{m.GetHomeOffset(), PdoDirectionRx},
				{m.GetHomingSpeed(), PdoDirectionRx},
				{m.GetHomingCreepSpeed(), PdoDirectionRx},
				{m.GetHomingAcceleration(), PdoDirectionRx},
				{m.GetModesOfOperation(), PdoDirectionRx},
				{m.GetModesOfOperationDisplay(), PdoDirectionTx},
			}
			for _, v := range homingVars {
				if v.ref != nil {
					if err := s.resolveVariable(ctx, v.ref, v.dir); err != nil {
						return err
					}
				}
			}
		}
	}
	return nil
}

// resolveJointDevice resolves all variables required for a standard joint device.
func (s *DeviceService) resolveJointDevice(ctx context.Context, joint *dscpb.JointDeviceData) error {
	if err := s.resolveVariable(ctx, joint.GetJointPositionCommandReference(), PdoDirectionRx); err != nil {
		return err
	}
	if err := s.resolveVariable(ctx, joint.GetJointPositionStateReference(), PdoDirectionTx); err != nil {
		return err
	}
	if joint.GetJointVelocityStateReference() != nil {
		if err := s.resolveVariable(ctx, joint.GetJointVelocityStateReference(), PdoDirectionTx); err != nil {
			return err
		}
	}
	if joint.GetJointAccelerationStateReference() != nil {
		if err := s.resolveVariable(ctx, joint.GetJointAccelerationStateReference(), PdoDirectionTx); err != nil {
			return err
		}
	}
	return nil
}

// resolveAdioDevice resolves all variables required for an ADIO device.
// Silently ignores when AdioDeviceData is empty (i.e. no actual ADIO variable has been specified).
func (s *DeviceService) resolveAdioDevice(ctx context.Context, adio *dscpb.AdioDeviceData) error {
	switch v := adio.GetAdioVariable().(type) {
	case *dscpb.AdioDeviceData_DigitalInput:
		if err := s.resolveVariable(ctx, v.DigitalInput.GetVariableReference(), PdoDirectionTx); err != nil {
			return err
		}
	case *dscpb.AdioDeviceData_DigitalOutput:
		if err := s.resolveVariable(ctx, v.DigitalOutput.GetVariableReference(), PdoDirectionRx); err != nil {
			return err
		}
	case *dscpb.AdioDeviceData_AnalogInputs:
		for _, ref := range v.AnalogInputs.GetNamedVariableReferences() {
			if err := s.resolveVariable(ctx, ref, PdoDirectionTx); err != nil {
				return err
			}
		}
	case *dscpb.AdioDeviceData_AnalogOutputs:
		for _, ref := range v.AnalogOutputs.GetNamedVariableReferences() {
			if err := s.resolveVariable(ctx, ref, PdoDirectionRx); err != nil {
				return err
			}
		}
	}
	return nil
}

// resolveForceTorqueDevice resolves all variables required for a force torque sensor.
func (s *DeviceService) resolveForceTorqueDevice(ctx context.Context, ft *dscpb.ForceTorqueDeviceData) error {
	ftVars := []struct {
		ref *dscpb.VariableReference
		dir PdoDirection
	}{
		{ft.GetForceX(), PdoDirectionTx},
		{ft.GetForceY(), PdoDirectionTx},
		{ft.GetForceZ(), PdoDirectionTx},
		{ft.GetTorqueX(), PdoDirectionTx},
		{ft.GetTorqueY(), PdoDirectionTx},
		{ft.GetTorqueZ(), PdoDirectionTx},
		{ft.GetStatusCode(), PdoDirectionTx},
		{ft.GetSampleCounter(), PdoDirectionTx},
		{ft.GetControlCode(), PdoDirectionRx},
	}
	for _, v := range ftVars {
		if v.ref != nil {
			if err := s.resolveVariable(ctx, v.ref, v.dir); err != nil {
				return err
			}
		}
	}
	return nil
}

// resolveVariable translates a logical VariableReference into a hardware address and EBI instructions.
func (s *DeviceService) resolveVariable(ctx context.Context, ref *dscpb.VariableReference, preferredDir PdoDirection) error {
	if ref == nil {
		return nil
	}
	tracingEnabled := s.config.GetOptions().GetEnableVariableResolutionTracing()
	if tracingEnabled {
		log.InfoContextf(ctx, "--- Tracing resolution for variable: pdo=%q, object=%q (PreferredDir=%v) ---", ref.GetPdo(), ref.GetObject(), preferredDir)
	}

	key := ref.GetPdo() + variableReferenceSeparator + ref.GetObject()
	if _, ok := s.pdoMappings[key]; ok {
		if tracingEnabled {
			log.InfoContextf(ctx, "Variable already resolved (cached).")
		}
		return nil
	}

	// 1. Resolve PDO Index (Implicit or Explicit).
	// We do this first because the Object resolution now depends on the PDO context.
	pdoIdx, pdoMeta, err := s.findPdo(ctx, ref.GetPdo(), objectAddress{}, nil, preferredDir)
	if err != nil && ref.GetPdo() != "" {
		// If explicit PDO resolution failed, we can't proceed.
		return fmt.Errorf("explicit PDO %q requested for variable %q was not found in ESI or is ambiguous: %w", ref.GetPdo(), ref.GetObject(), err)
	}

	if tracingEnabled {
		if pdoMeta != nil {
			log.InfoContextf(ctx, "Resolved PDO context: %q (#x%04X, Dir=%v)", pdoMeta.Name, pdoIdx, pdoMeta.Direction)
		} else {
			log.InfoContextf(ctx, "No explicit PDO context provided (implicit resolution).")
		}
	}

	// 2. Resolve Object Address (Hardware index/subindex).
	addr, meta, err := s.findObject(ctx, ref.GetObject(), pdoMeta, preferredDir)
	if err != nil {
		return fmt.Errorf("variable %q could not be resolved. Please check if the name or address matches the ESI file: %w", ref.GetObject(), err)
	}

	if tracingEnabled {
		log.InfoContextf(ctx, "Resolved Object: %q (#x%04X.%d, Mapping=%q)", meta.Name, addr.Index, addr.SubIndex, meta.PdoMapping)
	}

	// 3. If PDO was implicit (empty), we might need to re-resolve it now that we have the object's mapping flags.
	if ref.GetPdo() == "" {
		if tracingEnabled {
			log.InfoContextf(ctx, "Re-resolving PDO based on object mapping flags and preferred direction...")
		}
		pdoIdx, pdoMeta, err = s.findPdo(ctx, "", addr, meta, preferredDir)
		if err != nil {
			return fmt.Errorf("could not find suitable PDO for variable %q: %w", ref.GetObject(), err)
		}
		if tracingEnabled {
			log.InfoContextf(ctx, "Implicit PDO choice: %q (#x%04X)", pdoMeta.Name, pdoIdx)
		}
	}

	// 4. EBI Instruction Generation.
	if err := s.generateInstructions(ctx, pdoIdx, pdoMeta, addr, meta); err != nil {
		return fmt.Errorf("failed to map variable %q to PDO %q. This usually happens if the PDO is 'Fixed' or has a direction mismatch: %w", ref.GetObject(), pdoMeta.Name, err)
	}

	if tracingEnabled {
		log.InfoContextf(ctx, "Successfully resolved to PDO %q and Object %q.", pdoMeta.Name, meta.Name)
	}

	// Record resolution metadata.
	s.pdoMappings[key] = &dspb.ResolvedPdoVariable{
		PdoIndex:      pdoIdx,
		Index:         addr.Index,
		SubIndex:      addr.SubIndex,
		PdoEniName:    pdoMeta.Name,
		ObjectEniName: meta.Name,
	}

	return nil
}

// resolveActiveOpMode determines the synchronization mode to use based on the user configuration
// and the device's supported modes (from ESI).
func (s *DeviceService) resolveActiveOpMode(ctx context.Context) (string, error) {
	selectedOpMode := s.config.GetSyncConfig().GetSelectedOpModeName()

	// 1. Explicit selection by user.
	if selectedOpMode != "" {
		// Validate that the selected mode is actually supported by the device.
		for _, m := range s.supportedOpModes {
			if m.Name == selectedOpMode {
				return selectedOpMode, nil
			}
		}
		// Not found: this is a configuration error.
		var available []string
		for _, m := range s.supportedOpModes {
			available = append(available, fmt.Sprintf("%q", m.Name))
		}
		return "", fmt.Errorf("selected sync mode '%q' is not supported by this device (supported: %s)", selectedOpMode, strings.Join(available, ", "))
	}

	// 2. Automatic selection (user left it empty).

	// If the device reports NO OpModes in ESI, we cannot select one.
	// This implies the device uses its default implicit synchronization (FreeRun/SM).
	if len(s.supportedOpModes) == 0 {
		return "", nil
	}

	// Check if we have Ds402 mappings, which usually benefit from DC.
	hasDs402 := false
	for _, m := range s.config.GetInterfaceMappings() {
		if m.GetDeviceData().GetDs402DeviceData() != nil {
			hasDs402 = true
			break
		}
	}

	// If Ds402 is present, prefer the first available DC mode.
	if hasDs402 {
		for _, m := range s.supportedOpModes {
			if m.IsDc {
				log.InfoContextf(ctx, "Auto-selected DC mode '%q' for DS402 device.", m.Name)
				return m.Name, nil
			}
		}
	}

	// Fallback: Default to the first available mode.
	// ESI spec typically puts the default/preferred mode first.
	return s.supportedOpModes[0].Name, nil
}

// resolveBusVariableWrite resolves all variables required for a bus variable write.
//
// This function iterates over the list of process variable writes configured in `procVar`
// and attempts to resolve each one against the parsed ESI data using the current context `ctx`.
// It ensures that the variable is mapped to an RxPDO, as these are variables written to the device.
// After resolution, it verifies that the requested value type matches the data type
// defined in the device's ESI.
//
// It returns an error if any variable cannot be resolved or if there is a type mismatch.
func (s *DeviceService) resolveBusVariableWrite(ctx context.Context, procVar *dscpb.ProcessVariableDeviceData) error {
	for _, write := range procVar.GetBusVariableWrites() {
		ref := write.GetVariableReference()
		if err := s.resolveVariable(ctx, ref, PdoDirectionRx); err != nil {
			return err
		}
		key := ref.GetPdo() + variableReferenceSeparator + ref.GetObject()
		resolved := s.pdoMappings[key]
		addr := objectAddress{Index: resolved.Index, SubIndex: resolved.SubIndex}
		meta := s.objectIndex[addr]

		if err := verifyProcessVariableType(write.GetValue(), meta.DataType, meta.BitSize); err != nil {
			return fmt.Errorf("type mismatch for variable %q (ESI type %s, %d bits): %w", ref.GetObject(), meta.DataType, meta.BitSize, err)
		}
	}
	return nil
}

// resolveSdoDeviceData resolves all SDO variables required for reading and writing.
//
// Parameters:
//   - ctx: Request context.
//   - sdo_data: The SDO device data containing lists of reads and writes.
//
// Returns:
//   - An error if a named variable cannot be found in the dictionary.
func (s *DeviceService) resolveSdoDeviceData(ctx context.Context, sdo_data *dscpb.ServiceVariableDeviceData) error {
	resolved := make(map[string]*dspb.ResolvedSdoVariable)

	for _, write := range sdo_data.GetSdoWrites() {
		idx, sub, meta, err := s.resolveSdoVariable(ctx, write.GetObject(), PdoDirectionRx)
		if err != nil {
			return err
		}
		// We might be using undocumented SDOs. So only verify if we have metadata.
		if meta != nil {
			if err := verifyProcessVariableType(write.GetValue(), meta.DataType, meta.BitSize); err != nil {
				return fmt.Errorf("type mismatch for SDO write %q (ESI type %s, %d bits): %w", write.GetObject(), meta.DataType, meta.BitSize, err)
			}
		}
		resolved[write.GetObject()] = &dspb.ResolvedSdoVariable{Index: idx, SubIndex: sub}
	}

	for _, read := range sdo_data.GetSdoReads() {
		idx, sub, meta, err := s.resolveSdoVariable(ctx, read.GetObject(), PdoDirectionTx)
		if err != nil {
			return err
		}
		// We might be using undocumented SDOs. So only verify if we have metadata.
		if meta != nil {
			if err := verifySdoReadType(read.GetType(), meta.DataType, meta.BitSize); err != nil {
				return fmt.Errorf("type mismatch for SDO read %q (ESI type %s, %d bits): %w", read.GetObject(), meta.DataType, meta.BitSize, err)
			}
		}
		resolved[read.GetObject()] = &dspb.ResolvedSdoVariable{Index: idx, SubIndex: sub}
	}

	for k, v := range resolved {
		s.sdoMappings[k] = v
	}
	return nil
}

// resolveSdoVariable resolves a string identifier to a hardware address.
//
// SDOs (Service Data Objects) follow a different resolution path than PDOs
// because they use acyclic mailbox communication (CoE) rather than the
// real-time process data image. Crucially:
//  1. They do NOT need to be mapped to a PDO.
//  2. They do NOT trigger PDO exclusion conflicts or "Fixed PDO" errors.
//  3. They may target objects NOT present in the ESI dictionary (raw addresses).
//
// Parameters:
//   - ctx: Request context.
//   - object: The identifier string, e.g., "Status word" or "0x6041.0".
//   - expectedDir: The expected direction of the SDO variable (Rx for write, Tx for read).
//
// Returns:
//   - idx: The resolved 16-bit object index.
//   - sub: The resolved 8-bit sub-index.
//   - meta: Metadata from the ESI dictionary (nil if the object is not in the dictionary).
//   - err: An error if the name cannot be resolved or parsing fails.
func (s *DeviceService) resolveSdoVariable(ctx context.Context, object string, expectedDir PdoDirection) (uint32, uint32, *objectMetadata, error) {
	// 1. Try to parse as a raw numeric address (e.g. "0x605A.0")
	idxStr, subIdxStr := object, "0"
	if strings.Contains(object, ".") {
		parts := strings.SplitN(object, ".", 2)
		idxStr, subIdxStr = parts[0], parts[1]
	}

	idx, errIdx := parseUint32(idxStr)
	sub, errSub := parseUint32(subIdxStr)

	if errIdx == nil && errSub == nil {
		addr := objectAddress{Index: idx, SubIndex: sub}
		if meta, ok := s.objectIndex[addr]; ok {
			return idx, sub, meta, nil
		}
		// SDOs are allowed even if they are not in the dictionary (e.g. vendor-specific indices
		// missing from a generic ESI).
		log.WarningContextf(ctx, "SDO address #x%04X.%d not found in ESI dictionary. Type validation will be skipped.", idx, sub)
		return idx, sub, nil, nil
	}

	// 2. Resolve by name from the global dictionary.
	addr, meta, err := s.resolveObjectByName(ctx, object, nil, expectedDir)
	if err != nil {
		return 0, 0, nil, fmt.Errorf("SDO variable %q could not be resolved from ESI dictionary: %w", object, err)
	}
	return addr.Index, addr.SubIndex, meta, nil
}

// verifySdoReadType checks if a requested SDO read type matches the device's expected data type.
//
// Parameters:
//   - requestedType: The type requested by the user, as a proto enum.
//   - esiType: The expected data type as defined in the device's ESI dictionary.
//   - bitSize: The expected size of the variable in bits, as defined in the ESI.
//
// Returns:
//   - An error if the type validation fails, indicating a mismatch between the requested
//     type and the ESI specification, or if the `requestedType` is unsupported.
func verifySdoReadType(requestedType ecatpb.ServiceVariableType, esiType string, bitSize uint32) error {
	// Map the requested SDO read type to a dummy BusVariableValue so we can reuse verifyProcessVariableType
	dummyVal := &dscpb.BusVariableValue{}
	switch requestedType {
	case ecatpb.ServiceVariableType_BOOL_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_BoolValue{}
	case ecatpb.ServiceVariableType_UINT8_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Uint8Value{}
	case ecatpb.ServiceVariableType_INT8_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Int8Value{}
	case ecatpb.ServiceVariableType_UINT16_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Uint16Value{}
	case ecatpb.ServiceVariableType_INT16_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Int16Value{}
	case ecatpb.ServiceVariableType_UINT32_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Uint32Value{}
	case ecatpb.ServiceVariableType_INT32_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Int32Value{}
	case ecatpb.ServiceVariableType_UINT64_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Uint64Value{}
	case ecatpb.ServiceVariableType_INT64_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_Int64Value{}
	case ecatpb.ServiceVariableType_FLOAT_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_FloatValue{}
	case ecatpb.ServiceVariableType_DOUBLE_SDO_TYPE:
		dummyVal.Value = &dscpb.BusVariableValue_DoubleValue{}
	default:
		// Unknown or UNDEFINED_SDO_TYPE
		return fmt.Errorf("unsupported or missing requested type %q", requestedType)
	}

	if err := verifyProcessVariableType(dummyVal, esiType, bitSize); err != nil {
		return fmt.Errorf(strings.ReplaceAll(err.Error(), "provided", "requested"))
	}
	return nil
}

// verifyProcessVariableType checks if a bus variable value matches the device's expected data type.
//
// This function compares the type of `val` against the `esiType` and `bitSize`
// specified in the device's ESI file. It ensures that the provided value
// fits within the required number of bits and that there is no signedness mismatch
// (e.g., trying to write a signed integer to an unsigned type).
//
// It returns an error if the type validation fails or if `val` is nil.
func verifyProcessVariableType(val *dscpb.BusVariableValue, esiType string, bitSize uint32) error {
	if val == nil {
		return fmt.Errorf("value is nil")
	}
	esiTypeUpper := strings.ToUpper(esiType)

	// Helper function for range checking
	checkRange := func(val uint64, bitSize uint32) error {
		if bitSize >= 64 {
			return nil
		}
		maxVal := (uint64(1) << bitSize) - 1
		if val > maxVal {
			return fmt.Errorf("value %d exceeds maximum for %d bits", val, bitSize)
		}
		return nil
	}

	checkSignedRange := func(val int64, bitSize uint32) error {
		if bitSize >= 64 {
			return nil
		}
		maxVal := (int64(1) << (bitSize - 1)) - 1
		minVal := -(int64(1) << (bitSize - 1))
		if val > maxVal || val < minVal {
			return fmt.Errorf("value %d out of bounds for %d signed bits", val, bitSize)
		}
		return nil
	}

	switch val.GetValue().(type) {
	case *dscpb.BusVariableValue_BoolValue:
		if bitSize > 8 {
			return fmt.Errorf("expected <= 8 bits for bool, got %d", bitSize)
		}
		switch esiTypeUpper {
		case "BOOL", "BOOLEAN", "BIT", "BIT1":
			// OK
		default:
			return fmt.Errorf("bool value provided for non-bool type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Uint8Value:
		if bitSize > 8 {
			return fmt.Errorf("expected <= 8 bits for uint8, got %d", bitSize)
		}
		if err := checkRange(uint64(val.GetUint8Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "USINT", "UNSIGNED8", "BYTE", "BITARR8", "BIT2", "BIT3", "BIT4", "BIT5", "BIT6", "BIT7", "BIT8":
			// OK
		default:
			return fmt.Errorf("uint8 value provided for non-uint8 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Int8Value:
		if bitSize > 8 {
			return fmt.Errorf("expected <= 8 bits for int8, got %d", bitSize)
		}
		if err := checkSignedRange(int64(val.GetInt8Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "SINT", "INTEGER8":
			// OK
		default:
			return fmt.Errorf("int8 value provided for non-int8 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Uint16Value:
		if bitSize > 16 {
			return fmt.Errorf("expected <= 16 bits for uint16, got %d", bitSize)
		}
		if err := checkRange(uint64(val.GetUint16Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "UINT", "UNSIGNED16", "WORD", "BITARR16", "BIT9", "BIT10", "BIT11", "BIT12", "BIT13", "BIT14", "BIT15", "BIT16":
			// OK
		default:
			return fmt.Errorf("uint16 value provided for non-uint16 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Int16Value:
		if bitSize > 16 {
			return fmt.Errorf("expected <= 16 bits for int16, got %d", bitSize)
		}
		if err := checkSignedRange(int64(val.GetInt16Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "INT", "INTEGER16":
			// OK
		default:
			return fmt.Errorf("int16 value provided for non-int16 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Uint32Value:
		if bitSize > 32 {
			return fmt.Errorf("expected <= 32 bits for uint32, got %d", bitSize)
		}
		if err := checkRange(uint64(val.GetUint32Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "UDINT", "UNSIGNED32", "DWORD", "UNSIGNED24", "BITARR32":
			// OK
		default:
			return fmt.Errorf("uint32 value provided for non-uint32 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Int32Value:
		if bitSize > 32 {
			return fmt.Errorf("expected <= 32 bits for int32, got %d", bitSize)
		}
		if err := checkSignedRange(int64(val.GetInt32Value()), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "DINT", "INTEGER32", "INTEGER24":
			// OK
		default:
			return fmt.Errorf("int32 value provided for non-int32 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Uint64Value:
		if bitSize > 64 {
			return fmt.Errorf("expected <= 64 bits for uint64, got %d", bitSize)
		}
		if err := checkRange(val.GetUint64Value(), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "ULINT", "UNSIGNED64", "LWORD", "UNSIGNED40", "UNSIGNED48", "UNSIGNED56":
			// OK
		default:
			return fmt.Errorf("uint64 value provided for non-uint64 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_Int64Value:
		if bitSize > 64 {
			return fmt.Errorf("expected <= 64 bits for int64, got %d", bitSize)
		}
		if err := checkSignedRange(val.GetInt64Value(), bitSize); err != nil {
			return err
		}
		switch esiTypeUpper {
		case "LINT", "INTEGER64", "INTEGER40", "INTEGER48", "INTEGER56":
			// OK
		default:
			return fmt.Errorf("int64 value provided for non-int64 type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_FloatValue:
		if bitSize != 32 {
			return fmt.Errorf("expected 32 bits for float, got %d", bitSize)
		}
		switch esiTypeUpper {
		case "REAL", "REAL32", "FLOAT32", "FLOAT":
			// OK
		default:
			return fmt.Errorf("float value provided for non-float type %s", esiTypeUpper)
		}
	case *dscpb.BusVariableValue_DoubleValue:
		if bitSize != 64 {
			return fmt.Errorf("expected 64 bits for double, got %d", bitSize)
		}
		switch esiTypeUpper {
		case "LREAL", "REAL64", "FLOAT64", "DOUBLE":
			// OK
		default:
			return fmt.Errorf("double value provided for non-double type %s", esiTypeUpper)
		}
	default:
		return fmt.Errorf("unsupported value type: %T", val.GetValue())
	}
	return nil
}
