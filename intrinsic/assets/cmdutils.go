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

// Package cmdutils provides utils for asset inctl commands.
package cmdutils

import (
	"fmt"
	"slices"
	"strings"
	"time"

	"intrinsic/assets/platformlevelswitch"
	"intrinsic/assets/typeutils"
	"intrinsic/assets/viewutils"
	"intrinsic/tools/inctl/util/orgutil"

	"github.com/pkg/errors"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	"golang.org/x/exp/maps"

	atypepb "intrinsic/assets/proto/asset_type_go_proto"
	iapb "intrinsic/assets/proto/installed_assets_go_proto"
	viewpb "intrinsic/assets/proto/view_go_proto"
)

const (
	// keyAddress is the name of the address flag.
	keyAddress = "address"
	// keyAPIKey is the name of the arg to specify the api-key to use.
	keyAPIKey = "api_key"
	// keyAssetTypes is the name of the asset types flag.
	keyAssetTypes = "asset_types"
	// keyAuthUser is the name of the auth user flag.
	keyAuthUser = "auth_user"
	// keyAuthPassword is the name of the auth password flag.
	keyAuthPassword = "auth_password"
	// keyCatalogAddress is the name of the catalog address flag.
	keyCatalogAddress = "catalog_address"
	// keyCluster is the name of the cluster flag.
	keyCluster = "cluster"
	// keyDefault is the name of the default flag.
	keyDefault = "default"
	// keyDryRun is the name of the dry run flag.
	keyDryRun = "dry_run"
	// keyIgnoreExisting is the name of the flag to ignore AlreadyExists errors.
	keyIgnoreExisting = "ignore_existing"
	// keyImageUploadParallelism indicates how many layers of the image should be uploaded in parallel.
	keyImageUploadParallelism = "image_upload_parallelism"
	// keyOrgPrivate is the name of the org-private flag.
	keyOrgPrivate = "org_private"
	// keyOrganization is used as central flag name for passing an organization name to inctl.
	keyOrganization = orgutil.KeyOrganization
	// keyPolicy defines the flag used to specify the policy option when
	// interacting with the installed asset service.
	keyPolicy = "policy"
	// keyProcessingConcurrency is the name of the processing concurrency flag.
	keyProcessingConcurrency = "processing_concurrency"
	// keyProject is used as central flag name for passing a project name to inctl.
	keyProject = orgutil.KeyProject
	// KeyProvides is the name of the provided interfaces flag.
	KeyProvides = "provides"
	// keyRateBurst is the name of the rate burst flag.
	keyRateBurst = "rate_burst"
	// keyRateLimit is the name of the rate limit flag.
	keyRateLimit = "rate_limit"
	// keyRegistry is the name of the registry flag.
	keyRegistry = "registry"
	// keyReleaseNotes is the name of the release notes flag.
	keyReleaseNotes = "release_notes"
	// keySkipDirectUpload is boolean flag controlling direct upload behavior
	keySkipDirectUpload = "skip_direct_upload"
	// keySkipPrompts is the name of the flag to skip user prompts.
	keySkipPrompts = "skip_prompts"
	// keySolution is the name of the solution flag.
	keySolution = "solution"
	// keyTimeout is the name of the timeout flag.
	keyTimeout = "timeout"
	// keyVersion is the name of the version flag.
	keyVersion = "version"
	// keyView is the name of the view flag.
	keyView = "view"

	envPrefix = "intrinsic"
)

var policyMap = map[string]iapb.UpdatePolicy{
	"":                  iapb.UpdatePolicy_UPDATE_POLICY_UNSPECIFIED,
	"add_new_only":      iapb.UpdatePolicy_UPDATE_POLICY_ADD_NEW_ONLY,
	"update_unused":     iapb.UpdatePolicy_UPDATE_POLICY_UPDATE_UNUSED,
	"update_compatible": iapb.UpdatePolicy_UPDATE_POLICY_UPDATE_COMPATIBLE,
}

