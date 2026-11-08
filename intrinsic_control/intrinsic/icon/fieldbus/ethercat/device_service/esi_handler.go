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
	"encoding/xml"
	"fmt"
	"sort"
	"strconv"
	"strings"

	log "github.com/golang/glog"

	dspb "intrinsic/icon/fieldbus/ethercat/device_service/v1/device_service_go_proto"
	esipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/esi_go_proto"
)

// PdoDirection defines whether a PDO is Transmit (Input to MainDevice) or Receive (Output from MainDevice).
type PdoDirection int

const (
	// PdoDirectionUnknown indicates the direction was not specified.
	PdoDirectionUnknown PdoDirection = iota
	// PdoDirectionTx represents a Transmit PDO (SubDevice -> MainDevice).
	PdoDirectionTx
	// PdoDirectionRx represents a Receive PDO (MainDevice -> SubDevice).
	PdoDirectionRx
)

// XML structures for ESI parsing according to ETG.2000

type esiEtherCATInfo struct {
	XMLName        xml.Name        `xml:"EtherCATInfo"`
	InfoReferences []string        `xml:"InfoReference"`
	Vendor         esiVendor       `xml:"Vendor"`
	Descriptions   esiDescriptions `xml:"Descriptions"`
}

type esiVendor struct {
	ID   string `xml:"Id"`
	Name string `xml:"Name"`
}

type esiDescriptions struct {
	Groups  []esiGroup  `xml:"Groups>Group"`
	Devices []esiDevice `xml:"Devices>Device"`
	Modules []esiModule `xml:"Modules>Module"`
}

type esiGroup struct {
	Type string `xml:"Type"`
	Name string `xml:"Name"`
}

type esiDevice struct {
	Physics string `xml:"Physics,attr"`
	Type    struct {
		ProductCode string `xml:"ProductCode,attr"`
		RevisionNo  string `xml:"RevisionNo,attr"`
		Value       string `xml:",chardata"`
	} `xml:"Type"`
	Name    string     `xml:"Name"`
	Sm      []esiSm    `xml:"Sm"`
	RxPdo   []esiPdo   `xml:"RxPdo"`
	TxPdo   []esiPdo   `xml:"TxPdo"`
	Profile esiProfile `xml:"Profile"`
	Dc      *esiDc     `xml:"Dc"`
}

type esiDc struct {
	OpModes []esiOpMode `xml:"OpMode"`
}

type esiOpMode struct {
	Name           string `xml:"Name"`
	Desc           string `xml:"Desc"`
	AssignActivate string `xml:"AssignActivate"`
}

type esiSm struct {
	DefaultSize  string `xml:"DefaultSize,attr"`
	StartAddress string `xml:"StartAddress,attr"`
	ControlByte  string `xml:"ControlByte,attr"`
	Enable       string `xml:"Enable,attr"`
	Value        string `xml:",chardata"`
}

type esiPdo struct {
	Fixed     string     `xml:"Fixed,attr"`
	Mandatory string     `xml:"Mandatory,attr"`
	Sm        string     `xml:"Sm,attr"`
	Index     string     `xml:"Index"`
	Names     []esiName  `xml:"Name"`
	Excludes  []string   `xml:"Exclude"`
	Entries   []esiEntry `xml:"Entry"`
}

type esiName struct {
	LcId  string `xml:"LcId,attr"`
	Value string `xml:",chardata"`
}

type esiEntry struct {
	Index    string    `xml:"Index"`
	SubIndex string    `xml:"SubIndex"`
	BitLen   uint32    `xml:"BitLen"`
	Names    []esiName `xml:"Name"`
	DataType string    `xml:"DataType"`
}

type esiProfile struct {
	DictionaryFile string        `xml:"DictionaryFile"`
	Dictionary     esiDictionary `xml:"Dictionary"`
}

type esiDictionary struct {
	DataTypes []esiDataType `xml:"DataTypes>DataType"`
	Objects   []esiObject   `xml:"Objects>Object"`
}

type esiDataType struct {
	Name     string       `xml:"Name"`
	BitSize  uint32       `xml:"BitSize"`
	SubItems []esiSubItem `xml:"SubItem"`
}

type esiSubItem struct {
	SubIdx     string    `xml:"SubIdx"`
	Names      []esiName `xml:"Name"`
	Type       string    `xml:"Type"`
	BitSize    uint32    `xml:"BitSize"`
	PdoMapping string    `xml:"Flags>PdoMapping"`
}

