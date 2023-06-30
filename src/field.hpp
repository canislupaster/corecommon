#ifndef CORECOMMON_SRC_FIELD_HPP_
#define CORECOMMON_SRC_FIELD_HPP_

#include <cstddef>
#include <initializer_list>
#include <cmath>

#include <optional>
#include <array>
#include <limits>

//completely arbitrary!
static constexpr float Epsilon = std::numeric_limits<float>::epsilon()*64;

template<size_t num_rows, size_t num_cols>
struct Matrix;

template<size_t n, class RetTypeParam=void>
struct Vector {
	//🤙 dear god
	using RetType = std::conditional_t<std::is_void<RetTypeParam>::value, Vector, RetTypeParam>;
	
	std::array<float, n> v;

	template<class OtherRetType>
	explicit Vector(Vector<n, OtherRetType>&& x) : v(std::move(x.v)) {}

	template<class OtherRetType>
	explicit Vector(Vector<n, OtherRetType> const& x) : v(x.v) {}

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

	float norm() const {
		return sqrtf(this->dot(*this));
	}

	float norm_sq() const {
		return this->dot(*this);
	}

	//perpendicular, clockwise of original vector, swap arguments for ccw
	RetType perpendicular(size_t i, size_t j) const {
		RetType vec(0.0f);
		vec[i] = v[j];
		vec[j] = -v[i];
		return vec;
	}

	struct Intersection {
		float c1, c2; //intersection located at x1+off1*c1, x2+off2*c2
		RetType pos;

		bool in_segment() {
			return c1>=0 && c1<=1 && c2>=0 && c2<=1;
		}

		bool in_segment_no_endpoints() {
			return c1>0 && c1<1 && c2>0 && c2<1;
		}
	};

	static std::optional<Intersection> intersect(RetType const& x1, RetType const& off1, RetType const& x2, RetType const& off2) {
		for (size_t i = 0; i < n; i++) {
			if (x1[i]==x2[i]) continue;

			for (size_t j = 0; j < n; j++) {
				if (i==j) continue;

				float m1 = off1[i]/off1[j];
				float i_offset = x2[j]-x1[j];
				float x = (x2[i]-x1[i]-m1*i_offset)/(m1 - off2[i]/off2[j]);

				if (x==NAN) return std::optional<Intersection>();

				Intersection intersection {.c1=(i_offset+x)/off1[j], .c2=x/off2[j]};
				intersection.pos = x2 + off2*intersection.c2;

				if (n<=2 || x1+off1*intersection.c1 - intersection.pos < Epsilon) {
					intersection.c1 = intersection.c1 - std::remainder(intersection.c1, Epsilon);
					intersection.c2 = intersection.c2 - std::remainder(intersection.c2, Epsilon);

					return std::optional(intersection);
				} else {
					return std::optional<Intersection>();
				}
			}
		}

		return std::optional(Intersection {.c1=0, .c2=0, .pos=x1});
	}

