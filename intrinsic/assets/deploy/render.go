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

// Package render provides support for "rendering" an application proto into a
// workcell spec, ready for deployment to a workcell.
package render

import (
	"fmt"

	"intrinsic/kubernetes/workcell_spec/chartassignment"

	crcv1alpha1 "github.com/googlecloudrobotics/core/src/go/pkg/apis/apps/v1alpha1"
	"github.com/pkg/errors"
	"google.golang.org/protobuf/proto"
	kubeyaml "sigs.k8s.io/yaml"

	applicationpb "intrinsic/config/proto/application_go_proto"
	opmodepb "intrinsic/config/proto/operation_mode_go_proto"
	transferpb "intrinsic/kubernetes/workcell_spec/proto/transfer_go_proto"
	ripb "intrinsic/resources/proto/resource_instance_go_proto"
	rtrpb "intrinsic/resources/proto/resource_type_runtime_go_proto"
)

const (
	// maxRetryAttempts is the maximum number of times to retry populating
	// resource runtime data.
	maxRetryAttempts = 5
)

var (
	errOpModeUnspecified   = fmt.Errorf("operation mode unspecified")
	errOpModeNotRecognized = fmt.Errorf("operation mode not recognized")
)

// AddResourceChart adds a resource chart with the specified options into the
// workcell spec.
func AddResourceChart(spec *transferpb.WorkcellSpec, opts ResourceChartOptions) error {
	ca, err := resourceChart(opts)
	if err != nil {
		return errors.Wrap(err, "resourceChart")
	}

	b, err := kubeyaml.Marshal(ca)
	if err != nil {
		return errors.Wrap(err, "marshal chart assignment")
	}

	spec.Items = append(spec.GetItems(), &transferpb.WorkcellSpecItem{
		Item: &transferpb.WorkcellSpecItem_ChartAssignment{
			ChartAssignment: &transferpb.ChartAssignment{
				Yaml: b,
			},
		},
	})
	return nil
}

// SkillChartOptions specifies the Skills to add and how they're run.
type SkillChartOptions struct {
	Skills       []*SkillDeploymentRuntime
	Parent       string
	NumSkillPods int
}

// AddSkillChart creates a chart for the provided Skills and adds it to the
// workcell spec.
func AddSkillChart(spec *transferpb.WorkcellSpec, opts SkillChartOptions) error {
	ca, err := skillChart(opts)
	if err != nil {
		return errors.Wrap(err, "skillChart")
	}

	b, err := kubeyaml.Marshal(ca)
	if err != nil {
		return errors.Wrap(err, "marshal chart assignment")
	}

	spec.Items = append(spec.GetItems(),
		&transferpb.WorkcellSpecItem{
			Item: &transferpb.WorkcellSpecItem_ChartAssignment{
				ChartAssignment: &transferpb.ChartAssignment{
					Yaml: b,
				},
			},
		},
	)

	return nil
}

// IsSimulated returns whether the operation mode of an application indicates
// it is in simulation or not.  Returns an error if the result would be
// ambiguous.
func IsSimulated(app *applicationpb.Application) (bool, error) {
	switch app.GetOperationMode() {
	case opmodepb.OperationMode_SIMULATION:
		return true, nil
	case opmodepb.OperationMode_REAL_HARDWARE:
		return false, nil
	case opmodepb.OperationMode_OPERATION_MODE_UNSPECIFIED:
		return false, errOpModeUnspecified
	default:
		return false, fmt.Errorf("%w: got %v", errOpModeNotRecognized, app.GetOperationMode())
	}
}

// ApplicationParams combines the clients and IDs needed to render an application.
type ApplicationParams struct {
	// DefaultWorkcellSpec will be used as a base for the rendered workcell spec.
	DefaultWorkcellSpec *transferpb.WorkcellSpec
	// ResourceInstances holds all resource instances associated with this application.
	ResourceInstances []*ripb.ResourceInstance
	// ResourceTypes holds all resource types associated with this application,
	// keyed by type id version.
	ResourceTypes map[string]*rtrpb.ResourceTypeRuntime
	// SkillDeploymentRuntimes holds all skill deployment data and installation origins associated
	// with this application.
	SkillDeploymentRuntimes []*SkillDeploymentRuntime
	// AppDeploymentID specifies the application deployment ID.
	AppDeploymentID string
	// SolutionDeploymentID specifies the solution deployment ID.
	SolutionDeploymentID string
	// Simulated indicates whether the workcell is simulated.
	Simulated bool

	ClusterParams
	InitDataFilesParams
}

// WorkcellSpecFromApplication converts application parameters into a workcell spec that can be
// deployed with Transfer.ApplyWorkcellSpec(). Most importantly, the chart
// assignments in the application are modified in [updateChartAssignment] function.
func WorkcellSpecFromApplication(params *ApplicationParams) (*transferpb.WorkcellSpec, error) {
	spec := proto.Clone(params.DefaultWorkcellSpec).(*transferpb.WorkcellSpec)
	if spec == nil {
		spec = new(transferpb.WorkcellSpec)
	}

	appChart, err := chartassignment.AppName(spec)
	if err != nil {
		return nil, errors.Wrap(err, "AppName")
	}

	if err := chartassignment.UpdateWorkcellSpec(func(_ int, ca *crcv1alpha1.ChartAssignment) error {
		chartassignment.SetValue(ca, "simulated", params.Simulated)
		chartassignment.SetValue(ca, "app_deployment_id", params.AppDeploymentID)
		chartassignment.SetValue(ca, "solution_deployment_id", params.SolutionDeploymentID)

		return nil
	}, spec); err != nil {
		return nil, errors.Wrap(err, "update chart assignment")
	}

	if err := AddResourceChart(spec, ResourceChartOptions{
		Instances:           params.ResourceInstances,
		Types:               params.ResourceTypes,
		Parent:              appChart,
		Simulated:           params.Simulated,
		ClusterParams:       params.ClusterParams,
		InitDataFilesParams: params.InitDataFilesParams,
	}); err != nil {
		return nil, errors.Wrap(err, "add resource chart")
	}
	if err := AddSkillChart(spec, SkillChartOptions{
		Skills:       params.SkillDeploymentRuntimes,
		Parent:       appChart,
		NumSkillPods: params.ClusterParams.NumSkillPods,
	}); err != nil {
		return nil, errors.Wrap(err, "add skill chart")
	}

	return spec, nil
}
