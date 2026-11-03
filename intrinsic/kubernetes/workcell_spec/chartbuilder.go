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

// Package chartbuilder provides a Builder for making helm charts
package chartbuilder

import (
	// safearchive/tar only required for traversing tar archives
	"archive/tar" // NOLINT
	"bytes"
	"compress/gzip"
	"io"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strings"

	template "github.com/google/safetext/yamltemplate"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v3"
)

// ChartBuilder provides a Builder for making helm charts
type ChartBuilder struct {
	chartName  string
	gzipWriter *gzip.Writer
	tarWriter  *tar.Writer
}

var chartYamlTemplate = template.Must(template.New("").Parse(`apiVersion: v1
name: {{ .Name }}
version: {{ .Version }}
# Linter expects an icon.
icon: https://google.com/icon.png
`))

var forbidden = regexp.MustCompile(`[{}]`)

func sanitize(v string) string {
	beforeNL, _, _ := strings.Cut(v, "\n")
	return forbidden.ReplaceAllString(beforeNL, "")
}

func generateChartYaml(name, version string) (*bytes.Buffer, error) {
	var data bytes.Buffer
	if err := chartYamlTemplate.Execute(&data, map[string]string{
		"Name":    sanitize(name),
		"Version": sanitize(version),
	}); err != nil {
		return nil, err
	}
	return &data, nil
}

// AddChartYaml generates and adds the Chart.yaml file to the chart.
func (cb *ChartBuilder) AddChartYaml(name, version string) error {
	data, err := generateChartYaml(name, version)
	if err != nil {
		return err
	}
	header := &tar.Header{
		Name: "Chart.yaml",
		Size: int64(data.Len()),
		Mode: 0o444,
	}

	if err := cb.addEntry(header, data); err != nil {
		return err
	}

	return nil
}

// AddFileFromReader adds a file with the given directory, filename, and content from r to the chart.
func (cb *ChartBuilder) AddFileFromReader(dir, filename string, r io.Reader) error {
	b, err := io.ReadAll(r)
	if err != nil {
		return errors.Wrap(err, "reading file content")
	}

	header := &tar.Header{
		Name: path.Join(dir, filename),
		Size: int64(len(b)),
		Mode: 0o444,
	}

	if err := cb.addEntry(header, bytes.NewReader(b)); err != nil {
		return err
	}

	return nil
}

// AddValuesYaml generates and adds the values.yaml file to the chart.
func (cb *ChartBuilder) AddValuesYaml(values map[string]any) error {
	b, err := yaml.Marshal(values)
	if err != nil {
		return err
	}
	data := bytes.NewBuffer(b)

	header := &tar.Header{
		Name: "values.yaml",
		Size: int64(data.Len()),
		Mode: 0o444,
	}

	if err := cb.addEntry(header, data); err != nil {
		return err
	}

	return nil
}

// AddFile adds a file with the given path to the chart.
func (cb *ChartBuilder) AddFile(dir, path string) error {
	file, err := os.Open(path)
	if err != nil {
		return err
	}
	defer file.Close()

	// create a tar-header from the FileInfo data
	info, err := file.Stat()
	if err != nil {
		return err
	}
	header, err := tar.FileInfoHeader(info, info.Name())
	if err != nil {
		return err
	}
	// overwrite target dir
	header.Name = filepath.Join(dir, filepath.Base(path))

	if err := cb.addEntry(header, file); err != nil {
		return err
	}
	return nil
}

// addEntry adds data from header and reader to the chart.
func (cb *ChartBuilder) addEntry(header *tar.Header, src io.Reader) error {
	// Make sure all entries are places inside a folder with the name of the chart
	header.Name = filepath.Join(cb.chartName, header.Name)

	// write file header to the tar archive
	if err := cb.tarWriter.WriteHeader(header); err != nil {
		return err
	}
	// copy the file data to the tar archive
	if _, err := io.Copy(cb.tarWriter, src); err != nil {
		return err
	}
	return nil
}

// Build generates the helm-chart file.
func (cb *ChartBuilder) Build() {
	cb.tarWriter.Close()
	cb.gzipWriter.Close()
}

// New returns a builder for constructing a helm chart.
func New(w io.Writer, name, version string) (*ChartBuilder, error) {
	gw := gzip.NewWriter(w)
	tw := tar.NewWriter(gw)

	cb := &ChartBuilder{
		chartName:  name,
		gzipWriter: gw,
		tarWriter:  tw,
	}

	if err := cb.AddChartYaml(name, version); err != nil {
		return nil, errors.Wrap(err, "adding Chart.yaml")
	}

	return cb, nil
}