// CmdFlags abstracts interaction with inctl command flags.
type CmdFlags struct {
	cmd        *cobra.Command
	viperLocal *viper.Viper
}

// NewCmdFlags returns a new CmdFlags instance.
func NewCmdFlags() *CmdFlags {
	viperLocal := viper.New()
	viperLocal.SetEnvPrefix(envPrefix)

	return NewCmdFlagsWithViper(viperLocal)
}

// NewCmdFlagsWithViper returns a new CmdFlags instance with a custom Viper.
func NewCmdFlagsWithViper(viperLocal *viper.Viper) *CmdFlags {
	return &CmdFlags{cmd: nil, viperLocal: viperLocal}
}

// SetCommand sets the cobra Command to interact with.
//
// The command must be set before any flags are added.
func (cf *CmdFlags) SetCommand(cmd *cobra.Command) {
	cf.cmd = cmd
}

// AddFlagAssetTypes adds a flag for an optional list of asset types.
func (cf *CmdFlags) AddFlagAssetTypes(defaultTypes string) {
	types := typeutils.AllAssetTypes()
	names := make([]string, len(types))
	for i, t := range types {
		names[i] = typeutils.AssetTypeCodeName(t)
	}

	cf.OptionalString(keyAssetTypes, defaultTypes, fmt.Sprintf("A comma-separated list of asset types (choose from: %v).", strings.Join(names, ", ")))
}

// GetFlagAssetTypes gets the (enum) values of the asset types flag added by AddFlagAssetTypes.
func (cf *CmdFlags) GetFlagAssetTypes() ([]atypepb.AssetType, error) {
	assetTypesValue := cf.GetString(keyAssetTypes)
	if assetTypesValue == "" {
		return nil, nil
	}
	assetTypeNames := strings.Split(assetTypesValue, ",")
	assetTypes := make([]atypepb.AssetType, len(assetTypeNames))
	for i, name := range assetTypeNames {
		assetType := typeutils.AssetTypeFromCodeName(name)
		if assetType == atypepb.AssetType_ASSET_TYPE_UNSPECIFIED {
			return nil, fmt.Errorf("invalid asset type: %q", name)
		}
		assetTypes[i] = assetType
	}

	return assetTypes, nil
}



// AddFlagCatalogAddress adds a flag for the catalog address.
func (cf *CmdFlags) AddFlagCatalogAddress() {
	cf.OptionalString(keyCatalogAddress, "", "The address of the catalog service to use.")
}

// GetFlagCatalogAddress gets the value of the catalog address flag added by AddFlagCatalogAddress.
func (cf *CmdFlags) GetFlagCatalogAddress() string {
	return cf.GetString(keyCatalogAddress)
}



// AddFlagsCredentials adds args for specifying credentials.
func (cf *CmdFlags) AddFlagsCredentials() {
	cf.OptionalString(keyAPIKey, "", "The API key to use for authentication.")
}

// GetFlagsCredentials gets the values of the credential args.
func (cf *CmdFlags) GetFlagsCredentials() (apiKey string) {
	apiKey = cf.GetString(keyAPIKey)

	return apiKey
}

// AddFlagDefault adds a flag for marking a released asset as default.
func (cf *CmdFlags) AddFlagDefault(assetType string) {
	cf.OptionalBool(keyDefault, false, fmt.Sprintf(`Whether this %s version should be tagged as the default.
	Setting a version as default will unset the default status from any other version of this %s.
	An asset must have a default version for it to be appear when browsing the catalog.`, assetType, assetType))
}

// GetFlagDefault gets the value of the default flag added by AddFlagDefault.
func (cf *CmdFlags) GetFlagDefault() bool {
	return cf.GetBool(keyDefault)
}

// GetFlagDefaultIsSet returns whether the default flag was set.
func (cf *CmdFlags) GetFlagDefaultIsSet() bool {
	return cf.viperLocal.IsSet(keyDefault)
}

// AddFlagDryRun adds a flag for performing a dry run.
func (cf *CmdFlags) AddFlagDryRun() {
	cf.OptionalBool(keyDryRun, false, "Dry-run by validating but not performing any actions.")
}

