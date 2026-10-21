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

package ethercat

import (
	"context"
	"encoding/xml"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"

	log "github.com/golang/glog"
	"github.com/spf13/cobra"
	"golang.org/x/net/html/charset"
	"google.golang.org/grpc"
	"google.golang.org/grpc/status"
	"gopkg.in/xmlpath.v2"

	"intrinsic/assets/cmdutils"
	"intrinsic/assets/idutils"
	"intrinsic/util/proto/descriptor"

	dapb "intrinsic/assets/data/proto/v1/data_asset_go_proto"
	atpb "intrinsic/assets/proto/asset_type_go_proto"
	ipb "intrinsic/assets/proto/id_go_proto"
	iagrpcpb "intrinsic/assets/proto/installed_assets_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	metadatapb "intrinsic/assets/proto/metadata_go_proto"
	vpb "intrinsic/assets/proto/vendor_go_proto"
	esipb "intrinsic/icon/fieldbus/ethercat/device_service/v1/esi_go_proto"

	lrogrpcpb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	lropb "cloud.google.com/go/longrunning/autogen/longrunningpb"
	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	esiPackage                = "ai.intrinsic.ethercat.esi"
	esiVersion                = "1.0.0"
	multipleFilesWarningLimit = 20
	maxFileNameLength         = 40
)

var (
	validESIExtensions = []string{".xml", ".esi"}

	errInvalidOverrideFormat     = errors.New("invalid override format")
	errInvalidIDString           = errors.New("invalid ID string for override")
	errOverrideNotPrimaryFile    = errors.New("file specified in --override-id is not a primary file of any discovered bundle")
	errNoValidESIFiles           = errors.New("no valid ESI files found")
	errMultipleInfoFiles         = errors.New("bundle must contain exactly one EtherCATInfo file")
	errVendorNameMissing         = errors.New("could not extract vendor name from XML file")
	errInstallAssetFailed        = errors.New("could not install ESI asset")
	errWaitForInstallationFailed = errors.New("waiting for installation failed")
	errInstallationOpFailed      = errors.New("installation failed")
	errMalformedXML              = errors.New("malformed XML")
)

// esiFileInfo holds the content and metadata of an ESI file.
// This struct is used to cache information extracted from each ESI/XML file
// after initial parsing, preventing redundant file reads and XML parsing.
// The cached information is used during the bundling process to group related files.
type esiFileInfo struct {
	// content stores the full content of the ESI file as a UTF-8 encoded string.
	content string
	// isInfoFile is a boolean indicating whether this file is a primary EtherCAT Information file
	// (i.e., contains the root element "/EtherCATInfo").
	isInfoFile bool
	// references is a slice of strings containing the file paths referenced within this ESI file.
	// These references are used to group related ESI files into bundles.
	references []string
}

// getCommand returns the cobra command for the upload_esi command.
//
// Returns:
//   - *cobra.Command: The configured cobra command.
func getCommand() *cobra.Command {
	flags := cmdutils.NewCmdFlags()
	cmd := &cobra.Command{
		Use:   "upload_esi <file1 or folder1> [<file2 or folder2> ...]",
		Short: "Upload ESI files or folders containing ESI files as Data assets.",
		Args:  cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			return runUploadESI(cmd, args, flags)
		},
	}
	flags.SetCommand(cmd)
	flags.AddFlagsAddressClusterSolution()
	flags.AddFlagPolicy("data")
	flags.AddFlagsProjectOrg()
	cmd.Flags().StringSlice("override_id", []string{}, "Override generated asset ID for a bundle. Format: <primary_file_basename>=<package.name>")

	return cmd
}

// uploadParams holds the parameters required for uploading ESI bundles.
// It encapsulates the necessary clients, connection details, policies, and file information
// used by the `uploadBundles` function.
type uploadParams struct {
	// client is the gRPC client for interacting with the InstalledAssets service.
	client iagrpcpb.InstalledAssetsClient
	// conn is the gRPC client connection to the asset management service.
	conn *grpc.ClientConn
	// policy specifies the update policy to use when installing assets.
	policy iapb.UpdatePolicy
	// flags contains the command-line flags passed to the inctl command.
	flags *cmdutils.CmdFlags
	// fileInfos is a map caching metadata about each ESI file, keyed by file path.
	fileInfos map[string]*esiFileInfo
	// overrideIDs is a map of base filenames to asset IDs, used to override the generated asset IDs.
	overrideIDs map[string]string
}

