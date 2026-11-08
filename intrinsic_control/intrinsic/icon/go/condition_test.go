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

package icon

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"google.golang.org/protobuf/testing/protocmp"

	conditiontypespb "intrinsic/icon/proto/v1/condition_types_go_proto"
)

func TestIsTrue(t *testing.T) {
	test := struct {
		stateVar string
		want     *conditiontypespb.Condition
	}{
		"a",
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_EQUAL,
					Value: &conditiontypespb.Comparison_BoolValue{
						BoolValue: true,
					},
				},
			},
		},
	}
	got := IsTrue(test.stateVar)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("IsTrue(%q) returned diff (-want +got):\n%s", test.stateVar, diff)
	}
}

func TestIsFalse(t *testing.T) {
	test := struct {
		stateVar string
		want     *conditiontypespb.Condition
	}{
		"a",
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_EQUAL,
					Value: &conditiontypespb.Comparison_BoolValue{
						BoolValue: false,
					},
				},
			},
		},
	}
	got := IsFalse(test.stateVar)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("IsFalse(%q) returned diff (-want +got):\n%s", test.stateVar, diff)
	}
}

func TestEqualBool(t *testing.T) {
	test := struct {
		stateVar string
		value    bool
		want     *conditiontypespb.Condition
	}{
		"a",
		true,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_EQUAL,
					Value: &conditiontypespb.Comparison_BoolValue{
						BoolValue: true,
					},
				},
			},
		},
	}
	got := EqualBool(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("EqualBool(%q, %t) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestEqualInt64(t *testing.T) {
	tests := []struct {
		name     string
		stateVar string
		v        int64
		want     *conditiontypespb.Condition
	}{{
		"EqualInt64(\"state_var\", 1)",
		"state_var",
		1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "state_var",
					Operation:         conditiontypespb.Comparison_EQUAL,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: 1,
					},
				},
			},
		},
	}}
	for _, test := range tests {
		got := EqualInt64(test.stateVar, test.v)
		if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("%s returned diff (-want +got):\n%s", test.name, diff)
		}
	}
}

func TestNotEqualInt64(t *testing.T) {
	tests := []struct {
		name     string
		stateVar string
		v        int64
		want     *conditiontypespb.Condition
	}{{
		"NotEqualInt64(\"state_var\", 1)",
		"state_var",
		1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "state_var",
					Operation:         conditiontypespb.Comparison_NOT_EQUAL,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: 1,
					},
				},
			},
		},
	}}
	for _, test := range tests {
		got := NotEqualInt64(test.stateVar, test.v)
		if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("%s returned diff (-want +got):\n%s", test.name, diff)
		}
	}
}

func TestApproxEqual(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		epsilon  float64
		want     *conditiontypespb.Condition
	}{
		"a",
		1.0,
		0.25,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_APPROX_EQUAL,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: 1.0,
					},
					MaxAbsError: 0.25,
				},
			},
		},
	}
	got := ApproxEqual(test.stateVar, test.value, test.epsilon)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("ApproxEqual(%q, %f, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, test.epsilon, diff)
	}
}

func TestApproxNotEqual(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		epsilon  float64
		want     *conditiontypespb.Condition
	}{
		"b",
		2.0,
		0.5,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "b",
					Operation:         conditiontypespb.Comparison_APPROX_NOT_EQUAL,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: 2.0,
					},
					MaxAbsError: 0.5,
				},
			},
		},
	}
	got := ApproxNotEqual(test.stateVar, test.value, test.epsilon)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("ApproxNotEqual(%q, %f, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, test.epsilon, diff)
	}
}