// GetFlagDryRun gets the value of the dry run flag added by AddFlagDryRun.
func (cf *CmdFlags) GetFlagDryRun() bool {
	return cf.GetBool(keyDryRun)
}

// AddFlagIgnoreExisting adds a flag to ignore AlreadyExists errors.
func (cf *CmdFlags) AddFlagIgnoreExisting(assetType string) {
	cf.OptionalBool(keyIgnoreExisting, false, fmt.Sprintf("Ignore errors if the specified %s version already exists in the catalog.", assetType))
}

// GetFlagIgnoreExisting gets the value of the flag added by AddFlagIgnoreExisting.
func (cf *CmdFlags) GetFlagIgnoreExisting() bool {
	return cf.GetBool(keyIgnoreExisting)
}

// AddFlagImageUploadParallelism adds flag for modifying image upload parallelism.
func (cf *CmdFlags) AddFlagImageUploadParallelism(defVal int) {
	cf.OptionalInt(keyImageUploadParallelism, defVal, "The number of image layers uploaded in parallel.")
}

// GetFlagImageUploadParallelism returns number of image layers which should be uploaded in parallel.
func (cf *CmdFlags) GetFlagImageUploadParallelism() int {
	return cf.GetInt(keyImageUploadParallelism)
}

// AddFlagsAddressClusterSolution adds flags for the address, cluster, and solution when installing
// or working with installed assets.
func (cf *CmdFlags) AddFlagsAddressClusterSolution() {
	cf.OptionalString(keyAddress, "", "Internal flag to directly set the API server address. Normally, you should use --org instead, which tells inctl to connect via the cloud.")
	cf.optionalEnvString(keyCluster, "", "The target Kubernetes cluster ID. If you set this, you must not set --solution.")
	cf.optionalEnvString(keySolution, "", "The target solution. Must be running. If you set this, you must not set --cluster.")

	cf.cmd.MarkFlagsMutuallyExclusive(keyCluster, keySolution)
}

// GetFlagsAddressClusterSolution gets the values of the address, cluster, and solution flags added
// by AddFlagsAddressClusterSolution.
func (cf *CmdFlags) GetFlagsAddressClusterSolution() (string, string, string, error) {
	address := cf.GetString(keyAddress)
	cluster := cf.GetString(keyCluster)
	solution := cf.GetString(keySolution)

	if address == "" && cluster == "" && solution == "" {
		return "", "", "", fmt.Errorf("at least one of `--%s`, `--%s` or `--%s` must be set", keyAddress, keyCluster, keySolution)
	}
	// This matches these flags being marked as mutually exclusive above.  That
	// does not prevent two environment variables being provided or a
	// combination of flags and variables.  This is a fairly strict check, as
	// we probably want these flags to functionally behave as one where we
	// autodetect the type.  Which could probably be done with a clear order of
	// precedence (address before cluster before solution) without needing to
	// autodetect the kind.  If this is too strict, then we can override the
	// check in clientutils that triggers a lookup if solution is set.
	if cluster != "" && solution != "" {
		return "", "", "", fmt.Errorf("both `--%s=%q` and `--%s=%q` were provided by a flags and/or environment variables, which could be ambiguous", keyCluster, cluster, keySolution, solution)
	}

	return address, cluster, solution, nil
}

// AddFlagOrgPrivate adds a flag for marking a released asset as private to the organization.
func (cf *CmdFlags) AddFlagOrgPrivate() {
	cf.OptionalBool(keyOrgPrivate, false, `Whether this asset version should be private to the organization that owns it.
	Setting this to true will override all other permissions and will make this version of the asset
	visible to only the members of the organization that owns the asset`)
}

// GetFlagOrgPrivate gets the value of the org_private flag added by AddFlagOrgPrivate.
func (cf *CmdFlags) GetFlagOrgPrivate() bool {
	return cf.GetBool(keyOrgPrivate)
}