// runUploadESI is the main function to upload ESI files.
//
// Parameters:
//   - cmd: The cobra command.
//   - args: The command-line arguments.
//   - flags: The command-line flags.
//
// Returns:
//   - An error if the upload fails.
func runUploadESI(cmd *cobra.Command, args []string, flags *cmdutils.CmdFlags) error {
	ctx := cmd.Context()

	policy, err := flags.GetFlagPolicy()
	if err != nil {
		return fmt.Errorf("get policy: %w", err)
	}

	overrideFlag, err := cmd.Flags().GetStringSlice("override_id")
	if err != nil {
		return fmt.Errorf("get override-id flag: %w", err)
	}
	overrideIDs := make(map[string]string)
	for _, override := range overrideFlag {
		parts := strings.SplitN(override, "=", 2)
		if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
			return fmt.Errorf("%w: %q, want <basename>=<package.name>", errInvalidOverrideFormat, override)
		}
		idStr := parts[1]
		lastDot := strings.LastIndex(idStr, ".")
		if lastDot == -1 || lastDot == len(idStr)-1 || lastDot == 0 {
			return fmt.Errorf("%w: for %q: %q, must be in format <package>.<name>", errInvalidIDString, parts[0], idStr)
		}
		overrideIDs[parts[0]] = idStr
	}

	ctx, conn, _, err := clientutilsDialClusterFromInctl(ctx, flags)
	if err != nil {
		return fmt.Errorf("dial cluster: %w", err)
	}
	defer conn.Close()

	client := iagrpcpb.NewInstalledAssetsClient(conn)

	files, results, err := validateInput(args)
	if err != nil {
		return fmt.Errorf("validate input: %w", err)
	}

	fileInfos, results := preprocessFiles(files, results)

	bundles, results := groupFilesIntoBundles(files, fileInfos, results)

	// Check that all overrides correspond to a primary file in some bundle.
	primaryFiles := make(map[string]bool)
	for _, bundle := range bundles {
		for _, file := range bundle {
			if info, ok := fileInfos[file]; ok && info.isInfoFile {
				primaryFiles[filepath.Base(file)] = true
			}
		}
	}
	for file := range overrideIDs {
		if !primaryFiles[file] {
			return fmt.Errorf("%w: %q", errOverrideNotPrimaryFile, file)
		}
	}

	if err := confirmUpload(files); err != nil {
		return fmt.Errorf("confirm upload: %w", err)
	}

	params := &uploadParams{
		client:      client,
		conn:        conn,
		policy:      policy,
		flags:       flags,
		fileInfos:   fileInfos,
		overrideIDs: overrideIDs,
	}
	results = uploadBundles(ctx, params, bundles, results)

	printResults(results, bundles, fileInfos)

	if errorCount := countErrors(results); errorCount > 0 {
		for _, e := range results {
			if e != nil {
				return fmt.Errorf("failed to upload %d files: %w", errorCount, e)
			}
		}
		return fmt.Errorf("failed to upload %d files", errorCount)
	}
	return nil
}

// hasValidESIExtension checks if a file path has a valid ESI extension.
//
// Parameters:
//   - path: The file path to check.
//
// Returns:
//   - bool: True if the path has a valid ESI extension, false otherwise.
func hasValidESIExtension(path string) bool {
	ext := filepath.Ext(path)
	for _, validExt := range validESIExtensions {
		if ext == validExt {
			return true
		}
	}
	return false
}