func TestGreaterThan(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		want     *conditiontypespb.Condition
	}{
		"a",
		1.1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_GREATER_THAN,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: 1.1,
					},
				},
			},
		},
	}
	got := GreaterThan(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("GreaterThan(%q, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestGreaterThanInt64(t *testing.T) {
	test := struct {
		stateVar string
		value    int64
		want     *conditiontypespb.Condition
	}{
		"a",
		1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_GREATER_THAN,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: 1,
					},
				},
			},
		},
	}
	got := GreaterThanInt64(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("GreaterThanInt64(%q, %d) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestGreaterThanOrEqual(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		want     *conditiontypespb.Condition
	}{
		"a",
		-1.1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_GREATER_THAN_OR_EQUAL,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: -1.1,
					},
				},
			},
		},
	}
	got := GreaterThanOrEqual(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("GreaterThanOrEqual(%q, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestGreaterThanOrEqualToInt64(t *testing.T) {
	test := struct {
		stateVar string
		value    int64
		want     *conditiontypespb.Condition
	}{
		"a",
		-1,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "a",
					Operation:         conditiontypespb.Comparison_GREATER_THAN_OR_EQUAL,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: -1,
					},
				},
			},
		},
	}
	got := GreaterThanOrEqualToInt64(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("GreaterThanOrEqualToInt64(%q, %d) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestLessThan(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		want     *conditiontypespb.Condition
	}{
		"b",
		2.2,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "b",
					Operation:         conditiontypespb.Comparison_LESS_THAN,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: 2.2,
					},
				},
			},
		},
	}
	got := LessThan(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("LessThan(%q, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestLessThanInt64(t *testing.T) {
	test := struct {
		stateVar string
		value    int64
		want     *conditiontypespb.Condition
	}{
		"b",
		2,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "b",
					Operation:         conditiontypespb.Comparison_LESS_THAN,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: 2,
					},
				},
			},
		},
	}
	got := LessThanInt64(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("LessThanInt64(%q, %d) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestLessThanOrEqual(t *testing.T) {
	test := struct {
		stateVar string
		value    float64
		want     *conditiontypespb.Condition
	}{
		"b",
		-2.0,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "b",
					Operation:         conditiontypespb.Comparison_LESS_THAN_OR_EQUAL,
					Value: &conditiontypespb.Comparison_DoubleValue{
						DoubleValue: -2.0,
					},
				},
			},
		},
	}
	got := LessThanOrEqual(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("LessThanOrEqual(%q, %f) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestLessThanOrEqualToInt64(t *testing.T) {
	test := struct {
		stateVar string
		value    int64
		want     *conditiontypespb.Condition
	}{
		"b",
		-2,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_Comparison{
				Comparison: &conditiontypespb.Comparison{
					StateVariableName: "b",
					Operation:         conditiontypespb.Comparison_LESS_THAN_OR_EQUAL,
					Value: &conditiontypespb.Comparison_Int64Value{
						Int64Value: -2,
					},
				},
			},
		},
	}
	got := LessThanOrEqualToInt64(test.stateVar, test.value)
	if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
		t.Errorf("LessThanOrEqualToInt64(%q, %d) returned diff (-want +got):\n%s", test.stateVar, test.value, diff)
	}
}

func TestAnyOf(t *testing.T) {
	tests := []struct {
		name       string // String of equivalent function invocation.
		conditions []*conditiontypespb.Condition
		want       *conditiontypespb.Condition
	}{{
		"AnyOf()",
		nil,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_ConjunctionCondition{
				ConjunctionCondition: &conditiontypespb.ConjunctionCondition{
					Operation: conditiontypespb.ConjunctionCondition_ANY_OF,
				},
			},
		},
	}, {
		"AnyOf(IsTrue(\"is_done\"), GreaterThan(\"time_elapsed\", 60))",
		[]*conditiontypespb.Condition{IsTrue("is_done"), GreaterThan("time_elapsed", 60)},
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_ConjunctionCondition{
				ConjunctionCondition: &conditiontypespb.ConjunctionCondition{
					Operation: conditiontypespb.ConjunctionCondition_ANY_OF,
					Conditions: []*conditiontypespb.Condition{{
						Condition: &conditiontypespb.Condition_Comparison{
							Comparison: &conditiontypespb.Comparison{
								StateVariableName: "is_done",
								Operation:         conditiontypespb.Comparison_EQUAL,
								Value: &conditiontypespb.Comparison_BoolValue{
									BoolValue: true,
								},
							},
						},
					}, {
						Condition: &conditiontypespb.Condition_Comparison{
							Comparison: &conditiontypespb.Comparison{
								StateVariableName: "time_elapsed",
								Operation:         conditiontypespb.Comparison_GREATER_THAN,
								Value: &conditiontypespb.Comparison_DoubleValue{
									DoubleValue: 60,
								},
							},
						},
					}},
				},
			},
		},
	}}
	for _, test := range tests {
		got := AnyOf(test.conditions...)
		if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("%s returned diff (-want +got):\n%s", test.name, diff)
		}
	}
}