// GetFlagOrgPrivateIsSet returns whether the org_private flag was set.
func (cf *CmdFlags) GetFlagOrgPrivateIsSet() bool {
	return cf.viperLocal.IsSet(keyOrgPrivate)
}

// AddFlagPolicy adds a flag for the update policy.
func (cf *CmdFlags) AddFlagPolicy(assetType string) {
	cf.OptionalString(keyPolicy, "", fmt.Sprintf("The update policy to be used to install the %s. Can be: %v", assetType, maps.Keys(policyMap)))
}

// GetFlagPolicy gets the value of the policy flag.  This flag must be added manually by the
// utility.
func (cf *CmdFlags) GetFlagPolicy() (iapb.UpdatePolicy, error) {
	policy := cf.GetString(keyPolicy)
	if value, ok := policyMap[policy]; ok {
		return value, nil
	}

	return iapb.UpdatePolicy_UPDATE_POLICY_UNSPECIFIED, fmt.Errorf("%q provided for --%v is invalid; valid values are: %v", policy, keyPolicy, maps.Keys(policyMap))
}

// AddFlagOrganizationOptional adds an optional flag for the organization.
func (cf *CmdFlags) AddFlagOrganizationOptional() {
	cf.optionalEnvString(keyOrganization, "", "The Intrinsic organization to use.")
}

// GetFlagOrganization gets the value of the organization flag added by AddFlagOrganizationOptional.
func (cf *CmdFlags) GetFlagOrganization() string {
	return cf.GetString(keyOrganization)
}

// AddFlagsProjectOrg adds both the project and org flag, including the necessary handling.
func (cf *CmdFlags) AddFlagsProjectOrg() {
	// While WrapCmd returns the pointer to make it inline, it's modifying, so we can use it here.
	platformlevelswitch.WrapCmd(cf.cmd, cf.viperLocal)
	cf.MarkHidden(orgutil.KeyProject)
}

// AddFlagsProjectOrgOptional adds both the project and org flag as optional, including the necessary handling.
func (cf *CmdFlags) AddFlagsProjectOrgOptional() {
	// While WrapCmd returns the pointer to make it inline, it's modifying, so we can use it here.
	platformlevelswitch.WrapCmdOptional(cf.cmd, cf.viperLocal)
	cf.MarkHidden(orgutil.KeyProject)
}

// AddFlagProject adds a flag for the GCP project.
func (cf *CmdFlags) AddFlagProject() {
	cf.requiredEnvString(keyProject, "", "The Google Cloud Project (GCP) project to use.")
}

// AddFlagCatalogProjectOptional adds an optional flag for the GCP project to use for the catalog.
func (cf *CmdFlags) AddFlagCatalogProjectOptional() {
	cf.optionalEnvString(keyProject, "", "The Google Cloud Project (GCP) project to use for the catalog.")
}

// GetFlagProject gets the value of the project flag added by AddFlagProject.
func (cf *CmdFlags) GetFlagProject() string {
	return cf.GetString(keyProject)
}

// AddFlagProvides adds a flag for specifying provided interfaces.
func (cf *CmdFlags) AddFlagProvides() {
	cf.OptionalString(KeyProvides, "", "A comma-separated list of protocol-prefixed interfaces that assets must provide in order to be included in the output.")
}

// GetFlagProvides gets the value of the provides flag added by AddFlagProvides.
func (cf *CmdFlags) GetFlagProvides() ([]string, error) {
	providesValue := cf.GetString(KeyProvides)
	if providesValue == "" {
		return nil, nil
	}

	provides := strings.Split(providesValue, ",")
	for i, provide := range provides {
		provides[i] = strings.TrimSpace(provide)
	}

	return provides, nil
}

// AddFlagProcessingConcurrency adds a flag for the processing concurrency limit with a default value.
func (cf *CmdFlags) AddFlagProcessingConcurrency(defaultLimit int) {
	cf.OptionalInt(keyProcessingConcurrency, defaultLimit, "Maximum number of concurrent processing operations. Set <= 0 to disable.")
}

