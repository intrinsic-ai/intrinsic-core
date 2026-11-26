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

// Package poserandomizer allows the creation of randomized Pose protos.
//
// The pose protos are generated based on a set of geometric constraints
// specified in the spawner resource configuration.
package poserandomizer

import (
	"math"
	"math/rand"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	pointpb "intrinsic/math/proto/point_go_proto"
	posepb "intrinsic/math/proto/pose_go_proto"
	quatpb "intrinsic/math/proto/quaternion_go_proto"
)

type vector3 struct {
	x, y, z float64
}

func addVec(v1 vector3, v2 vector3) vector3 {
	return vector3{x: v1.x + v2.x, y: v1.y + v2.y, z: v1.z + v2.z}
}

func subVec(v1 vector3, v2 vector3) vector3 {
	return vector3{x: v1.x - v2.x, y: v1.y - v2.y, z: v1.z - v2.z}
}

func vecFromPointProto(p *pointpb.Point) vector3 {
	if p == nil {
		return vector3{x: 0, y: 0, z: 0}
	}
	return vector3{x: p.GetX(), y: p.GetY(), z: p.GetZ()}
}

// A PositionDomain is a randomization domain that produces three-dimensional
// positions. It is not stateful and will use the "math/rand" package for
// uniform distribution of floating point values.
type PositionDomain interface {
	Generate() *pointpb.Point
}

type positionEqualityDomain struct {
	offset vector3
}

func (d positionEqualityDomain) Generate() *pointpb.Point {
	return &pointpb.Point{
		X: d.offset.x,
		Y: d.offset.y,
		Z: d.offset.z,
	}
}

type positionBoundingBoxDomain struct {
	maxPoint vector3
	minPoint vector3
}

func (d positionBoundingBoxDomain) Generate() *pointpb.Point {
	delta := subVec(d.maxPoint, d.minPoint)
	return &pointpb.Point{
		X: d.minPoint.x + rand.Float64()*delta.x,
		Y: d.minPoint.y + rand.Float64()*delta.y,
		Z: d.minPoint.z + rand.Float64()*delta.z,
	}
}

type quaternion struct {
	x, y, z, w float64
}

// A RotationDomain is a randomization domain that produces three-dimensional
// orientations.
type RotationDomain interface {
	Generate() *quatpb.Quaternion
}

type rotationEqualityDomain struct {
	offset quaternion
}

func (d rotationEqualityDomain) Generate() *quatpb.Quaternion {
	return &quatpb.Quaternion{
		X: d.offset.x,
		Y: d.offset.y,
		Z: d.offset.z,
		W: d.offset.w,
	}
}

type rotationBallDomain struct{}

func (d rotationBallDomain) Generate() *quatpb.Quaternion {
	// Sample normal distribution. If we assume uniform values in the given range
	// then, when normalized, these vectors this would create a uniform sampling
	// of the N-d ball surface:
	// https://mathworld.wolfram.com/HyperspherePointPicking.html
	var x, y, z, w float64
	norm := 0.0
	for math.Abs(norm) < 1e-6 {
		x = rand.NormFloat64()
		y = rand.NormFloat64()
		z = rand.NormFloat64()
		w = rand.NormFloat64()
		norm = math.Sqrt(x*x + y*y + z*z + w*w)
	}
	return &quatpb.Quaternion{
		X: x / norm,
		Y: y / norm,
		Z: z / norm,
		W: w / norm,
	}
}

type rotationAxisDomain struct {
	axis vector3
}

func (d rotationAxisDomain) Generate() *quatpb.Quaternion {
	angle := math.Pi * rand.Float64()
	return &quatpb.Quaternion{
		X: d.axis.x * math.Sin(angle),
		Y: d.axis.y * math.Sin(angle),
		Z: d.axis.z * math.Sin(angle),
		W: math.Cos(angle),
	}
}

// A PoseRandomizer is a structure that contains the sole ability to generate
// random poses, both position and orientation, based on a set of initial
// constraints as they were passed to the `Create` method.
type PoseRandomizer struct {
	position PositionDomain
	rotation RotationDomain
}

func compareRotEq(r *quatpb.Quaternion, x float64, y float64, z float64, w float64) bool {
	return r.GetX() == x && r.GetY() == y && r.GetZ() == z && r.GetW() == w
}

// BoundingBoxPositionDomain is a PositionDomain that uniformly generates points
// within the extents provided by the axis-aligned bounding box.
func BoundingBoxPositionDomain(
	xmin float64, ymin float64, zmin float64,
	xmax float64, ymax float64, zmax float64,
) PositionDomain {
	return positionBoundingBoxDomain{
		minPoint: vector3{
			x: math.Min(xmin, xmax),
			y: math.Min(ymin, ymax),
			z: math.Min(zmin, zmax),
		},
		maxPoint: vector3{
			x: math.Max(xmin, xmax),
			y: math.Max(ymin, ymax),
			z: math.Max(zmin, zmax),
		},
	}
}

// NoRotation is a RotationDomain that only ever produces the identity rotation.
func NoRotation() RotationDomain {
	return rotationEqualityDomain{offset: quaternion{x: 0.0, y: 0.0, z: 0.0, w: 1.0}}
}

// ArbitraryRotation is a RotationDomain that produces uniformly distributed
// arbitrary rotations.
func ArbitraryRotation() RotationDomain { return rotationBallDomain{} }

// AxiallyConstrainedRotation is a RotationDomain that produces uniformly
// distributed rotations about an axis in world space. The axis does not
// need to be normalized, but it must have a magnitude larger than some
// tolerance close to zero.
func AxiallyConstrainedRotation(x float64, y float64, z float64) (RotationDomain, error) {
	norm := math.Sqrt(x*x + y*y + z*z)
	if norm < 1e-6 {
		return NoRotation(), status.Errorf(codes.InvalidArgument, "Unable to create axially constrained rotation around roughly-zero vector: (%f, %f, %f)", x, y, z)
	}

	return rotationAxisDomain{
		axis: vector3{
			x: x / norm,
			y: y / norm,
			z: z / norm,
		},
	}, nil
}

// Create returns a PoseRandomizer from the given spawner configuration
func Create() (*PoseRandomizer, error) {
	return &PoseRandomizer{
		position: &positionEqualityDomain{offset: vector3{x: 0, y: 0, z: 0}},
		rotation: &rotationEqualityDomain{offset: quaternion{x: 0, y: 0, z: 0, w: 1}},
	}, nil
}

// GeneratePose uses a PoseRandomizer to generate a new random pose from the
// default random source in "math/rand".
func (pr *PoseRandomizer) GeneratePose() *posepb.Pose {
	pos := pr.position.Generate()
	rot := pr.rotation.Generate()

	// For unset rotations, set W=1 to make it a valid rotation.
	if compareRotEq(rot, 0, 0, 0, 0) {
		rot.W = 1.0
	}

	return &posepb.Pose{
		Position:    pos,
		Orientation: rot,
	}
}
