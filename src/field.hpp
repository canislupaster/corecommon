#ifndef CORECOMMON_SRC_FIELD_HPP_
#define CORECOMMON_SRC_FIELD_HPP_

#include <cstddef>
#include <initializer_list>
#include <cmath>

#include <array>

template<size_t num_rows, size_t num_cols>
struct Matrix;

template<size_t n>
struct Vector {
	std::array<float, n> v;

	Vector(): v{0} {}
	Vector(float x) {
		for (size_t i=0; i<n; i++) v[i]=x;
	}

	Vector(float* x) {
		memcpy(v.data(), x, n*sizeof(float));
	}

	Vector(std::initializer_list<float> l) {
		std::copy(l.begin(), l.end(), v.begin());
	}

	Matrix<1,n> transpose() const {
		Matrix<1,n> out;
		for (size_t i=0; i<n; i++) out.cols[i][0]=v[i];
		return out;
	}

	Matrix<n,n> outer_product(Vector const& other) const {
		Matrix<n,n> out;
		for (size_t i=0; i<n; i++) out.cols[i]=*this*other.v[i];

		return out;
	}

	float dot(Vector const& other) const {
		float ret=0;
		for (size_t i=0; i<n; i++) ret += v[i]*other[i];
		return ret;
	}

	Vector cross(Vector const& other) const {
		Vector out {
			v[1]*other[2]-v[2]*other[1],
			v[0]*other[2]-v[2]*other[0],
			v[0]*other[1]-v[1]*other[0]
		};

		return out;
	}

	float norm() const {
		float ret=0;
		for (size_t i=0; i<n; i++) ret += v[i]*v[i];
		return sqrtf(ret);
	}

	void normalize(float length) {
		float mag = norm();
		for (size_t i=0; i<n; i++) v[i] *= length/mag;
	}

	Vector& operator*=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] = v[i]*other[i];
		return *this;
	}

	Vector& operator*=(float scale) {
		for (size_t i=0; i<n; i++) v[i] *= scale;
		return *this;
	}

	Vector& operator/=(float scale) {
		for (size_t i=0; i<n; i++) v[i] /= scale;
		return *this;
	}

	Vector& operator+=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] += other[i];
		return *this;
	}

	Vector& operator-=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] -= other[i];
		return *this;
	}

	Vector& operator/=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] /= other[i];
		return *this;
	}

	Vector operator*(Vector const& other) const {
		Vector<n> out = *this;
		out *= other;
		return out;
	}

	Vector operator*(float scale) const {
		Vector<n> out = *this;
		out *= scale;
		return out;
	}

	Vector operator/=(float scale) const {
		Vector<n> out = *this;
		out *= scale;
		return out;
	}

	Vector operator+(Vector const& other) const {
		Vector<n> out = *this;
		out += other;
		return out;
	}

	Vector operator-(Vector const& other) const {
		Vector<n> out = *this;
		out -= other;
		return out;
	}

	Vector operator/(Vector const& other) const {
		Vector<n> out = *this;
		out /= other;
		return out;
	}

	Vector operator-() const {
		Vector<n> out;
		for (size_t i=0; i<n; i++) out[i] = -v[i];
		return out;
	}

	float& operator[](size_t i) {
		return v[i];
	}

	float operator[](size_t i) const {
		return v[i];
	}

	float* begin() {
		return v.begin();
	}

	float* end() {
		return v.end();
	}
};

template<size_t n, bool lower>
struct TriangularMatrix;

template<size_t num_rows, size_t num_cols>
struct Matrix {
	Vector<num_rows> cols[num_cols];

	Matrix(): cols() {}
	Matrix(std::initializer_list<std::initializer_list<float>> list) {
		std::copy(list.begin()->begin(), (list.end()-1)->end(), cols[0].begin());
	}

	Matrix(std::initializer_list<Vector<num_rows>> list): cols(list) {}

	Matrix(float diag): cols() {
		for (size_t col=0; col<num_cols; col++) {
			cols[col][col] = diag;
		}
	}