// GetFlagProcessingConcurrency gets the value of the processing concurrency flag added by AddFlagProcessingConcurrency.
func (cf *CmdFlags) GetFlagProcessingConcurrency() int {
	return cf.GetInt(keyProcessingConcurrency)
}

// AddFlagsRateLimit adds flags for gRPC connection rate limiting with default values.
func (cf *CmdFlags) AddFlagsRateLimit(defaultLimit float64, defaultBurst int) {
	cf.OptionalFloat64(keyRateLimit, defaultLimit, "Maximum gRPC requests per second. Set <= 0 to disable.")
	cf.OptionalInt(keyRateBurst, defaultBurst, "Maximum gRPC burst size.")
}

// GetFlagRateLimit gets the value of the rate limit flag added by AddFlagsRateLimit.
func (cf *CmdFlags) GetFlagRateLimit() float64 {
	return cf.GetFloat64(keyRateLimit)
}

// GetFlagRateBurst gets the value of the rate burst flag added by AddFlagsRateLimit.
func (cf *CmdFlags) GetFlagRateBurst() int {
	return cf.GetInt(keyRateBurst)
}

// AddFlagRegistry adds a flag for the registry when side-loading an asset.
func (cf *CmdFlags) AddFlagRegistry() {
	cf.optionalEnvString(keyRegistry, "", fmt.Sprint("The container registry address."))
}

// GetFlagRegistry gets the value of the registry flag added by AddFlagRegistry.
func (cf *CmdFlags) GetFlagRegistry() string {
	return cf.GetString(keyRegistry)
}

// AddFlagsRegistryAuthUserPassword adds flags for user/password authentication for a private
// container registry.
func (cf *CmdFlags) AddFlagsRegistryAuthUserPassword() {
	cf.OptionalString(keyAuthUser, "", "The username used to access the private container registry.")
	cf.OptionalString(keyAuthPassword, "", "The password used to authenticate private container registry access.")
	cf.cmd.MarkFlagsRequiredTogether(keyAuthUser, keyAuthPassword)
}

// GetFlagsRegistryAuthUserPassword gets the values of the user/password flags added by
// AddFlagsRegistryAuthUserPassword
func (cf *CmdFlags) GetFlagsRegistryAuthUserPassword() (string, string) {
	return cf.GetString(keyAuthUser), cf.GetString(keyAuthPassword)
}

// AddFlagReleaseNotes adds a flag for release notes.
func (cf *CmdFlags) AddFlagReleaseNotes(assetType string) {
	cf.OptionalString(keyReleaseNotes, "", fmt.Sprintf("Release notes for this version of the %s.", assetType))
}

// GetFlagReleaseNotes gets the value of the release notes flag added by AddFlagReleaseNotes.
func (cf *CmdFlags) GetFlagReleaseNotes() string {
	return cf.GetString(keyReleaseNotes)
}

// AddFlagSideloadStartTimeout adds a flag for the timeout when starting an asset.
func (cf *CmdFlags) AddFlagSideloadStartTimeout(assetType string) {
	cf.OptionalString(keyTimeout, "180s", fmt.Sprintf(`Maximum time to wait for the %s to
become available in the cluster after starting it. Can be set to any valid duration
(\"60s\", \"5m\", ...) or to \"0\" to disable waiting.`, assetType))
}

// GetFlagSideloadStartTimeout gets the value of the flag added by AddFlagSideloadStartTimeout.
func (cf *CmdFlags) GetFlagSideloadStartTimeout() (time.Duration, string, error) {
	timeoutStr := cf.GetString(keyTimeout)
	timeout, err := parseNonNegativeDuration(timeoutStr)
	if err != nil {
		return timeout, timeoutStr, errors.Wrapf(err, "invalid value passed for --%s", keyTimeout)
	}

	return timeout, timeoutStr, nil
}