type esiObject struct {
	Index      string    `xml:"Index"`
	Names      []esiName `xml:"Name"`
	Type       string    `xml:"Type"`
	BitSize    uint32    `xml:"BitSize"`
	PdoMapping string    `xml:"Flags>PdoMapping"`
}

// esiModuleFile represents the structure of external module files referenced by InfoReference.
type esiModuleFile struct {
	XMLName        xml.Name    `xml:"EtherCATModule"`
	InfoReferences []string    `xml:"InfoReference"`
	Modules        []esiModule `xml:"Modules>Module"`
}

type esiModule struct {
	Type struct {
		ModuleIdent string `xml:"ModuleIdent,attr"`
	} `xml:"Type"`
	Name    string     `xml:"Name"`
	RxPdo   []esiPdo   `xml:"RxPdo"`
	TxPdo   []esiPdo   `xml:"TxPdo"`
	Profile esiProfile `xml:"Profile"`
}

// Internal metadata structures

const (
	// lcidEnglish is the Language Identifier for English (United States).
	// ESIs use this ID to identify English localized names.
	lcidEnglish = "1033"
)

type objectMetadata struct {
	Index      uint32
	SubIndex   uint32
	Name       string
	DataType   string
	BitSize    uint32
	PdoMapping string // "T", "R", "RT", or empty
}

type pdoMetadata struct {
	Index          uint32
	Name           string
	Direction      PdoDirection
	Fixed          bool
	DefaultSm      int
	Excludes       []uint32
	DefaultEntries []objectAddress
	// Index for contextual lookup of variables within this PDO.
	NameToAddr map[string]objectAddress
}

type objectAddress struct {
	Index    uint32
	SubIndex uint32
}

// pickName selects the best name from a slice of localized names, preferring English.
//
// Parameters:
//   - names: A slice of esiName structs containing LcId and Value.
//
// Returns:
//   - The localized string value for English if found, otherwise the first
//     available name without an LcId, falling back to the first available name.
func pickName(names []esiName) string {
	if len(names) == 0 {
		return ""
	}
	// 1. Search for English
	for _, n := range names {
		if n.LcId == lcidEnglish {
			return n.Value
		}
	}
	// 2. Search for a name without an LcId (potential default/English)
	for _, n := range names {
		if n.LcId == "" {
			return n.Value
		}
	}
	// 3. Default to the first available name
	return names[0].Value
}

// makeFallbackVariableName generates a standardized name for objects or entries that lack
// a localized name in the ESI. Format: Var_#x[Index]_[SubIndex]
func makeFallbackVariableName(index, subIndex uint32) string {
	return fmt.Sprintf("Var_#x%04X_%d", index, subIndex)
}

// parseUint32 parses a string that might be in hex (0x... or #x...) or decimal.
//
// Parameters:
//   - s: The string representation of the number.
//
// Returns:
//   - The parsed uint32 value.
//   - An error if the string is not a valid number in the detected base.
func parseUint32(s string) (uint32, error) {
	s = strings.TrimSpace(s)
	if s == "" {
		return 0, nil
	}
	base := 10
	if strings.HasPrefix(s, "0x") || strings.HasPrefix(s, "0X") {
		s = s[2:]
		base = 16
	} else if strings.HasPrefix(s, "#x") || strings.HasPrefix(s, "#X") {
		s = s[2:]
		base = 16
	}
	val, err := strconv.ParseUint(s, base, 32)
	return uint32(val), err
}