	static Matrix<3,3> cross(Vector<3> vec) {
		return Matrix({
			{0,-vec[2],vec[1]},
			{vec[3],0,-vec[0]},
			{-vec[1],vec[0],0}
		});
	}

	static Matrix<3,3> rotate_3d(Matrix<3,3> mat, Vector<3> axis, float angle) {
		return mat*cosf(angle) + axis.outer_product(axis*(1-cosf(angle))) + Matrix::cross(axis)*sinf(angle);
	}

	Vector<num_rows> const& operator[](size_t x) const {
		return cols[x];
	}

	Vector<num_rows>& operator[](size_t x) {
		return cols[x];
	}

	void transpose_inplace() {
		for (size_t x=0; x<num_cols; x++) {
			for (size_t y=x+1; y<num_rows; y++) {
				float z = cols[x][y];
				cols[x][y] = cols[y][x];
				cols[y][x] = z;
			}
		}
	}

	Matrix<num_cols, num_rows> transpose() const {
		Matrix<num_cols, num_rows> mat;
		for (size_t x=0; x<num_cols; x++) {
			for (size_t y=0; y<num_rows; y++) {
				mat[y][x] = cols[x][y];
			}
		}

		return mat;
	}

	template<size_t other_rows, size_t other_cols>
	Matrix operator*(Matrix<other_rows, other_cols> const& other) const {
		Matrix<num_rows, other_cols> out;

		for (unsigned x=0; x<other_cols; x++) {
			Vector<other_rows>& col = other.cols[x];
			for (unsigned y=0; y<num_rows; y++) {
				out.cols[x][y]=0;
				for (unsigned x2=0; x2<std::min(num_cols, other_rows); x2++) {
					out.cols[x][y]+=cols[x2][y]*col[x2];
				}
			}
		}

		return out;
	}

	Matrix& operator*=(Matrix const& other) {
		*this = *this * other;
		return *this;
	}

	Matrix& operator*=(Vector<num_rows> const& vec) {
		for (size_t i=0; i<num_cols; i++) cols[i] *= vec;
		return *this;
	}

	Matrix& operator*=(float scale) {
		for (size_t i=0; i<num_cols; i++) cols[i] *= scale;
		return *this;
	}

	Matrix& operator/=(float scale) {
		for (size_t i=0; i<num_cols; i++) cols[i] /= scale;
		return *this;
	}

	Matrix& operator+=(Matrix const& other) {
		for (size_t i=0; i<num_cols; i++) cols[i] += other[i];
		return *this;
	}

	Matrix& operator-=(Matrix const& other) {
		for (size_t i=0; i<num_cols; i++) cols[i] -= other[i];
		return *this;
	}

	Matrix& operator/=(Matrix const& other) {
		for (size_t i=0; i<num_cols; i++) cols[i] /= other[i];
		return *this;
	}

	Matrix operator*(Vector<num_rows> const& other) const {
		Matrix<num_rows, num_cols> out = *this;
		out *= other;
		return out;
	}
	
	Matrix operator+(Matrix const& other) const {
		Matrix<num_rows, num_cols> out = *this;
		out += other;
		return out;
	}

	Matrix operator-(Matrix const& other) const {
		Matrix<num_rows, num_cols> out = *this;
		out -= other;
		return out;
	}

	Matrix operator-() const {
		Matrix<num_rows, num_cols> out;
		for (size_t i=0; i<num_cols; i++) out[i] = -cols[i];
		return out;
	}
};

template<size_t n, bool lower>
struct TriangularMatrix {
	std::array<float, (n*n+n)/2> vals;

	TriangularMatrix(): vals{0} {}
	TriangularMatrix(float diag): vals{0} {
		size_t i=0;
		for (size_t x=0; x<n; x++) {
			vals[lower ? i : i+x] = diag;
			i+=x+1;
		}
	}

