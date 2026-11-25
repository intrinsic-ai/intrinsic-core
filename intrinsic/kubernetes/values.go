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

// Package values provides helpers for working with helm values.
package values

import (
	"archive/tar"
	"compress/gzip"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"reflect"
	"strconv"
	"strings"
	"time"

	"intrinsic/kubernetes/workcell_spec/repositorybasename"
	"intrinsic/production/imagepublisher"

	log "github.com/golang/glog"
	"github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/pkg/errors"
	helmchart "helm.sh/helm/v3/pkg/chart"
	kubeyaml "sigs.k8s.io/yaml"
)

const (
	featureOptionsOverridesKey = "feature_options_overrides"
	startedAtLayout            = "2006-01-02T15:04:05.000-07:00"

	valuesYAMLFile = "values.yaml"
)

// parseDefaultValues extracts the Helm values from within the ChartAssignment.
// These come from two places: values.yaml inside the Helm chart tarball and
// in the Chart Assignment directly (ca.Spec.Chart.Values).
func parseDefaultValues(ca *v1alpha1.ChartAssignment) (v1alpha1.ConfigValues, error) {
	values := make(v1alpha1.ConfigValues)
	base64Reader := base64.NewDecoder(base64.StdEncoding, strings.NewReader(ca.Spec.Chart.Inline))
	gzipReader, err := gzip.NewReader(base64Reader)
	if err != nil {
		return nil, errors.Wrap(err, "gzip.NewReader")
	}
	r := tar.NewReader(gzipReader)
	// Iterate over all files until we find the one with values.
	foundHelmChartValues := false
	for {
		h, err := r.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, errors.Wrap(err, "next file in tar")
		}
		if path.Base(h.Name) != valuesYAMLFile {
			continue
		}
		if err := ReadBytes(r, h.Size, &values); err != nil {
			return nil, errors.Wrapf(err, "reading %q", valuesYAMLFile)
		}
		foundHelmChartValues = true
		break
	}
	if !foundHelmChartValues {
		log.Warningf("%q missing in chart.", valuesYAMLFile)
	}
	if err := Merge(ca.Spec.Chart.Values, &values); err != nil {
		return nil, err
	}
	return values, nil
}

// validateFlagValues ensures that all flagValues match valid configuration options in defaultValues.
func validateFlagValues(name string, flagValues, defaultValues v1alpha1.ConfigValues) error {
	for key, flagValue := range flagValues {
		defaultValue, ok := defaultValues[key]
		if !ok {
			fmt.Printf("\x1b[1;33mWarning: value with label %q passed in --values does not match any value defined in values.yaml from the %q  chart. Typo?\x1b[0m\n\n", key, name)
			continue
		}
		flagType := reflect.TypeOf(flagValue)
		defaultType := reflect.TypeOf(defaultValue)
		if flagType != defaultType {
			return fmt.Errorf("value with label %q (%v) passed in --values has type %v, expected %v", key, flagValue, flagType, defaultType)
		}
	}
	return nil
}

// ValidateFlagValuesCA ensures that all values passed in --values match
// valid configuration options in valuesYAMLFile.
func ValidateFlagValuesCA(ca *v1alpha1.ChartAssignment, flagValues v1alpha1.ConfigValues) error {
	defaultValues, err := parseDefaultValues(ca)
	if err != nil {
		return err
	}
	if err := validateFlagValues(ca.Name, flagValues, defaultValues); err != nil {
		return err
	}
	return nil
}

// ValidateFlagValuesChart ensures that all values passed in --values match
// valid configuration options in valuesYAMLFile.
func ValidateFlagValuesChart(chart *helmchart.Chart, flagValues v1alpha1.ConfigValues) error {
	defaultValues := chart.Values
	if err := validateFlagValues(chart.Name(), flagValues, defaultValues); err != nil {
		return err
	}
	return nil
}