// validateInput validates the input arguments and returns a list of files to process.
//
// Parameters:
//   - args: The command-line arguments.
//
// Returns:
//   - []string: A list of valid file paths.
//   - map[string]error: A map of file paths to errors encountered during validation.
//   - error: An error if no valid ESI files are found.
func validateInput(args []string) ([]string, map[string]error, error) {
	results := make(map[string]error)
	fileSet := make(map[string]bool)

	// First iterate over the args to fill the files list.
	for _, arg := range args {
		info, err := os.Stat(arg)
		if err != nil {
			results[arg] = fmt.Errorf("could not stat %q: %w", arg, err)
			continue
		}
		if info.IsDir() {
			err := filepath.Walk(arg, func(path string, info os.FileInfo, err error) error {
				if err != nil {
					return err
				}
				if info.IsDir() {
					return nil
				}
				absPath, err := filepath.Abs(path)
				if err != nil {
					return fmt.Errorf("could not get absolute path for %q: %w", path, err)
				}
				if !hasValidESIExtension(absPath) {
					results[absPath] = fmt.Errorf("file %q has an invalid extension, should be one of %s", absPath, strings.Join(validESIExtensions, ", "))
					return nil
				}
				fileSet[absPath] = true
				return nil
			})
			if err != nil {
				results[arg] = fmt.Errorf("could not walk folder %q: %w", arg, err)
				continue
			}

		} else {
			absPath, err := filepath.Abs(arg)
			if err != nil {
				results[arg] = fmt.Errorf("could not get absolute path for %q: %w", arg, err)
				continue
			}
			if !hasValidESIExtension(absPath) {
				results[absPath] = fmt.Errorf("file %q has an invalid extension, should be one of %s", absPath, strings.Join(validESIExtensions, ", "))
				continue
			}
			fileSet[absPath] = true
		}
	}

	var files []string
	for absPath := range fileSet {
		files = append(files, absPath)
	}

	// If no files were provided or found, return an error.
	if len(files) == 0 {
		return nil, results, errNoValidESIFiles
	}
	return files, results, nil
}

// commonAncestor finds the common ancestor directory of a list of file paths.
//
// Parameters:
//   - paths: A slice of file paths.
//
// Returns:
//   - string: The common ancestor directory path.
func commonAncestor(paths []string) string {
	if len(paths) == 0 {
		return ""
	}
	sep := string(filepath.Separator)
	parts := strings.Split(filepath.Dir(paths[0]), sep)
	common := parts
	for _, path := range paths[1:] {
		parts = strings.Split(filepath.Dir(path), sep)
		var i int
		for i = 0; i < len(common) && i < len(parts) && common[i] == parts[i]; i++ {
		}
		common = common[:i]
	}
	return strings.Join(common, sep)
}

// preprocessFiles reads and parses all files to cache their content and metadata.
//
// Parameters:
//   - files: A slice of file paths to preprocess.
//   - results: A map of file paths to existing errors.
//
// Returns:
//   - map[string]*esiFileInfo: A map caching metadata about each ESI file.
//   - map[string]error: An updated map of file paths to errors, including new errors from preprocessing.
func preprocessFiles(files []string, results map[string]error) (map[string]*esiFileInfo, map[string]error) {
	fileInfos := make(map[string]*esiFileInfo)
	for _, file := range files {
		if _, exists := results[file]; exists {
			continue
		}
		content, err := readAndDecodeXMLFile(file)
		if err != nil {
			results[file] = err
			continue
		}
		refs, err := parseReferences(content)
		if err != nil {
			results[file] = err
			continue
		}
		fileInfos[file] = &esiFileInfo{
			content:    content,
			isInfoFile: isEtherCATInfo(content),
			references: refs,
		}
	}
	return fileInfos, results
}

// groupFilesIntoBundles groups files into bundles based on references in the ESI files.
//
// Parameters:
//   - files: A slice of file paths.
//   - fileInfos: A map caching metadata about each ESI file.
//   - results: A map of file paths to existing errors.
//
// Returns:
//   - [][]string: A slice of bundles, where each bundle is a slice of file paths.
//   - map[string]error: An updated map of file paths to errors, including new errors from bundling.
func groupFilesIntoBundles(files []string, fileInfos map[string]*esiFileInfo, results map[string]error) ([][]string, map[string]error) {
	ds := newDisjointSet(files)
	fileSet := make(map[string]bool)
	for _, file := range files {
		fileSet[file] = true
	}

	for _, file := range files {
		if info, ok := fileInfos[file]; ok {
			for _, ref := range info.references {
				refPath := filepath.Join(filepath.Dir(file), filepath.FromSlash(strings.ReplaceAll(ref, "\\", "/")))
				if _, ok := fileSet[refPath]; ok {
					ds.union(file, refPath)
				} else {
					log.Warningf("File %q references %q which is not in the list of files to upload", file, ref)
				}
			}
		}
	}

	// Group files by bundle using disjoint set
	bundles := make(map[string][]string)
	for _, file := range files {
		root := ds.find(file)
		bundles[root] = append(bundles[root], file)
	}

	var resultBundles [][]string
	for _, bundleFiles := range bundles {
		infoCount := 0
		var bundleErr error
		var errFile string
		for _, file := range bundleFiles {
			if info, ok := fileInfos[file]; ok && info.isInfoFile {
				infoCount++
			}
			if err, ok := results[file]; ok {
				bundleErr = err
				errFile = file
			}
		}

		if bundleErr != nil {
			err := fmt.Errorf("file %q in bundle is invalid: %w", errFile, bundleErr)
			for _, file := range bundleFiles {
				results[file] = err
			}
			continue
		}

		if infoCount == 0 {
			continue
		}
		if infoCount > 1 {
			err := fmt.Errorf("%w: found %d in files: %s", errMultipleInfoFiles, infoCount, strings.Join(bundleFiles, ", "))
			for _, file := range bundleFiles {
				results[file] = err
			}
			continue
		}
		resultBundles = append(resultBundles, bundleFiles)
	}

	return resultBundles, results
}

