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

// assetinstancegen creates an InstanceConfig textproto message for an asset instance.
package main

import (
	"bytes"
	"flag"
	"fmt"
	"os"
	"strings"
	"text/template"

	"intrinsic/production/intrinsic"

	log "github.com/golang/glog"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	"google.golang.org/protobuf/types/known/emptypb"

	icpb "intrinsic/assets/proto/v1/instance_config_go_proto"

	_ "embed"
)

var (
	output               = flag.String("output", "", "Output textproto file path.")
	requiredNodeHostname = flag.String("required_node_hostname", "", "The node's hostname where asset is required to be run.")
	serviceConfig        = flag.String("service_config", "", "The asset's service configuration file path.")

	//go:embed instance_config.textproto.tmpl
	instanceConfigTemplateText string
	instanceConfigTemplate     = template.Must(template.New("instance_config").Funcs(template.FuncMap{
		"indent": indent,
	}).Parse(instanceConfigTemplateText))
)

func indent(spaces int, v string) string {
	pad := strings.Repeat(" ", spaces)
	lines := strings.Split(strings.TrimRight(v, "\n"), "\n")
	for i, l := range lines {
		if strings.TrimSpace(l) != "" {
			lines[i] = pad + l
		} else {
			lines[i] = ""
		}
	}
	return strings.Join(lines, "\n")
}

type ignoreUnknownAnys struct {
	*protoregistry.Types
}

func (r ignoreUnknownAnys) FindMessageByURL(url string) (protoreflect.MessageType, error) {
	mt, err := r.Types.FindMessageByURL(url)
	if err == nil {
		return mt, nil
	}
	return (&emptypb.Empty{}).ProtoReflect().Type(), nil
}

func (r ignoreUnknownAnys) FindMessageByName(name protoreflect.FullName) (protoreflect.MessageType, error) {
	mt, err := r.Types.FindMessageByName(name)
	if err == nil {
		return mt, nil
	}
	return (&emptypb.Empty{}).ProtoReflect().Type(), nil
}

func validateFull(full []byte, hasServiceConfig bool) error {
	opts := prototext.UnmarshalOptions{
		DiscardUnknown: true,
		Resolver:       ignoreUnknownAnys{protoregistry.GlobalTypes},
	}
	ic := &icpb.InstanceConfig{}
	if err := opts.Unmarshal(full, ic); err != nil {
		return fmt.Errorf("unable to parse generated instance config: %v", err)
	}
	if hasServiceConfig && ic.GetService().GetServiceConfig().GetTypeUrl() == "" {
		return fmt.Errorf("service config must specify a protobuf type URL, e.g. [type.googleapis.com/package.MessageName]")
	}
	return nil
}

func main() {
	intrinsic.Init()

	if *output == "" {
		log.Exitf("--output is required")
	}

	var serviceConfigContent string
	if *serviceConfig != "" {
		b, err := os.ReadFile(*serviceConfig)
		if err != nil {
			log.Exitf("could not read service config at %q: %v", *serviceConfig, err)
		}
		serviceConfigContent = string(b)
	}

	data := struct {
		ServiceConfig         string
		ScheduledNodeHostname string
	}{
		ServiceConfig:         serviceConfigContent,
		ScheduledNodeHostname: *requiredNodeHostname,
	}

	var b bytes.Buffer
	if err := instanceConfigTemplate.Execute(&b, &data); err != nil {
		log.Exitf("unable to execute template: %v", err)
	}

	if strings.TrimSpace(b.String()) == "" {
		b.Reset()
	} else if !bytes.HasSuffix(b.Bytes(), []byte("\n")) {
		b.WriteByte('\n')
	}

	if err := validateFull(b.Bytes(), *serviceConfig != ""); err != nil {
		log.Exitf("invalid instance config: %v\n%v", err, b.String())
	}

	if err := os.WriteFile(*output, b.Bytes(), 0644); err != nil {
		log.Exitf("could not write instance config to %q: %v", *output, err)
	}
}