// loadAndIndexESI parses the ESI bundle and builds the primary indices for variable resolution.
// It handles multi-file bundles by following InfoReference and DictionaryFile links.
//
// Parameters:
//   - ctx: Request context.
//   - bundle: The ESI bundle containing the XML files.
//   - vendorID: The vendor ID of the target device.
//   - productCode: The product code of the target device.
//   - revision: The revision number of the target device.
//
// Returns:
//   - An error if the device cannot be found in the bundle or if any referenced file fails to parse.
func (s *DeviceService) loadAndIndexESI(ctx context.Context, bundle *esipb.EsiBundle, vendorID, productCode, revision uint32) error {
	s.objectIndex = make(map[objectAddress]*objectMetadata)
	s.objectNameIndex = make(map[string][]objectAddress)
	s.pdoIndex = make(map[uint32]*pdoMetadata)

	var targetDevice *esiDevice
	var mainInfo *esiEtherCATInfo

	// Phase A: Locate target device
	for _, file := range bundle.GetFiles() {
		var info esiEtherCATInfo
		if err := xml.Unmarshal([]byte(file.Data), &info); err != nil {
			// The EsiBundle might consist of EtherCATInfo or EtherCATModule files (or
			// potential other files a user might have added). We first only want to
			// process the EtherCATInfo files, so we skip files that are not valid
			// EtherCATInfo XML or are malformed.
			continue
		}

		// ESIs often contain multiple devices. Looking for the requested one.
		for _, dev := range info.Descriptions.Devices {
			// If parsing fails, the value is invalid or malformed; skip this device as it cannot match.
			pCode, _ := parseUint32(dev.Type.ProductCode)
			rev, _ := parseUint32(dev.Type.RevisionNo)
			vID, _ := parseUint32(info.Vendor.ID)

			if vID == vendorID && pCode == productCode && rev == revision {
				targetDevice = &dev
				mainInfo = &info
				break
			}
		}
		if targetDevice != nil {
			break
		}
	}

	if targetDevice == nil {
		return fmt.Errorf("device with VendorID %d, ProductCode %d, Revision %d not found in ESI bundle", vendorID, productCode, revision)
	}

	// Parse OpModes from ESI.
	if targetDevice.Dc != nil {
		for _, mode := range targetDevice.Dc.OpModes {
			isDc := false
			if val, err := parseUint32(mode.AssignActivate); err == nil && val > 0 {
				isDc = true
			}
			s.supportedOpModes = append(s.supportedOpModes, &dspb.OpModeInfo{
				Name:        mode.Name,
				Description: mode.Desc,
				IsDc:        isDc,
			})
		}
	}

	// If a device has no <Dc> element or no <OpMode>s, s.supportedOpModes will be empty.
	// This is valid and implies the device does not support explicit Synchronization Mode selection
	// (it likely operates in FreeRun or SM-Synchronous mode implicitly).

	// Index Objects (including external DictionaryFile)
	if err := s.indexObjects(bundle, targetDevice.Profile); err != nil {
		return err
	}

	// Index default PDOs
	s.indexPdos(targetDevice.RxPdo, PdoDirectionRx)
	s.indexPdos(targetDevice.TxPdo, PdoDirectionTx)

	// Index objects from PDO entries (common in real-world ESIs)
	s.indexPdoEntries(targetDevice.RxPdo, "R")
	s.indexPdoEntries(targetDevice.TxPdo, "T")

	// Follow InfoReferences to find additional PDOs and Objects defined in Modules.
	for _, ref := range mainInfo.InfoReferences {
		if file, ok := bundle.GetFiles()[ref]; ok {
			// The file can either be a module or an info file.
			var modFile esiModuleFile
			if err := xml.Unmarshal([]byte(file.Data), &modFile); err == nil && len(modFile.Modules) > 0 {
				// Nested InfoReferences are not forbidden, but unlikely. To keep complexity managable (for now) we return an error in such cases.
				if len(modFile.InfoReferences) > 0 {
					return fmt.Errorf("module file %q contains nested InfoReferences: %w", ref, ErrEsiDeepNesting)
				}
				for _, mod := range modFile.Modules {
					s.indexObjects(bundle, mod.Profile)
					s.indexPdos(mod.RxPdo, PdoDirectionRx)
					s.indexPdos(mod.TxPdo, PdoDirectionTx)
					s.indexPdoEntries(mod.RxPdo, "R")
					s.indexPdoEntries(mod.TxPdo, "T")
				}
				continue
			}
			var infoFile esiEtherCATInfo
			if err := xml.Unmarshal([]byte(file.Data), &infoFile); err == nil {
				if len(infoFile.InfoReferences) > 0 {
					return fmt.Errorf("referenced ESI file %q contains nested InfoReferences: %w", ref, ErrEsiDeepNesting)
				}
				for _, mod := range infoFile.Descriptions.Modules {
					s.indexObjects(bundle, mod.Profile)
					s.indexPdos(mod.RxPdo, PdoDirectionRx)
					s.indexPdos(mod.TxPdo, PdoDirectionTx)
					s.indexPdoEntries(mod.RxPdo, "R")
					s.indexPdoEntries(mod.TxPdo, "T")
				}
			}
		}
	}

	return nil
}