// buildDataAsset creates a data asset for a single bundle.
//
// Parameters:
//   - ctx: The context.
//   - bundleFiles: A slice of file paths in the bundle.
//   - fileInfos: A map caching metadata about each ESI file.
//   - overrideIDs: A map of base filenames to asset IDs for overrides.
//
// Returns:
//   - *dapb.DataAsset: The created DataAsset proto.
//   - string: The path of the primary file in the bundle.
//   - error: An error if the DataAsset cannot be built.
func buildDataAsset(ctx context.Context, bundleFiles []string, fileInfos map[string]*esiFileInfo, overrideIDs map[string]string) (*dapb.DataAsset, string, error) {
	var primaryFile string
	for _, file := range bundleFiles {
		if fileInfos[file].isInfoFile {
			primaryFile = file
			break
		}
	}

	vendorName, err := parseVendorName(fileInfos[primaryFile].content, primaryFile)
	if err != nil {
		return nil, primaryFile, fmt.Errorf("could not parse vendor name from primary file %q: %w", primaryFile, err)
	}

	var id *ipb.Id
	if idStr, ok := overrideIDs[filepath.Base(primaryFile)]; ok {
		lastDot := strings.LastIndex(idStr, ".")
		pkg := idStr[:lastDot]
		name := idStr[lastDot+1:]
		id, err = idutils.IDProtoFrom(pkg, name)
		if err != nil {
			return nil, primaryFile, fmt.Errorf("can't parse override id %q: %w", idStr, err)
		}
	} else {
		id, err = createID(vendorName, primaryFile)
		if err != nil {
			return nil, primaryFile, fmt.Errorf("could not create ID for bundle with primary file %q: %w", primaryFile, err)
		}
	}

	bundleProto := &esipb.EsiBundle{
		Files: make(map[string]*esipb.Esi),
	}
	baseDir := commonAncestor(bundleFiles)
	for _, file := range bundleFiles {
		relPath, err := filepath.Rel(baseDir, file)
		if err != nil {
			return nil, primaryFile, fmt.Errorf("could not get relative path for file %q: %w", file, err)
		}
		bundleProto.Files[relPath] = &esipb.Esi{Data: fileInfos[file].content}
	}

	da, err := createBundleDataAsset(ctx, bundleProto, primaryFile, vendorName, id)
	if err != nil {
		return nil, primaryFile, fmt.Errorf("could not create data asset: %w", err)
	}
	return da, primaryFile, nil
}