// ImageLists returns the lists of docker images to push for the given chart assignment.
// The paths are relative to the current working directory.
func ImageLists(ca *v1alpha1.ChartAssignment) []string {
	if ca.Spec.Chart.Values["image_lists"] == nil {
		return nil
	}
	var imageLists []string
	for _, list := range ca.Spec.Chart.Values["image_lists"].([]any) {
		imageLists = append(imageLists, imagepublisher.PathInRunfiles(list.(string)))
	}
	return imageLists
}

// ClearImageLists clears the image_lists value.
func ClearImageLists(ca *v1alpha1.ChartAssignment) {
	delete(ca.Spec.Chart.Values, "image_lists")
}

// LFSPaths returns the list of LFS paths for the given chart assignment.
func LFSPaths(ca *v1alpha1.ChartAssignment) []string {
	if ca.Spec.Chart.Values["lfs_paths"] == nil {
		return nil
	}
	var lfsPaths []string
	for _, path := range ca.Spec.Chart.Values["lfs_paths"].([]any) {
		lfsPaths = append(lfsPaths, path.(string))
	}
	return lfsPaths
}

// ClearLFSPaths clears the lfs_paths value.
func ClearLFSPaths(ca *v1alpha1.ChartAssignment) {
	delete(ca.Spec.Chart.Values, "lfs_paths")
}

// EmbeddedChartFiles returns the list of embedded charts for the given chart assignment.
// The paths are relative to the current working directory.
// TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
func EmbeddedChartFiles(ca *v1alpha1.ChartAssignment) []string {
	if ca.Spec.Chart.Values["embed_chart_files"] == nil {
		return nil
	}
	var embedChartFiles []string
	for _, list := range ca.Spec.Chart.Values["embed_chart_files"].([]any) {
		embedChartFiles = append(embedChartFiles, list.(string))
	}
	return embedChartFiles
}

// ClearEmbeddedChartFiles clears the embed_chart_files value.
// TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
func ClearEmbeddedChartFiles(ca *v1alpha1.ChartAssignment) {
	ca.Spec.Chart.Values["embed_chart_files"] = nil
}

func defaultProjectDomain(project string) string {
	return fmt.Sprintf("www.endpoints.%s.cloud.goog", project)
}

func getDomainForCluster(project string, clusterName string, cloudRoboticsConfig map[string]string) string {
	if cloudRoboticsDomain, ok := cloudRoboticsConfig["CLOUD_ROBOTICS_DOMAIN"]; ok && cloudRoboticsDomain != "" {
		// Cluster names for cloud clusters are slightly mangled.
		// If a cluster is used as additional region, to provide shorter paths from users to cloud to robot,
		// the cluster name will be "cloud<region>".
		// We use this information to build the right URLs for the cluster ingress rules.
		if strings.HasPrefix(clusterName, "cloud-") {
			return fmt.Sprintf("%s.%s", strings.SplitN(clusterName, "-", 2)[1], cloudRoboticsDomain)
		}

		return cloudRoboticsDomain
	}

	// The cloud robotics config didn't actually contain our target value!
	return defaultProjectDomain(project)
}

// ParseExtraValues parses the comma-separated YAML values passed via --values.
func ParseExtraValues(extraValues string) (v1alpha1.ConfigValues, error) {
	// Strip extraValues of any surrounding quotes.
	// We have seen this when running the lint test on Guitar (http://yaqs/413207464834498560).
	// err is nil on successful stripping, otherwise the return value should not be used.
	if u, err := strconv.Unquote(extraValues); err == nil {
		log.Infof("Stripped extraneous quotes around %q into %q", extraValues, u)
		extraValues = u
	}

	parsed := make(v1alpha1.ConfigValues)
	// Parse the passed values. This will verify that the format is a valid YAML
	// string. We have to wrap it in {} to support multiple values on one line (eg
	// "a: b, c: d"), otherwise we get "mapping values are not allowed in this
	// context".
	if err := Parse([]byte("{"+extraValues+"}"), &parsed); err != nil {
		return nil, errors.Wrap(err, "parsing --values")
	}
	return parsed, nil
}