// indexPdoEntries indexes objects defined directly within PDO tags, which is common in MDP devices.
// It also builds the contextual NameToAddr map for each PDO to support scope-limited variable lookup.
//
// This indexing is supplemental to the global dictionary indexing. It is safe to continue on
// individual parsing or lookup failures as malformed PDO entries will simply be unavailable
// for contextual lookup, while still potentially being resolvable globally via the dictionary.
//
// Parameters:
//   - pdos: Slice of PDOs to scan for entries.
//   - pdoMapping: The default mapping flag ("T" or "R") to apply if the entry is missing one.
func (s *DeviceService) indexPdoEntries(pdos []esiPdo, pdoMapping string) {
	for _, pdo := range pdos {
		pdoIdx, _ := parseUint32(pdo.Index)
		// pdoIndex must be pre-populated by a prior call to indexPdos.
		// If a PDO index is not found, we skip its entries because we cannot safely associate
		// them with a Sync Manager or Direction without the underlying PDO definition metadata.
		meta, ok := s.pdoIndex[pdoIdx]
		if !ok {
			continue
		}

		for _, entry := range pdo.Entries {
			idx, errIdx := parseUint32(entry.Index)
			sub, errSub := parseUint32(entry.SubIndex)
			if errIdx != nil || errSub != nil {
				// Skip entries with malformed hardware addresses.
				continue
			}
			if idx == 0 {
				continue // Skip padding entries
			}

			entryName := pickName(entry.Names)
			addr := objectAddress{Index: idx, SubIndex: sub}

			// If entry doesn't have a name, try to use the name from the dictionary (which should be indexed by now)
			if entryName == "" {
				if dictMeta, ok := s.objectIndex[addr]; ok {
					entryName = dictMeta.Name
				}
			}

			// Use a fallback name built from index and subindex if we still don't have a name.
			if entryName == "" {
				entryName = makeFallbackVariableName(idx, sub)
			}

			// Add to contextual PDO index.
			meta.NameToAddr[entryName] = addr

			// Only index globally if not already present (prefer dictionary metadata if available)
			if existing, ok := s.objectIndex[addr]; ok {
				// If already present, ensure the mapping includes this PDO's direction.
				if !strings.Contains(existing.PdoMapping, pdoMapping) {
					existing.PdoMapping += pdoMapping
				}
			} else {
				s.objectIndex[addr] = &objectMetadata{
					Index:      idx,
					SubIndex:   sub,
					Name:       entryName,
					DataType:   entry.DataType,
					BitSize:    entry.BitLen,
					PdoMapping: pdoMapping,
				}
				s.objectNameIndex[entryName] = append(s.objectNameIndex[entryName], addr)
			}
		}
	}
}

// indexObjects populates the ObjectIndex and ObjectNameIndex from the device dictionary.
// It resolves complex DataTyped objects into their individual SubItems for precise addressing.
//
// Parameters:
//   - bundle: The ESI bundle containing potential external dictionary files.
//   - profile: The ESI profile containing dictionary and dictionary file references.
//
// Returns:
//   - An error if the external dictionary file exists but cannot be parsed.
func (s *DeviceService) indexObjects(bundle *esipb.EsiBundle, profile esiProfile) error {
	objects := profile.Dictionary.Objects
	dataTypes := make(map[string]esiDataType)
	for _, dt := range profile.Dictionary.DataTypes {
		dataTypes[dt.Name] = dt
	}

	if profile.DictionaryFile != "" {
		if dictData, ok := bundle.GetFiles()[profile.DictionaryFile]; ok {
			var dict esiDictionary
			if err := xml.Unmarshal([]byte(dictData.Data), &dict); err == nil {
				objects = append(objects, dict.Objects...)
				for _, dt := range dict.DataTypes {
					dataTypes[dt.Name] = dt
				}
			} else {
				type dictFileWrapper struct {
					Profile struct {
						Dictionary esiDictionary `xml:"Dictionary"`
					} `xml:"Profile"`
				}
				var wrapper dictFileWrapper
				if err := xml.Unmarshal([]byte(dictData.Data), &wrapper); err == nil {
					objects = append(objects, wrapper.Profile.Dictionary.Objects...)
					for _, dt := range wrapper.Profile.Dictionary.DataTypes {
						dataTypes[dt.Name] = dt
					}
				}
			}
		}
	}

	for _, obj := range objects {
		idx, err := parseUint32(obj.Index)
		if err != nil {
			continue
		}

		objName := pickName(obj.Names)
		if objName == "" {
			objName = makeFallbackVariableName(idx, 0)
		}
		// Always index sub-index 0
		meta0 := &objectMetadata{
			Index:      idx,
			SubIndex:   0,
			Name:       objName,
			DataType:   obj.Type,
			BitSize:    obj.BitSize,
			PdoMapping: obj.PdoMapping,
		}
		addr0 := objectAddress{Index: idx, SubIndex: 0}
		s.objectIndex[addr0] = meta0
		s.objectNameIndex[objName] = append(s.objectNameIndex[objName], addr0)

		// Resolve sub-indices if the type has SubItems
		if dt, ok := dataTypes[obj.Type]; ok && len(dt.SubItems) > 0 {
			// Edge case: Sub-index 0 might be explicitly defined in the DataType with its own PdoMapping.
			for _, si := range dt.SubItems {
				if sIdx, _ := parseUint32(si.SubIdx); sIdx == 0 {
					if si.PdoMapping != "" {
						meta0.PdoMapping = si.PdoMapping
					}
					break
				}
			}

			for _, si := range dt.SubItems {
				subIdx, err := parseUint32(si.SubIdx)
				if err != nil {
					continue
				}
				if subIdx == 0 {
					continue // Already indexed as meta0
				}

				siName := pickName(si.Names)
				if siName == "" {
					siName = makeFallbackVariableName(idx, subIdx)
				}
				// Priority: Item specific mapping -> Parent mapping
				mapping := si.PdoMapping
				if mapping == "" {
					mapping = obj.PdoMapping
				}

				metaSI := &objectMetadata{
					Index:      idx,
					SubIndex:   subIdx,
					Name:       siName,
					DataType:   si.Type,
					BitSize:    si.BitSize,
					PdoMapping: mapping,
				}
				addrSI := objectAddress{Index: idx, SubIndex: subIdx}
				s.objectIndex[addrSI] = metaSI
				s.objectNameIndex[siName] = append(s.objectNameIndex[siName], addrSI)
			}
		}
	}
	return nil
}