	RetType& operator*=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] = v[i]*other[i];
		return static_cast<RetType&>(*this);
	}

	RetType& operator*=(float scale) {
		for (size_t i=0; i<n; i++) v[i] *= scale;
		return static_cast<RetType&>(*this);
	}

	RetType& operator/=(float scale) {
		for (size_t i=0; i<n; i++) v[i] /= scale;
		return static_cast<RetType&>(*this);
	}

	RetType& operator+=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] += other[i];
		return static_cast<RetType&>(*this);
	}

	RetType& operator-=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] -= other[i];
		return static_cast<RetType&>(*this);
	}

	RetType& operator/=(Vector const& other) {
		for (size_t i=0; i<n; i++) v[i] /= other[i];
		return static_cast<RetType&>(*this);
	}

	RetType& abs() {
		for (size_t i=0; i<n; i++) v[i]=std::abs(v[i]);
		return static_cast<RetType&>(*this);
	}

	RetType operator*(Vector const& other) const {
		RetType out = *this;
		out *= other;
		return out;
	}

	RetType operator*(float scale) const {
		RetType out = *this;
		out *= scale;
		return out;
	}

	RetType operator/=(float scale) const {
		RetType out = *this;
		out *= scale;
		return out;
	}

	RetType operator+(Vector const& other) const {
		RetType out = *this;
		out += other;
		return out;
	}

	RetType operator-(Vector const& other) const {
		RetType out = *this;
		out -= other;
		return out;
	}

	RetType operator/(Vector const& other) const {
		RetType out = *this;
		out /= other;
		return out;
	}

	RetType operator-() const {
		RetType out;
		for (size_t i=0; i<n; i++) out[i] = -v[i];
		return out;
	}

	RetType abs() const {
		RetType out = *this;
		out.abs();
		return out;
	}

	float& operator[](size_t i) {
		return v[i];
	}

	float operator[](size_t i) const {
		return v[i];
	}

	bool operator>=(float other) const {
		for (size_t i = 0; i < n; i++) { if (v[i]<other) return false; }
		return true;
	}

	bool operator>(float other) const {
		for (size_t i = 0; i < n; i++) { if (v[i]<=other) return false; }
		return true;
	}

	bool operator<(float other) const {
		for (size_t i = 0; i < n; i++) { if (v[i]>=other) return false; }
		return true;
	}

	bool operator<=(float other) const {
		for (size_t i = 0; i < n; i++) { if (v[i]>other) return false; }
		return true;
	}

	bool operator==(float other) const {
		for (size_t i = 0; i < n; i++) { if (v[i]!=other) return false; }
		return true;
	}

	bool operator==(Vector const& other) const {
		return v==other.v;
	}

	bool operator!=(Vector const& other) const {
		return v!=other.v;
	}

	RetType normalize(float length) const {
		return *this*(length/norm());
	}

	float* begin() {
		return v.begin();
	}

	float* end() {
		return v.end();
	}

	float const* begin() const {
		return v.begin();
	}

	float const* end() const {
		return v.end();
	}
};

struct Vec2: Vector<2, Vec2> {
	//implicit conversions 🤡
	using Vector<2, Vec2>::Vector;
	Vec2(Vector<2, Vec2> const& vec2): Vector<2, Vec2>(vec2) {}
	Vec2(Vector<2, Vec2>&& vec2): Vector<2, Vec2>(vec2) {}
	Vec2(Vector<2> const& vec2): Vector<2, Vec2>(vec2) {}
	Vec2(Vector<2>&& vec2): Vector<2, Vec2>(vec2) {}

	float determinant(Vec2 const& other) const;
	Vec2 rotate_by(Vec2 const& other);
	Vec2 reflect(Vec2 const& other);
	float slope(bool swap) const;
};

struct Vec3: Vector<3> {
	using Vector<3>::Vector;
	Vec3(Vector<3> const& vec3): Vector<3>(vec3) {}
	Vec3(Vector<3>&& vec3): Vector<3>(vec3) {}

	float determinant(Vector const& other) const;
	Vec3 cross(Vec3 const& other) const;
};

using Vec4 = Vector<4>;

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

	Matrix(std::initializer_list<float> diag): cols() {
		for (size_t col=0; col<num_cols; col++) {
			cols[col][col] = *(diag.begin()+col);
		}
	}

	Matrix(float diag): cols() {
		for (size_t col=0; col<num_cols; col++) {
			cols[col][col] = diag;
		}
	}

	static Matrix<3,3> cross(Vec3 vec) {
		return Matrix({
			{0,-vec[2],vec[1]},
			{vec[3],0,-vec[0]},
			{-vec[1],vec[0],0}
		});
	}

	static Matrix<3,3> rotate_3d(Matrix<3,3> mat, Vec3 axis, float angle) {
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
				for (int x2=x-1; x2>=y; x2--) {
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

using Mat2 = Matrix<2, 2>;
using Mat3 = Matrix<3, 3>;
using Mat4 = Matrix<4, 4>;

struct Rotation3D {
	//standard quaternion
	float cos;
	Vec3 axis;

	Rotation3D() = default;
	Rotation3D(Vec3 axis, float angle);
	static Rotation3D from_quat(Vec3 sin_axis, float cos);
	Rotation3D& operator*=(Rotation3D const& other);
	Rotation3D conjugate() const;

	Mat3 to_matrix() const;

	Vec3 operator*(Vec3 const& v) const;
};

#endif //CORECOMMON_SRC_FIELD_HPP_