// AddFlagSkipDirectUpload adds a flag for disabling direct upload to workcells
func (cf *CmdFlags) AddFlagSkipDirectUpload(assetType string) {
	usage := fmt.Sprintf("Skips direct upload of %s to workcell. Requires "+
		"external repository. (default false)\nCan be defined via the %s_%s "+
		"environment variable.", assetType, envPrefix, strings.ToUpper(keySkipDirectUpload))
	cf.OptionalBool(keySkipDirectUpload, false, usage)
	cf.cmd.PersistentFlags().Lookup(keySkipDirectUpload).Hidden = true
	cf.viperLocal.BindEnv(keySkipDirectUpload)
}

// GetFlagSkipDirectUpload gets the value of the flag added by AddFlagSkipDirectUpload
func (cf *CmdFlags) GetFlagSkipDirectUpload() bool {
	return cf.GetBool(keySkipDirectUpload)
}

// AddFlagSkipPrompts adds a flag for disabling user prompts.
func (cf *CmdFlags) AddFlagSkipPrompts() {
	cf.OptionalBool(keySkipPrompts, false, "True to skip user prompts.")
}

// GetFlagSkipPrompts gets the value of the flag added by AddFlagSkipPrompts
func (cf *CmdFlags) GetFlagSkipPrompts() bool {
	return cf.GetBool(keySkipPrompts)
}

// AddFlagVersion adds a flag for the asset version.
func (cf *CmdFlags) AddFlagVersion(assetType string) {
	cf.RequiredString(keyVersion, fmt.Sprintf("The %s version, in sem-ver format.", assetType))
}

// GetFlagVersion gets the value of the version flag added by AddFlagVersion.
func (cf *CmdFlags) GetFlagVersion() string {
	return cf.GetString(keyVersion)
}

// AddFlagView adds a flag for the asset view.
func (cf *CmdFlags) AddFlagView() {
	shortStrings := make([]string, 0, len(viewpb.AssetViewType_value))
	for _, v := range viewpb.AssetViewType_value {
		shortStrings = append(shortStrings, viewutils.ShortStringFromEnum(viewpb.AssetViewType(v)))
	}
	slices.Sort(shortStrings)
	cf.OptionalString(keyView, "", fmt.Sprintf("The view of the asset to return. Can be: %v", shortStrings))
}

// GetFlagView gets the value of the view flag added by AddFlagView.
func (cf *CmdFlags) GetFlagView() (viewpb.AssetViewType, error) {
	return viewutils.EnumFromShortString(cf.GetString(keyView))
}

// String adds a new string flag.
func (cf *CmdFlags) String(name string, value string, usage string) {
	cf.cmd.PersistentFlags().String(name, value, usage)
	cf.viperLocal.BindPFlag(name, cf.cmd.PersistentFlags().Lookup(name))
}

// RequiredString adds a new required string flag.
func (cf *CmdFlags) RequiredString(name string, usage string) {
	cf.String(name, "", fmt.Sprintf("(required) %s", usage))
	cf.cmd.MarkPersistentFlagRequired(name)
}

// OptionalString adds a new optional string flag.
func (cf *CmdFlags) OptionalString(name string, value string, usage string) {
	cf.String(name, value, fmt.Sprintf("(optional) %s", usage))
}

// requiredEnvString adds a new required string flag that is bound to the corresponding ENV
// variable.
func (cf *CmdFlags) requiredEnvString(name string, value string, usage string) {
	envVarName := strings.ToUpper(fmt.Sprintf("%s_%s", envPrefix, name))
	cf.envString(name, value, fmt.Sprintf("%s\nRequired unless %s environment variable is defined.", usage, envVarName))

	if cf.GetString(name) == "" {
		cf.cmd.MarkPersistentFlagRequired(name)
	}
}

// optionalEnvString adds a new optional string flag that is bound to the corresponding ENV
// variable.
func (cf *CmdFlags) optionalEnvString(name string, value string, usage string) {
	envVarName := strings.ToUpper(fmt.Sprintf("%s_%s", envPrefix, name))
	cf.envString(name, value, fmt.Sprintf("%s\nCan be defined via the %s environment variable.", usage, envVarName))
}