// indexPdos populates the PdoIndex with metadata required for EBI generation.
//
// Parameters:
//   - pdos: Slice of PDOs to index.
//   - dir: Direction (Tx or Rx) of the PDOs.
func (s *DeviceService) indexPdos(pdos []esiPdo, dir PdoDirection) {
	for _, p := range pdos {
		idx, err := parseUint32(p.Index)
		if err != nil {
			continue
		}
		fixed := strings.EqualFold(p.Fixed, "1") || strings.EqualFold(p.Fixed, "true")
		sm, _ := strconv.Atoi(p.Sm)

		var excludes []uint32
		for _, ex := range p.Excludes {
			if exIdx, err := parseUint32(ex); err == nil {
				excludes = append(excludes, exIdx)
			}
		}

		var entries []objectAddress
		for _, e := range p.Entries {
			eIdx, _ := parseUint32(e.Index)
			eSub, _ := parseUint32(e.SubIndex)
			entries = append(entries, objectAddress{Index: eIdx, SubIndex: eSub})
		}

		pdoName := pickName(p.Names)
		meta := &pdoMetadata{
			Index:          idx,
			Name:           pdoName,
			Direction:      dir,
			Fixed:          fixed,
			DefaultSm:      sm,
			Excludes:       excludes,
			DefaultEntries: entries,
			NameToAddr:     make(map[string]objectAddress),
		}

		// Pre-populate contextual index from default entries
		for i, addr := range entries {
			entryName := pickName(p.Entries[i].Names)
			if entryName != "" {
				meta.NameToAddr[entryName] = addr
			}
		}

		s.pdoIndex[idx] = meta
	}
}

// findObject resolves a variable name or address string into a hardware address and metadata.
// It prioritizes contextual search if a PDO context is provided.
//
// Parameters:
//   - ctx: Request context.
//   - object: The name or address string to resolve (supports "name:" prefix for strict naming).
//   - pdoContext: The target PDO metadata to scope the search (optional).
//   - preferredDir: The preferred direction (Tx/Rx) for the variable.
//
// Returns:
//   - The resolved hardware address.
//   - Associated metadata for the object.
//   - An error if resolution fails or if a name search is ambiguous without context.
func (s *DeviceService) findObject(ctx context.Context, object string, pdoContext *pdoMetadata, preferredDir PdoDirection) (objectAddress, *objectMetadata, error) {
	tracingEnabled := s.config.GetOptions().GetEnableVariableResolutionTracing()

	if strings.HasPrefix(object, "name:") {
		name := strings.TrimPrefix(object, "name:")
		if tracingEnabled {
			log.InfoContextf(ctx, "Strict name search for: %q", name)
		}
		return s.resolveObjectByName(ctx, name, pdoContext, preferredDir)
	}

	// Try address pattern first
	idxStr := object
	subIdxStr := "0"
	if strings.Contains(object, ".") {
		parts := strings.SplitN(object, ".", 2)
		idxStr = parts[0]
		subIdxStr = parts[1]
	}

	idx, errIdx := parseUint32(idxStr)
	sub, errSub := parseUint32(subIdxStr)
	if errIdx == nil && errSub == nil {
		addr := objectAddress{Index: idx, SubIndex: sub}
		if meta, ok := s.objectIndex[addr]; ok {
			if tracingEnabled {
				log.InfoContextf(ctx, "Resolved via numeric address: #x%04X.%d", idx, sub)
			}
			return addr, meta, nil
		}
	}

	if tracingEnabled {
		log.InfoContextf(ctx, "Attempting name search for: %q", object)
	}
	return s.resolveObjectByName(ctx, object, pdoContext, preferredDir)
}