// uploadBundles uploads the ESI bundles as data assets.
//
// Parameters:
//   - ctx: The context.
//   - params: The upload parameters.
//   - bundles: A slice of bundles, where each bundle is a slice of file paths.
//   - results: A map of file paths to existing errors.
//
// Returns:
//   - map[string]error: An updated map of file paths to errors, including errors from the upload process.
func uploadBundles(ctx context.Context, params *uploadParams, bundles [][]string, results map[string]error) map[string]error {
	for _, bundleFiles := range bundles {
		da, primaryFile, err := buildDataAsset(ctx, bundleFiles, params.fileInfos, params.overrideIDs)
		if err != nil {
			errForBundle := fmt.Errorf("failed to build data asset for bundle with primary file %q: %w", primaryFile, err)
			for _, file := range bundleFiles {
				results[file] = errForBundle
			}
			continue
		}

		idStr, err := idutils.IDFromProto(da.GetMetadata().GetIdVersion().GetId())
		if err != nil {
			for _, file := range bundleFiles {
				results[file] = fmt.Errorf("invalid id for bundle with primary file %q: %w", primaryFile, err)
			}
			continue
		}
		log.InfoContextf(ctx, "Installing ESI bundle asset %q", idStr)
		op, err := installAsset(ctx, params.client, da, params.policy)
		if err != nil {
			for _, file := range bundleFiles {
				results[file] = err
			}
			continue
		}

		log.InfoContextf(ctx, "Awaiting completion of the bundle installation")
		err = waitForOperation(ctx, params.conn, op)
		for _, file := range bundleFiles {
			results[file] = err
		}
		if err == nil {
			log.InfoContextf(ctx, "Finished installing bundle %q", idStr)
		}
	}
	return results
}

// confirmUpload asks the user for confirmation if there are too many files to upload.
//
// Parameters:
//   - files: A slice of file paths to be uploaded.
//
// Returns:
//   - error: An error if the user aborts the upload.
func confirmUpload(files []string) error {
	fileCount := len(files)
	if fileCount > multipleFilesWarningLimit {
		fmt.Printf("You are about to upload %d files. Do you want to continue? [y/N] ", fileCount)
		var confirmation string
		fmt.Scanln(&confirmation)
		if !(confirmation == "y" || confirmation == "yes") {
			fmt.Println("Aborted.")
			return fmt.Errorf("upload aborted by user")
		}
	}
	return nil
}

// printResults prints the results of the upload.
//
// Parameters:
//   - results: A map of file paths to errors.
//   - bundles: A slice of bundles, where each bundle is a slice of file paths.
//   - fileInfos: A map caching metadata about each ESI file.
func printResults(results map[string]error, bundles [][]string, fileInfos map[string]*esiFileInfo) {
	fmt.Println("Summary:")
	processedFiles := make(map[string]bool)

	bundleEntry := func(file string, indentation int, err error, isOtherFile bool) {
		shortenedFile := file
		if len(file) > maxFileNameLength {
			shortenedFile = "..." + file[len(file)-maxFileNameLength+3:]
		}
		prefix := strings.Repeat(" ", indentation)
		filePadding := strings.Repeat(" ", maxFileNameLength-len(shortenedFile))
		if err != nil {
			status := "[FAILED] "
			fmt.Printf("  %s%s%s%s: %s\n", status, prefix, shortenedFile, filePadding, strconv.Quote(fmt.Sprintf("%v", err)))
		} else {
			if isOtherFile {
				fmt.Printf("         ↳ %s%s%s\n", prefix, shortenedFile, filePadding)
			} else {
				status := "[SUCCESS]"
				fmt.Printf("  %s%s%s%s\n", status, prefix, shortenedFile, filePadding)
			}
		}
	}

	for _, bundle := range bundles {
		var primaryFile string
		var otherFiles []string
		for _, file := range bundle {
			if fileInfos[file].isInfoFile {
				primaryFile = file
			} else {
				otherFiles = append(otherFiles, file)
			}
		}
		sort.Strings(otherFiles)

		err := results[primaryFile]
		bundleEntry(primaryFile, 0, err, false)
		processedFiles[primaryFile] = true

		for _, file := range otherFiles {
			bundleEntry(file, 0, err, true)
			processedFiles[file] = true
		}
	}

	for file, err := range results {
		if processedFiles[file] {
			continue
		}
		bundleEntry(file, 0, err, false)
	}
}

// countErrors counts the number of errors in the results map.
//
// Parameters:
//   - results: A map of file paths to errors.
//
// Returns:
//   - int: The number of errors.
func countErrors(results map[string]error) int {
	errorCount := 0
	for _, err := range results {
		if err != nil {
			errorCount++
		}
	}
	return errorCount
}

// toPackageName generates a string from `vendorName`, prepending `esiPackage`, such that the
// resulting string fulfills the following criteria:

//   - consists only of alphanumeric characters, underscores, and periods;
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character;
//   - follows each period with an alphabetic character;
//   - does not contain multiple underscores in a row.
//
// Parameters:
//   - vendorName: The name of the vendor.
//
// Returns:
//   - string: The generated package name.
//   - error: An error if the vendor name is invalid.
func toPackageName(vendorName string) (string, error) {
	pkg := esiPackage
	if !idutils.IsPackage(pkg) {
		return "", fmt.Errorf("package %q is not a valid package prefix", pkg)
	}
	if vendorName == "" {
		return "", fmt.Errorf("vendor name must not be empty")
	}
	vendorName, err := toName(vendorName)
	if err != nil {
		return "", fmt.Errorf("could not create package from vendor name %s: %w", vendorName, err)
	}

	pkg = fmt.Sprintf("%s.%s", pkg, vendorName)
	if !idutils.IsPackage(pkg) {
		return "", fmt.Errorf("Generated package %q is not a valid package", pkg)
	}
	return pkg, nil
}

// toName generates a string from `name`, such that the
// resulting string fulfills the following criteria:
//   - consists only of alphanumeric characters, underscores, and periods;
//   - begins with an alphabetic character;
//   - ends with an alphanumeric character;
//   - does not contain multiple underscores in a row.
//
// Parameters:
//   - name: The string to convert.
//
// Returns:
//   - string: The generated name.
//   - error: An error if the name is invalid.
func toName(name string) (string, error) {
	name, err := idutils.ToLabelNonReversible(name)
	if err != nil {
		return "", fmt.Errorf("could not create name from %s: %w", name, err)
	}
	// Replace dots and hyphens with underscores.
	name = idutils.FromLabel(name)
	name = strings.ReplaceAll(name, ".", "_")
	name = strings.ReplaceAll(name, "-", "_")
	// remove duplicate underscores.
	for strings.Contains(name, "__") {
		name = strings.ReplaceAll(name, "__", "_")
	}
	// Remove invalid characters at the beginning
	name = regexp.MustCompile(`^[^a-zA-Z]+`).ReplaceAllString(name, "")
	// Remove invalid characters at the end
	name = regexp.MustCompile(`[^a-zA-Z0-9]+$`).ReplaceAllString(name, "")

	if !idutils.IsName(name) {
		return "", fmt.Errorf("Generated name %q is not a valid name", name)
	}
	return name, nil
}

// createID creates an ID from the vendor name and the file name.
//
// Parameters:
//   - vendorName: The name of the vendor.
//   - file: The path to the ESI file.
//
// Returns:
//   - *ipb.Id: The generated ID.
//   - error: An error if the vendor name or file name are invalid.
func createID(vendorName string, file string) (*ipb.Id, error) {
	filename := filepath.Base(file)
	name, err := toName(filename)
	if err != nil {
		return nil, fmt.Errorf("could not create name from file %s: %w", filename, err)
	}
	name = idutils.FromLabel(name)

	pkg, err := toPackageName(vendorName)
	if err != nil {
		return nil, fmt.Errorf("could not create package from vendor name %s: %w", vendorName, err)
	}

	id, err := idutils.IDProtoFrom(pkg, name)
	if err != nil {
		return nil, fmt.Errorf("could not create id from package %s and name %s: %w", pkg, name, err)
	}
	return id, nil
}

// readAndDecodeXMLFile reads a file and returns its content as a utf-8 encoded string.
//
// Parameters:
//   - file: The path to the file.
//
// Returns:
//   - string: The content of the file as a utf-8 encoded string.
//   - error: An error if the file could not be read or decoded.
func readAndDecodeXMLFile(file string) (string, error) {
	content, err := os.ReadFile(file)
	if err != nil {
		return "", fmt.Errorf("could not read file %s: %w", file, err)
	}
	utf8Content, err := decodeToUTF8XML(string(content))
	if err != nil {
		return "", fmt.Errorf("could not convert file to utf8 %s: %w", file, err)
	}
	return utf8Content, nil
}