// BuildValues assembles the Helm values from the various sources they can come from.
// It differentiates between default values (coming from the chart assignment or the wrapped tarball) and runtime values (specified as `extraValues` or dynamically set by inctl).
// It returns the runtime values.
func BuildValues(project string, clusterName string, location string, extraValues string, cloudRoboticsConfig map[string]string) (v1alpha1.ConfigValues, error) {
	runtimeValues := make(v1alpha1.ConfigValues)
	setCommonValues(project, clusterName, location, runtimeValues)
	runtimeValues["domain"] = getDomainForCluster(project, clusterName, cloudRoboticsConfig)

	parsedExtra, err := ParseExtraValues(extraValues)
	if err != nil {
		return nil, err
	}
	if err := Merge(parsedExtra, &runtimeValues); err != nil {
		return nil, errors.Wrap(err, "merging --values")
	}

	return runtimeValues, nil
}

// setCommonValues sets the values that are common across all apps but vary by deployment.
func setCommonValues(project string, clusterName string, location string, values v1alpha1.ConfigValues) {
	// LINT.IfChange
	// Use the ISO8601 date with milliseconds, which is parsable as absl::Time by the commandline flag
	// library.
	values["started_at"] = time.Now().Format(startedAtLayout)
	values["installed_by"] = os.Getenv("USER")
	values["robot"] = map[string]any{"name": clusterName}
	values["project"] = project
	values["domain"] = defaultProjectDomain(project)
	if location != "" {
		values["location"] = location
	}
	// LINT.ThenChange(//intrinsic/kubernetes/common-values.yaml)
}

// AddEmbeddedChart embeds a chart in values.
// TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
func AddEmbeddedChart(name string, data []byte, values *v1alpha1.ConfigValues) {
	if *values == nil {
		*values = make(v1alpha1.ConfigValues)
	}
	if _, ok := (*values)["embed_chart"]; !ok {
		(*values)["embed_chart"] = make(map[string]any)
	}
	if (*values)["embed_chart"] == nil {
		(*values)["embed_chart"] = make(map[string]any)
	}
	(*values)["embed_chart"].(map[string]any)[strings.ReplaceAll(name, "-", "_")] = base64.StdEncoding.EncodeToString(data)
}

// Parse parses values from YAML.
func Parse(data []byte, values *v1alpha1.ConfigValues) error {
	if err := kubeyaml.Unmarshal(data, values); err != nil {
		return errors.Wrap(err, "unmarshal values")
	}
	return nil
}

// ReadBytes reads `size` bytes from reader and parses them as values.
func ReadBytes(r io.Reader, size int64, values *v1alpha1.ConfigValues) error {
	data := make([]byte, size)
	if _, err := io.ReadFull(r, data); err != nil && err != io.EOF {
		return errors.Wrapf(err, "reading values")
	}
	if err := Parse(data, values); err != nil {
		return errors.Wrap(err, "unmarshal values")
	}
	return nil
}

// ReadFile reads values from a file.
func ReadFile(path string, values *v1alpha1.ConfigValues) error {
	file, err := os.Open(path)
	if err != nil {
		errors.Wrapf(err, "open %q", path)
	}
	defer file.Close()

	stat, err := file.Stat()
	if err != nil {
		return errors.Wrapf(err, "stat %q", path)
	}

	if err := ReadBytes(file, stat.Size(), values); err != nil {
		return errors.Wrapf(err, "read %q", path)
	}

	return nil
}

func asStringMap(v any) (map[string]any, bool) {
	switch m := v.(type) {
	case map[string]any:
		return m, true
	case v1alpha1.ConfigValues:
		return m, true
	default:
		return nil, false
	}
}

func mergeMaps(src, dst map[string]any) {
	for key, srcVal := range src {
		if srcMap, srcIsMap := asStringMap(srcVal); srcIsMap {
			if dstMap, dstIsMap := asStringMap(dst[key]); dstIsMap && dstMap != nil {
				mergeMaps(srcMap, dstMap)
				continue
			}
			cloned := make(map[string]any, len(srcMap))
			mergeMaps(srcMap, cloned)
			if _, isConfigValues := srcVal.(v1alpha1.ConfigValues); isConfigValues {
				dst[key] = v1alpha1.ConfigValues(cloned)
			} else {
				dst[key] = cloned
			}
			continue
		}
		dst[key] = srcVal
	}
}