// resolveObjectByName implements a context-first resolution strategy.
//
// In EtherCAT, variable names are not guaranteed to be globally unique (e.g.,
// multiple modules might each define a "Status word"). The context-first strategy
// resolves this by:
//  1. Searching inside the provided pdoContext first. If a user explicitly
//     requests a PDO (or we can infer one), searching within that PDO's entries
//     ensures we find the exact instance of the variable intended, effectively
//     eliminating global name collisions.
//  2. Falling back to a global search only if no context is provided or the
//     name is not found within the context.
//  3. Applying directional filters (Preferred Direction) to the global results
//     to further narrow down candidates for ambiguous names.
//
// Parameters:
//   - ctx: Request context.
//   - name: The localized variable name to search for.
//   - pdoContext: The target PDO metadata to scope the search (optional).
//   - preferredDir: The preferred direction (Tx/Rx) for the variable.
//
// Returns:
//   - The resolved hardware address.
//   - Associated metadata.
//   - An error if the name is not found or is ambiguous after filtering.
func (s *DeviceService) resolveObjectByName(ctx context.Context, name string, pdoContext *pdoMetadata, preferredDir PdoDirection) (objectAddress, *objectMetadata, error) {
	tracingEnabled := s.config.GetOptions().GetEnableVariableResolutionTracing()

	// 1. Contextual Search: Look inside requested PDO first.
	if pdoContext != nil {
		if addr, ok := pdoContext.NameToAddr[name]; ok {
			if tracingEnabled {
				log.InfoContextf(ctx, "Found %q inside PDO context #x%04X", name, pdoContext.Index)
			}
			return addr, s.objectIndex[addr], nil
		}
	}

	// 2. Global Search: Fallback to global index.
	addrs := s.objectNameIndex[name]
	if len(addrs) == 0 {
		return objectAddress{}, nil, fmt.Errorf("object name %q not found", name)
	}

	// 3. Ambiguity Filtering: If multiple objects have the same name, use context and direction to filter.
	if len(addrs) > 1 {
		if tracingEnabled {
			log.InfoContextf(ctx, "Ambiguous name %q found at %d locations globally.", name, len(addrs))
		}

		// Priority A: Filter by PDO context direction.
		if pdoContext != nil {
			var filtered []objectAddress
			for _, addr := range addrs {
				meta := s.objectIndex[addr]
				if isDirectionAllowed(meta.PdoMapping, pdoContext.Direction) {
					filtered = append(filtered, addr)
				}
			}
			if len(filtered) == 1 {
				if tracingEnabled {
					log.InfoContextf(ctx, "Resolved ambiguity using PDO direction filter: #x%04X.%d", filtered[0].Index, filtered[0].SubIndex)
				}
				return filtered[0], s.objectIndex[filtered[0]], nil
			}
			addrs = filtered // Continue with narrowed set
		}

		// Priority B: Filter by Preferred direction.
		if preferredDir != PdoDirectionUnknown && len(addrs) > 1 {
			var filtered []objectAddress
			for _, addr := range addrs {
				meta := s.objectIndex[addr]
				if isDirectionAllowed(meta.PdoMapping, preferredDir) {
					filtered = append(filtered, addr)
				}
			}
			if len(filtered) == 1 {
				if tracingEnabled {
					log.InfoContextf(ctx, "Resolved ambiguity using Preferred direction filter: #x%04X.%d", filtered[0].Index, filtered[0].SubIndex)
				}
				return filtered[0], s.objectIndex[filtered[0]], nil
			}
			addrs = filtered // Continue with narrowed set
		}

		if len(addrs) == 0 {
			return objectAddress{}, nil, fmt.Errorf("object name %q not found after direction filtering", name)
		}
		if len(addrs) > 1 {
			return objectAddress{}, nil, fmt.Errorf("ambiguous object name %q remains after direction filtering", name)
		}
	}

	if tracingEnabled {
		log.InfoContextf(ctx, "Resolved via global name search: #x%04X.%d", addrs[0].Index, addrs[0].SubIndex)
	}
	return addrs[0], s.objectIndex[addrs[0]], nil
}

