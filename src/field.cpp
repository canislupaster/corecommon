#include "field.hpp"

Rotation3D::Rotation3D(Vector<3> axis, float angle): axis(axis*sinf(angle/2)), cos(cosf(angle/2)) {}

Rotation3D Rotation3D::from_quat(Vector<3> sin_axis, float cos) {
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

Matrix3 Rotation3D::to_matrix() const {
	Matrix3 cprod = Matrix3::cross(axis);
	return Matrix3(cos*cos - 1) + axis.outer_product(axis*2) + cprod*2*cos;
}

Vector<3> Rotation3D::operator*(const Vector<3>& v) const {
	Vector<3> c = axis.cross(v);
	return v*(cos*cos-1) + axis*axis.dot(v*2) + c*2*cos;
}