// Merge merges `values` into `result`, matching Helm's deep-merge semantics
// (nested maps are merged recursively; scalars, slices, and zero values such as
// false/""/0 override existing entries).
func Merge(values v1alpha1.ConfigValues, result *v1alpha1.ConfigValues) error {
	if result == nil {
		return errors.New("merging into nil ConfigValues pointer")
	}
	if *result == nil {
		*result = make(v1alpha1.ConfigValues)
	}
	mergeMaps(values, *result)
	return nil
}

// MergeFromFile reads a single file of Helm values and merges them into `result`.
func MergeFromFile(path string, result *v1alpha1.ConfigValues) error {
	var values v1alpha1.ConfigValues
	if err := ReadFile(path, &values); err != nil {
		return err
	}
	if err := Merge(values, result); err != nil {
		return errors.Wrapf(err, "merging %q", path)
	}
	return nil
}

// ImageData contains the parsed data on images for generator applications.
// This is constructed from AssembleImages from image command line flags.
type ImageData struct {
	// Name is name given to the image within the chart.
	Name string
	// Path the image path, disambiguated from other images of the same name per
	// go/intrinsic-unique-gcr-basenames.
	Path string
	// Tag is tag or digest value to be used to identify the image version in
	// the image repository.  This will be prepended by `:` for tags or `@` for
	// digests.
	Tag string
	// ImageArchive is the path to the image tarball.
	ImageArchive string
}

// AssembleImages takes a list of image specifications of the format:
// image_name%/path/to/digest/file%/path/to/image/tarball
// and returns a list ImageData corresponding to the provided args.
func AssembleImages(args []string) ([]*ImageData, error) {
	images := make([]*ImageData, 0, len(args))
	for _, v := range args {
		parts := strings.SplitN(v, "%", 3)
		if len(parts) != 3 {
			return nil, fmt.Errorf("Expected image_name%%/path/to/digest/file%%/path/to/image/tarball, got %q", v)
		}
		image := parts[0]
		digestFile := parts[1]
		tarballPath := parts[2]
		digest, err := os.ReadFile(digestFile)
		if err != nil {
			return nil, errors.Wrapf(err, "reading %q", digestFile)
		}
		images = append(images, &ImageData{
			Name:         strings.ReplaceAll(image, "-", "_"),
			Path:         repositorybasename.Make(image, tarballPath),
			Tag:          fmt.Sprintf("@%s", digest),
			ImageArchive: tarballPath,
		})
	}
	return images, nil
}

// AssembleImageValues takes a list of image specifications of the format:
// image_name%/path/to/digest/file%/path/to/image/tarball
// and returns the `images` Helm values mapping expected by our templates.
func AssembleImageValues(args []string) (map[string]string, error) {
	images, err := AssembleImages(args)
	if err != nil {
		return nil, err
	}
	imagesValues := make(map[string]string)
	for _, i := range images {
		// keep the leading '/' since helm charts prepend the registry without one
		imagesValues[i.Name] = fmt.Sprintf("/%s%s", i.Path, i.Tag)
	}
	return imagesValues, nil
}

// SetFeatureOptions sets the deployment environment feature options into `values`.
// This relies on the project_feature_options map that is generated by valuesgen.go, and
// provides the overrides as a separate value in the ChartAssignment.
// SetFeatureOptions overrides any existing feature options and is idempotent.
func SetFeatureOptions(project, overrides string, values v1alpha1.ConfigValues) error {
	// Convert feature options overrides from YAML to JSON, so they can be used verbatim or parsed
	// by the Helm templates.
	overridesDict := make(map[string]any)
	if err := kubeyaml.Unmarshal([]byte("{"+overrides+"}"), &overridesDict); err != nil {
		return errors.Wrap(err, "kubeyaml.Unmarshal")
	}
	overridesJSON, err := json.Marshal(overridesDict)
	if err != nil {
		return errors.Wrap(err, "json.Marshal")
	}

	values[featureOptionsOverridesKey] = string(overridesJSON)
	return nil
}