// GetString gets the value of a string flag.
func (cf *CmdFlags) GetString(name string) string {
	return cf.viperLocal.GetString(name)
}

// Bool adds a new bool flag.
func (cf *CmdFlags) Bool(name string, value bool, usage string) {
	cf.cmd.PersistentFlags().Bool(name, value, usage)
	cf.viperLocal.BindPFlag(name, cf.cmd.PersistentFlags().Lookup(name))
}

// OptionalBool adds a new optional bool flag.
func (cf *CmdFlags) OptionalBool(name string, value bool, usage string) {
	cf.Bool(name, value, fmt.Sprintf("(optional) %s", usage))
}

// GetBool gets the value of a bool flag.
func (cf *CmdFlags) GetBool(name string) bool {
	return cf.viperLocal.GetBool(name)
}

// Int adds a new int flag.
func (cf *CmdFlags) Int(name string, value int, usage string) {
	cf.cmd.PersistentFlags().Int(name, value, usage)
	cf.viperLocal.BindPFlag(name, cf.cmd.PersistentFlags().Lookup(name))
}

// OptionalInt adds a new optional int flag.
func (cf *CmdFlags) OptionalInt(name string, value int, usage string) {
	cf.Int(name, value, fmt.Sprintf("(optional) %s", usage))
}

// GetInt gets the value of an int flag.
func (cf *CmdFlags) GetInt(name string) int {
	return cf.viperLocal.GetInt(name)
}

// Float64 adds a new float64 flag.
func (cf *CmdFlags) Float64(name string, value float64, usage string) {
	cf.cmd.PersistentFlags().Float64(name, value, usage)
	cf.viperLocal.BindPFlag(name, cf.cmd.PersistentFlags().Lookup(name))
}

// OptionalFloat64 adds a new optional float64 flag.
func (cf *CmdFlags) OptionalFloat64(name string, value float64, usage string) {
	cf.Float64(name, value, fmt.Sprintf("(optional) %s", usage))
}

// GetFloat64 gets the value of a float64 flag.
func (cf *CmdFlags) GetFloat64(name string) float64 {
	return cf.viperLocal.GetFloat64(name)
}

// StringSlice adds a new string slice flag.
func (cf *CmdFlags) StringSlice(name string, value []string, usage string) {
	cf.cmd.PersistentFlags().StringSlice(name, value, usage)
	cf.viperLocal.BindPFlag(name, cf.cmd.PersistentFlags().Lookup(name))
}

// GetStringSlice gets the value of a string slice flag, splitting elements by spaces.
func (cf *CmdFlags) GetStringSlice(name string) []string {
	raw := cf.viperLocal.GetStringSlice(name)
	var result []string
	for _, item := range raw {
		// strings.Fields splits the string around one or more white space characters.
		spaceSeparated := strings.Fields(item)
		result = append(result, spaceSeparated...)
	}
	return result
}

// IsSet checks if value of given flag was set on command line.
// Allows to check if value is coming from user, or is default value.
func (cf *CmdFlags) IsSet(name string) bool {
	return cf.viperLocal.IsSet(name)
}

func (cf *CmdFlags) envString(name string, value string, usage string) {
	cf.String(name, value, usage)
	cf.viperLocal.BindEnv(name)
}

func parseNonNegativeDuration(durationStr string) (time.Duration, error) {
	duration, err := time.ParseDuration(durationStr)
	if err != nil {
		return 0, fmt.Errorf("parsing duration: %w", err)
	}
	if duration < 0 {
		return 0, fmt.Errorf("duration must not be negative, but got %q", durationStr)
	}
	return duration, nil
}

// MarkHidden marks all flags in the list as hidden for the given command
func (cf *CmdFlags) MarkHidden(flagsToHide ...string) {
	flags := cf.cmd.PersistentFlags()
	for _, flagName := range flagsToHide {
		flag := flags.Lookup(flagName)
		if flag != nil {
			flag.Hidden = true
		}
	}
}