// decodeToUTF8XML creates an utf-8 encoded XML string from the input XML data.
//
// Parameters:
//   - input: The XML data as a string.
//
// Returns:
//   - string: The content of the file as a utf-8 encoded string.
//   - error: An error if the XML is malformed or encoding fails.
func decodeToUTF8XML(input string) (string, error) {
	decoder := xml.NewDecoder(strings.NewReader(input))
	decoder.CharsetReader = func(ch string, input io.Reader) (io.Reader, error) {
		r, err := charset.NewReaderLabel(ch, input)
		if err != nil {
			return nil, err
		}
		return r, nil
	}

	var buf strings.Builder
	encoder := xml.NewEncoder(&buf)

	for {
		tok, err := decoder.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			return "", fmt.Errorf("%w: %v", errMalformedXML, err)
		}

		// If the token is a process instruction, check if it contains an encoding declaration.
		// If it is, change the encoding to UTF-8 since we're converting the encoding to UTF-8 here.
		t, isProcInst := tok.(xml.ProcInst)
		if isProcInst && t.Target == "xml" {
			// ProcInst does not have attributes, the encoding is part of the data.
			if strings.Contains(string(t.Inst), `encoding="`) {
				t.Inst = []byte(regexp.MustCompile(`encoding="[^"]*"`).ReplaceAllString(string(t.Inst), `encoding="utf-8"`))
				tok = t

			}
			// Flush the encoder to make sure that the ProcInst is the first token.
			err := encoder.Flush()
			if err != nil {
				return "", fmt.Errorf("failed to flush XML encoder: %w", err)
			}
		}
		err = encoder.EncodeToken(tok)
		if err != nil {
			return "", fmt.Errorf("%w: %v", errMalformedXML, err)
		}
	}
	// Flush the encoder to make sure that all tokens are written.
	if err := encoder.Flush(); err != nil {
		return "", fmt.Errorf("failed to flush XML encoder: %w", err)
	}
	return buf.String(), nil
}

// parseReferences parses the references from an ESI file.
//
// Parameters:
//   - contentStr: The content of the ESI file.
//
// Returns:
//   - []string: A slice of referenced file paths.
//   - error: An error if the XML is malformed.
func parseReferences(contentStr string) ([]string, error) {
	root, err := xmlpath.Parse(strings.NewReader(contentStr))
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errMalformedXML, err)
	}

	references := make(map[string]bool)
	paths := []*xmlpath.Path{
		xmlpath.MustCompile("//InfoReference"),
		xmlpath.MustCompile("//DictionaryFile"),
		xmlpath.MustCompile("//DiagFile"),
	}

	for _, p := range paths {
		iter := p.Iter(root)
		for iter.Next() {
			ref := iter.Node().String()
			ext := filepath.Ext(ref)
			if ext == ".xml" || ext == ".esi" {
				references[ref] = true
			}
		}
	}

	var result []string
	for ref := range references {
		result = append(result, ref)
	}
	return result, nil
}

// isEtherCATInfo checks if the given XML content is an EtherCATInfo file.
//
// Parameters:
//   - contentStr: The XML content as a string.
//
// Returns:
//   - bool: True if the content is an EtherCATInfo file, false otherwise.
func isEtherCATInfo(contentStr string) bool {
	root, err := xmlpath.Parse(strings.NewReader(contentStr))
	if err != nil {
		return false
	}
	infoPath := xmlpath.MustCompile("/EtherCATInfo")
	_, ok := infoPath.String(root)
	return ok
}

// parseVendorName parses the vendor name from an ESI file.
//
// Parameters:
//   - contentStr: The content of the ESI file.
//   - file: The path to the ESI file.
//
// Returns:
//   - string: The vendor name.
//   - error: An error if the vendor name could not be parsed.
func parseVendorName(contentStr string, file string) (string, error) {
	root, err := xmlpath.Parse(strings.NewReader(contentStr))
	if err != nil {
		return "", fmt.Errorf("%w: file %s: %v", errMalformedXML, file, err)
	}
	vendorNamePath := xmlpath.MustCompile("/EtherCATInfo/Vendor/Name")
	vendorName, ok := vendorNamePath.String(root)
	if !ok {
		return "", fmt.Errorf("%w: %s", errVendorNameMissing, file)
	}
	return vendorName, nil
}