func TestAllOf(t *testing.T) {
	tests := []struct {
		name       string // String of equivalent function invocation.
		conditions []*conditiontypespb.Condition
		want       *conditiontypespb.Condition
	}{{
		"AllOf()",
		nil,
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_ConjunctionCondition{
				ConjunctionCondition: &conditiontypespb.ConjunctionCondition{
					Operation: conditiontypespb.ConjunctionCondition_ALL_OF,
				},
			},
		},
	}, {
		"AllOf(IsTrue(\"is_done\"), AnyOf(GreaterThan(\"time_elapsed\", 60), LessThanOrEqual(\"distance_to_goal\", 1.5)))",
		[]*conditiontypespb.Condition{IsTrue("is_done"), AnyOf(GreaterThan("time_elapsed", 60), LessThanOrEqual("distance_to_goal", 1.5))},
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_ConjunctionCondition{
				ConjunctionCondition: &conditiontypespb.ConjunctionCondition{
					Operation: conditiontypespb.ConjunctionCondition_ALL_OF,
					Conditions: []*conditiontypespb.Condition{{
						Condition: &conditiontypespb.Condition_Comparison{
							Comparison: &conditiontypespb.Comparison{
								StateVariableName: "is_done",
								Operation:         conditiontypespb.Comparison_EQUAL,
								Value: &conditiontypespb.Comparison_BoolValue{
									BoolValue: true,
								},
							},
						},
					}, {
						Condition: &conditiontypespb.Condition_ConjunctionCondition{
							ConjunctionCondition: &conditiontypespb.ConjunctionCondition{
								Operation: conditiontypespb.ConjunctionCondition_ANY_OF,
								Conditions: []*conditiontypespb.Condition{{
									Condition: &conditiontypespb.Condition_Comparison{
										Comparison: &conditiontypespb.Comparison{
											StateVariableName: "time_elapsed",
											Operation:         conditiontypespb.Comparison_GREATER_THAN,
											Value: &conditiontypespb.Comparison_DoubleValue{
												DoubleValue: 60,
											},
										},
									},
								}, {
									Condition: &conditiontypespb.Condition_Comparison{
										Comparison: &conditiontypespb.Comparison{
											StateVariableName: "distance_to_goal",
											Operation:         conditiontypespb.Comparison_LESS_THAN_OR_EQUAL,
											Value: &conditiontypespb.Comparison_DoubleValue{
												DoubleValue: 1.5,
											},
										},
									},
								}},
							},
						},
					}},
				},
			},
		},
	}}
	for _, test := range tests {
		got := AllOf(test.conditions...)
		if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("%s returned diff (-want +got):\n%s", test.name, diff)
		}
	}
}

func TestNot(t *testing.T) {
	tests := []struct {
		name      string // String of equivalent function invocation.
		condition *conditiontypespb.Condition
		want      *conditiontypespb.Condition
	}{{
		"Not(IsTrue(\"is_done\"))",
		IsTrue("is_done"),
		&conditiontypespb.Condition{
			Condition: &conditiontypespb.Condition_NegatedCondition{
				NegatedCondition: &conditiontypespb.NegatedCondition{
					Condition: &conditiontypespb.Condition{
						Condition: &conditiontypespb.Condition_Comparison{
							Comparison: &conditiontypespb.Comparison{
								StateVariableName: "is_done",
								Operation:         conditiontypespb.Comparison_EQUAL,
								Value: &conditiontypespb.Comparison_BoolValue{
									BoolValue: true,
								},
							},
						},
					},
				},
			},
		},
	}}
	for _, test := range tests {
		got := Not(test.condition)
		if diff := cmp.Diff(test.want, got, protocmp.Transform()); diff != "" {
			t.Errorf("%s returned diff (-want +got):\n%s", test.name, diff)
		}
	}
}
