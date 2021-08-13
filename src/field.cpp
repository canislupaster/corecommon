#include "field.hpp"

Rotation3D::Rotation3D(Vec3 axis, float angle): axis(axis*sinf(angle/2)), cos(cosf(angle/2)) {}

Rotation3D Rotation3D::from_quat(Vec3 sin_axis, float cos) {
	Rotation3D out;
	out.axis=sin_axis;
	out.cos=cos;
	return out;
}

Rotation3D& Rotation3D::operator*=(const Rotation3D& other) {
	float old_cos = cos;
	cos = cos*other.cos - other.axis.dot(axis);
	axis = other.axis*old_cos + axis*other.cos + axis.cross(other.axis);

	return *this;
}

Rotation3D Rotation3D::conjugate() const {
	return Rotation3D::from_quat(-axis, cos);
}

Mat3 Rotation3D::to_matrix() const {
	Mat3 cprod = Mat3::cross(axis);
	return Mat3(cos*cos - 1) + axis.outer_product(axis*2) + cprod*2*cos;
}

Vec3 Rotation3D::operator*(const Vec3& v) const {
	Vec3 c = axis.cross(v);
	return v*(cos*cos-1) + axis*axis.dot(v*2) + c*2*cos;
}

float Vec2::determinant(Vec2 const& other) const {
	return v[0]*other[1] - v[1]*other[0];
}

Vec2 Vec2::rotate_by(Vec2 const& other) {
	return Vec2 {v[0]*other[0] - v[1]*other[1], v[0]*other[1] + v[1]*other[0]};
}

Vec2 Vec2::reflect(Vec2 const& other) {
	Vec2 rotated {-dot(other), other.determinant(*this)};
	return rotated.rotate_by(other);
}

float Vec3::determinant(Vector const& other) const {
	return v[1]*other[2]-v[2]*other[1] + v[0]*other[2]-v[2]*other[0] + v[0]*other[1]-v[1]*other[0];
}

Vec3 Vec3::cross(Vec3 const& other) const {
	Vec3 out {
			v[1]*other[2]-v[2]*other[1],
			v[0]*other[2]-v[2]*other[0],
			v[0]*other[1]-v[1]*other[0]
	};

	return out;
}
