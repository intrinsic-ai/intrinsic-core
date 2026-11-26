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

package poserandomizer

import (
	"math"
	"math/rand"
	"testing"

	"gonum.org/v1/gonum/num/quat"
	"google.golang.org/protobuf/encoding/prototext"

	pointpb "intrinsic/math/proto/point_go_proto"
	quatpb "intrinsic/math/proto/quaternion_go_proto"
)

func compareFloatWithTolerance(x float64, y float64, tolerance float64) bool {
	return math.Abs(x-y) < tolerance
}

func compareFloat(x float64, y float64) bool {
	return compareFloatWithTolerance(x, y, 1e-4)
}

func comparePos(p *pointpb.Point, x float64, y float64, z float64) bool {
	return compareFloat(p.GetX(), x) && compareFloat(p.GetY(), y) && compareFloat(p.GetZ(), z)
}

func compareRot(r *quatpb.Quaternion, x float64, y float64, z float64, w float64) bool {
	return compareFloat(r.GetX(), x) && compareFloat(r.GetY(), y) && compareFloat(r.GetZ(), z) && compareFloat(r.GetW(), w)
}

func TestPositionDomains(t *testing.T) {
	rand.Seed(99)
	tests := []struct {
		name                    string
		positionDomain          PositionDomain
		generatedPointValidator func(*pointpb.Point) bool
	}{
		{
			name:           "position bounding box at origin",
			positionDomain: BoundingBoxPositionDomain(0, 0, 0, 0, 0, 0),
			generatedPointValidator: func(p *pointpb.Point) bool {
				return comparePos(p, 0, 0, 0)
			},
		},
		{
			name:           "position bounding box at specific point",
			positionDomain: BoundingBoxPositionDomain(0.1, 0.2, 0.3, 0.1, 0.2, 0.3),
			generatedPointValidator: func(p *pointpb.Point) bool {
				return comparePos(p, 0.1, 0.2, 0.3)
			},
		},
		{
			name:           "position bounding box with range",
			positionDomain: BoundingBoxPositionDomain(-0.1, -0.2, -0.3, 0.1, 0.2, 0.3),
			generatedPointValidator: func(p *pointpb.Point) bool {
				return p.GetX() >= -0.1 && p.GetX() <= 0.1 &&
					p.GetY() >= -0.2 && p.GetY() <= 0.2 &&
					p.GetZ() >= -0.3 && p.GetZ() <= 0.3
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			for i := 0; i < 10; i++ {
				p := test.positionDomain.Generate()
				if !test.generatedPointValidator(p) {
					t.Errorf("Point '%s' did not pass validator", prototext.Format(p))
				}
			}
		})
	}
}

func TestRotationDomains(t *testing.T) {
	rand.Seed(99)
	tests := []struct {
		name                   string
		rotationDomain         RotationDomain
		generatedQuatValidator func(*quatpb.Quaternion) bool
	}{
		{
			name:           "no rotation",
			rotationDomain: NoRotation(),
			generatedQuatValidator: func(q *quatpb.Quaternion) bool {
				return compareRot(q, 0, 0, 0, 1)
			},
		},
		{
			name:           "any rotation",
			rotationDomain: ArbitraryRotation(),
			generatedQuatValidator: func(q *quatpb.Quaternion) bool {
				x := q.GetX()
				y := q.GetY()
				z := q.GetZ()
				w := q.GetW()
				norm := x*x + y*y + z*z + w*w
				return math.Abs(norm-1.0) < 1e-6
			},
		},
		{
			name: "axially constrained rotation errors",
			rotationDomain: func() RotationDomain {
				_, err := AxiallyConstrainedRotation(0, 0, 0)
				if err == nil {
					t.Errorf("Expected axially constrained rotation with zero to produce error, got nil")
				}
				return NoRotation()
			}(),
			generatedQuatValidator: func(q *quatpb.Quaternion) bool { return true },
		},
		{
			name: "axially constrained rotation",
			rotationDomain: func() RotationDomain {
				d, err := AxiallyConstrainedRotation(0, 1, 0)
				if err != nil {
					t.Errorf("Expected valid axially constrained rotation, Got error: %v", err)
					return NoRotation()
				}
				return d
			}(),
			generatedQuatValidator: func(q *quatpb.Quaternion) bool {
				x := q.GetX()
				y := q.GetY()
				z := q.GetX()
				w := q.GetW()

				// In this case, X and Z should be zero, and Y and W should add to one.
				return math.Abs(x) < 1e-6 && math.Abs(z) < 1e-6 && math.Abs(y*y+w*w-1.0) < 1e-6
			},
		},
		{
			name: "interesting axially constrained rotation",
			rotationDomain: func() RotationDomain {
				d, err := AxiallyConstrainedRotation(1, 2, 3)
				if err != nil {
					t.Errorf("Expected valid axially constrained rotation, Got error: %v", err)
					return NoRotation()
				}
				return d
			}(),
			generatedQuatValidator: func(qpb *quatpb.Quaternion) bool {
				axisNorm := math.Sqrt(1*1 + 2*2 + 3*3)
				axisX := 1 / axisNorm
				axisY := 2 / axisNorm
				axisZ := 3 / axisNorm

				q := quat.Number{Real: qpb.GetW(), Imag: qpb.GetX(), Jmag: qpb.GetY(), Kmag: qpb.GetZ()}
				axisQuat := quat.Number{Real: 0, Imag: axisX, Jmag: axisY, Kmag: axisZ}
				result := quat.Mul(quat.Mul(q, axisQuat), quat.Conj(q))

				// Dot product of result and axis should be close to one.
				dot := func(a, b quat.Number) float64 {
					return math.Sqrt(a.Real*b.Real + a.Imag*b.Imag + a.Jmag*b.Jmag + a.Kmag*b.Kmag)
				}
				return math.Abs(dot(axisQuat, result)-1.0) < 1e-6
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			for i := 0; i < 10; i++ {
				q := test.rotationDomain.Generate()
				if !test.generatedQuatValidator(q) {
					t.Errorf("Quaternion '%s' did not pass validator", prototext.Format(q))
				}
			}
		})
	}
}