// isDirectionAllowed checks if the object's mapping flags support the target PDO direction.
// According to ETG.2000, mapping flags can be "T" (Transmit/Input), "R" (Receive/Output),
// or "RT" (Both). We use strings.Contains to correctly match the specific direction
// even if the object is bi-directional (RT).
func isDirectionAllowed(mapping string, dir PdoDirection) bool {
	if dir == PdoDirectionTx {
		return strings.Contains(mapping, "T")
	}
	if dir == PdoDirectionRx {
		return strings.Contains(mapping, "R")
	}
	return false
}

// findPdo resolves the PDO index for a given variable, either through explicit naming or implicit ESI defaults.
//
// Parameters:
//   - ctx: Request context.
//   - pdo: Explicit PDO name or index (optional).
//   - addr: The object address being mapped.
//   - objMeta: Metadata for the object being mapped. (Optional, required for Pass 3).
//   - preferredDir: The preferred direction (Tx/Rx) for the variable.
//
// Returns:
//   - Resolved PDO index.
//   - Associated PDO metadata.
//   - An error if no suitable PDO can be found or if explicit PDO resolution fails.
func (s *DeviceService) findPdo(ctx context.Context, pdo string, addr objectAddress, objMeta *objectMetadata, preferredDir PdoDirection) (uint32, *pdoMetadata, error) {
	tracingEnabled := s.config.GetOptions().GetEnableVariableResolutionTracing()

	if pdo != "" {
		if idx, err := parseUint32(pdo); err == nil {
			if meta, ok := s.pdoIndex[idx]; ok {
				return idx, meta, nil
			}
		}
		var found *pdoMetadata
		for _, meta := range s.pdoIndex {
			if meta.Name == pdo {
				if found != nil {
					return 0, nil, fmt.Errorf("ambiguous PDO name %q", pdo)
				}
				found = meta
			}
		}
		if found != nil {
			return found.Index, found, nil
		}
		return 0, nil, fmt.Errorf("PDO %q not found", pdo)
	}

	// Implicit Resolution Pass 1: Look in already active PDOs that contain this object.
	// This is the most efficient choice as it requires zero EBI changes.
	if tracingEnabled {
		log.InfoContextf(ctx, "Pass 1: Searching currently active PDOs for reuse...")
	}
	for pdoIdx := range s.activePdos {
		meta := s.pdoIndex[pdoIdx]
		for _, entry := range meta.DefaultEntries {
			if entry == addr {
				if tracingEnabled {
					log.InfoContextf(ctx, "Match found! Reusing already active PDO #x%04X", pdoIdx)
				}
				return pdoIdx, meta, nil
			}
		}
	}

	// Pass 2: Look in default-active fixed PDOs that contain this object.
	if tracingEnabled {
		log.InfoContextf(ctx, "Pass 2: Searching default-active fixed PDOs...")
	}
	for _, meta := range s.pdoIndex {
		if meta.DefaultSm != 0 && meta.Fixed {
			for _, entry := range meta.DefaultEntries {
				if entry == addr {
					if tracingEnabled {
						log.InfoContextf(ctx, "Match found in default-active fixed PDO #x%04X", meta.Index)
					}
					return meta.Index, meta, nil
				}
			}
		}
	}

	// Pass 3: pick the first mappable PDO that supports the object's direction.
	if objMeta != nil {
		if tracingEnabled {
			log.InfoContextf(ctx, "Pass 3: Searching for ANY mappable PDO matching direction...")
		}
		// If PdoMapping is empty, the object cannot be mapped to a PDO (SDO access only).
		if objMeta.PdoMapping == "" {
			return 0, nil, fmt.Errorf("object %q not mappable to PDO", objMeta.Name)
		}

		// Iterate in a deterministic (sorted) order (by index).
		var pdoOrder []uint32
		for idx := range s.pdoIndex {
			pdoOrder = append(pdoOrder, idx)
		}
		sort.Slice(pdoOrder, func(i, j int) bool { return pdoOrder[i] < pdoOrder[j] })

		for _, pdoIdx := range pdoOrder {
			meta := s.pdoIndex[pdoIdx]
			if !isDirectionAllowed(objMeta.PdoMapping, meta.Direction) || meta.Fixed {
				continue
			}
			// If we have a preferred direction, skip PDOs of the other direction in this pass.
			if preferredDir != PdoDirectionUnknown && meta.Direction != preferredDir {
				continue
			}

			if tracingEnabled {
				log.InfoContextf(ctx, "Found mappable PDO #x%04X (Dir=%v)", meta.Index, meta.Direction)
			}
			return meta.Index, meta, nil
		}

		// Final fallback: if preferredDir didn't work, try any valid direction.
		if tracingEnabled {
			log.InfoContextf(ctx, "Pass 4: Fallback to any valid direction mapping...")
		}
		for _, pdoIdx := range pdoOrder {
			meta := s.pdoIndex[pdoIdx]
			if isDirectionAllowed(objMeta.PdoMapping, meta.Direction) && !meta.Fixed {
				return meta.Index, meta, nil
			}
		}
	}

	return 0, nil, fmt.Errorf("could not find suitable PDO")
}