	//lmao undefined behavior if u access past x!
	Vector<n>& operator[](size_t x) {
		if (lower) {
			x=n-1-x;
			return reinterpret_cast<Vector<n>&>(vals[x*(x+1)/2 - x]);
		} else {
			return reinterpret_cast<Vector<n>&>(vals[x*(x+1)/2]);
		}
	}

	Vector<n> const& operator[](size_t x) const {
		if (lower) {
			x=n-1-x;
			return reinterpret_cast<Vector<n> const&>(vals[x*(x+1)/2 - x]);
		} else {
			return reinterpret_cast<Vector<n> const&>(vals[x*(x+1)/2]);
		}
	}

	float det() const {
		float d=0;
		unsigned i=0;
		for (int x=0; x<n; x++) {
			d *= this->vals[i+x];
			i+=x+1;
		}

		return d;
	}

	TriangularMatrix<n,lower> inverse() const {
		TriangularMatrix<n,lower> inverse;

		unsigned i=vals.size()-1;
		for (unsigned x=0; x<n; x++) {
			float dx = vals[i+x];
			inverse[i+x] = 1/dx;

			for (unsigned y=0; y<x; y++) {
				float coeff=0;
				for (unsigned x2=x-1; x2>=y; x2--) {
					coeff += inverse[x2][y]*vals[x][y];
				}

				inverse[i+y] = -coeff/dx;
			}

			i-=x+1;
		}
	}

	Matrix<n,n> operator*(TriangularMatrix<n,!lower> const& other) const {
		Matrix<n,n> res{{0}};

		unsigned i=0;
		for (unsigned x=0; x<n; x++) {
			for (unsigned y=0; y<n; y++) {
				float& cell = res[lower ? x : n-1-x][lower ? y : n-1-y];
				cell=0;

				for (unsigned x2=0; x2<std::min(x+1, y+1); x2++) {
					cell =+ other.vals[i+x2]*(*this)[x2][y];
				}
			}

			i+=x+1;
		}

		return res;
	}
};

template<size_t n>
struct SquareMatrix: public Matrix<n,n> {
	struct LU {
		TriangularMatrix<n, true> l;
		union {
			TriangularMatrix<n, false> u;
			float det;
		};
	};

	LU lu(bool det=false) const {
		LU ret = {.l=TriangularMatrix<n,true>()};
		std::copy(this->cols[0].begin(), this->cols[0].end(), ret.l[0].begin());

		if (det) {
			ret.det=1.0f;
		} else {
			ret.u=TriangularMatrix<n,false>(1.0f);
		}

		for (size_t x=1; x<n; x++) {
			Vector<n>& col = ret.l[x];
			std::copy(this->cols[x].begin()+x, this->cols[x].end(), col.begin()+x);

			for (size_t y=x-1; y>=0; y--) {
				if (this->col[y]!=0) {
					Vector<n>& other = ret.l[y];
					float v = other[y]/this->col[y];

					for (size_t i=x; i<n; i++) {
						col[i] -= other[i]*v;
					}

					if (!det) {
						ret.u[y][x] = v;
					}
				}
			}

			if (det) {
				ret.det *= col[x];
				if (ret.det==0) return ret;
			}
		}

		return ret;
	}

	Matrix<n, n> inverse() const {
		LU x = lu();
		return x.u.inverse()*x.l.inverse();
	}

	float det() const {
		LU x = lu(true);
		return x.det;
	}
};

using Matrix3 = Matrix<3,3>;

struct Rotation3D {
	//standard quaternion
	float cos;
	Vector<3> axis;

	Rotation3D() = default;
	Rotation3D(Vector<3> axis, float angle);
	static Rotation3D from_quat(Vector<3> sin_axis, float cos);
	Rotation3D& operator*=(Rotation3D const& other);
	Rotation3D conjugate() const;

	Matrix3 to_matrix() const;

	Vector<3> operator*(Vector<3> const& v) const;
};

#endif //CORECOMMON_SRC_FIELD_HPP_