// createBundleDataAsset creates a DataAsset from an ESI bundle.
//
// Parameters:
//   - ctx: The context.
//   - bundle: The EsiBundle proto.
//   - primaryFile: The path to the primary ESI file.
//   - vendorName: The vendor name extracted from the primary file.
//   - id: The asset ID.
//
// Returns:
//   - *dapb.DataAsset: The created DataAsset.
//   - error: An error if the DataAsset creation fails.
func createBundleDataAsset(ctx context.Context, bundle *esipb.EsiBundle, primaryFile string, vendorName string, id *ipb.Id) (*dapb.DataAsset, error) {
	filename := filepath.Base(primaryFile)
	bundleAny, err := anypb.New(bundle)
	if err != nil {
		return nil, fmt.Errorf("could not marshal esi bundle: %v: %w", bundle, err)
	}

	da := &dapb.DataAsset{
		Metadata: &metadatapb.Metadata{
			IdVersion:   &ipb.IdVersion{Id: id},
			DisplayName: fmt.Sprintf("[ESI] %s - %s", vendorName, filename),
			AssetType:   atpb.AssetType_ASSET_TYPE_DATA,
			Vendor:      &vpb.Vendor{DisplayName: vendorName},
		},
		Data:              bundleAny,
		FileDescriptorSet: descriptor.FileDescriptorSetFrom(bundle),
	}
	return da, nil
}

// installAsset installs a DataAsset.
//
// Parameters:
//   - ctx: The context.
//   - client: The installed assets client.
//   - da: The DataAsset to install.
//   - policy: The update policy.
//
// Returns:
//   - *lropb.Operation: The operation.
//   - error: An error if the installation fails.
func installAsset(ctx context.Context, client iagrpcpb.InstalledAssetsClient, da *dapb.DataAsset, policy iapb.UpdatePolicy) (*lropb.Operation, error) {
	op, err := client.CreateInstalledAsset(ctx, &iapb.CreateInstalledAssetRequest{
		Policy: policy,
		Asset: &iapb.CreateInstalledAssetRequest_Asset{
			Variant: &iapb.CreateInstalledAssetRequest_Asset_Data{
				Data: da,
			},
		},
	})
	if err != nil {
		return nil, fmt.Errorf("%w: %v", errInstallAssetFailed, err)
	}
	return op, nil
}

// waitForOperation waits for an operation to complete.
//
// Parameters:
//   - ctx: The context.
//   - conn: The grpc connection.
//   - op: The operation to wait for.
//
// Returns:
//   - error: An error if the operation fails.
func waitForOperation(ctx context.Context, conn *grpc.ClientConn, op *lropb.Operation) error {
	op, err := lrogrpcpb.NewOperationsClient(conn).WaitOperation(ctx, &lropb.WaitOperationRequest{Name: op.GetName()})
	if err != nil {
		return fmt.Errorf("%w: %v", errWaitForInstallationFailed, err)
	}
	err = status.ErrorProto(op.GetError())
	if err != nil {
		return fmt.Errorf("%w: %v", errInstallationOpFailed, err)
	}
	return nil
}

// disjointSet is a data structure that keeps track of a set of elements partitioned into a number of disjoint (non-overlapping) subsets.
type disjointSet struct {
	parent map[string]string
	rank   map[string]int
}

// newDisjointSet creates a new disjoint set with the given elements.
//
// Parameters:
//   - elements: A slice of elements to initialize the disjoint set with.
//
// Returns:
//   - *disjointSet: The new disjoint set.
func newDisjointSet(elements []string) *disjointSet {
	ds := &disjointSet{
		parent: make(map[string]string),
		rank:   make(map[string]int),
	}
	for _, e := range elements {
		ds.parent[e] = e
		ds.rank[e] = 0
	}
	return ds
}

// find returns the representative of the set containing i.
//
// Parameters:
//   - i: The element to find the representative for.
//
// Returns:
//   - string: The representative of the set.
func (ds *disjointSet) find(i string) string {
	if ds.parent[i] == i {
		return i
	}
	ds.parent[i] = ds.find(ds.parent[i])
	return ds.parent[i]
}

// union merges the sets containing i and j.
//
// Parameters:
//   - i: The first element.
//   - j: The second element.
func (ds *disjointSet) union(i, j string) {
	rootI := ds.find(i)
	rootJ := ds.find(j)
	if rootI != rootJ {
		if ds.rank[rootI] < ds.rank[rootJ] {
			ds.parent[rootI] = rootJ
		} else if ds.rank[rootI] > ds.rank[rootJ] {
			ds.parent[rootJ] = rootI
		} else {
			ds.parent[rootJ] = rootI
			ds.rank[rootI]++
		}
	}
}

func init() {
	EtherCATCmd.AddCommand(getCommand())
}