// generateInstructions records any necessary EBI modifications (activation/addition).
//
// Parameters:
//   - ctx: Request context.
//   - pdoIdx: Index of target PDO.
//   - pdoMeta: Metadata of target PDO.
//   - addr: Object address to map.
//   - objMeta: Metadata for the object.
//
// Returns:
//   - An error if the mapping violates ESI constraints (e.g. fixed PDO or direction mismatch).
func (s *DeviceService) generateInstructions(ctx context.Context, pdoIdx uint32, pdoMeta *pdoMetadata, addr objectAddress, objMeta *objectMetadata) error {
	tracingEnabled := s.config.GetOptions().GetEnableVariableResolutionTracing()

	if !s.activePdos[pdoIdx] {
		if pdoMeta.DefaultSm == 0 {
			if tracingEnabled {
				log.InfoContextf(ctx, "Activating inactive PDO %q (#x%04X)", pdoMeta.Name, pdoIdx)
			}
			s.exclusionsToRemove = append(s.exclusionsToRemove, pdoIdx)
			for _, ex := range pdoMeta.Excludes {
				if other, ok := s.pdoIndex[ex]; ok && other.DefaultSm != 0 {
					if tracingEnabled {
						log.InfoContextf(ctx, "Conflict: Enabling #x%04X requires excluding default-active PDO %q (#x%04X)", pdoIdx, other.Name, ex)
					}
					s.exclusionsToAdd = append(s.exclusionsToAdd, ex)
				}
			}
		}
		s.activePdos[pdoIdx] = true
	} else if tracingEnabled {
		log.InfoContextf(ctx, "PDO %q (#x%04X) is already active. Reuse confirmed.", pdoMeta.Name, pdoIdx)
	}

	found := false
	for _, entry := range pdoMeta.DefaultEntries {
		if entry == addr {
			found = true
			break
		}
	}

	if !found {
		if pdoMeta.Fixed {
			return fmt.Errorf("cannot add variable to fixed PDO %q", pdoMeta.Name)
		}
		if !isDirectionAllowed(objMeta.PdoMapping, pdoMeta.Direction) {
			return fmt.Errorf("object %q (mapping %q) not mappable to PDO %q (direction %v)", objMeta.Name, objMeta.PdoMapping, pdoMeta.Name, pdoMeta.Direction)
		}
		if tracingEnabled {
			log.InfoContextf(ctx, "Object %q not in PDO defaults. Generating Dynamic Addition entry for PDO #x%04X", objMeta.Name, pdoIdx)
		}

		s.objectsToAdd = append(s.objectsToAdd, &dspb.ResolvedConfiguration_EbiPdoInstructions_ObjectAddition{
			PdoIndex:       pdoIdx,
			ObjectIndex:    addr.Index,
			ObjectSubIndex: addr.SubIndex,
			DataType:       objMeta.DataType,
			BitSize:        objMeta.BitSize,
			Name:           objMeta.Name,
		})
	} else if tracingEnabled {
		log.InfoContextf(ctx, "Object %q is already a default member of PDO #x%04X. No EBI addition needed.", objMeta.Name, pdoIdx)
	}

	return nil
}
