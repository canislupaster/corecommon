#include <string.h>

#include "util.h"

typedef struct {
	float* vals;
	unsigned rows;
	unsigned cols;
} mat_t;

float* mat_cell(mat_t* mat, int x, int y) {
	return mat->vals + (x*mat->rows) + y;
}

void mat_mul(mat_t* a, mat_t* b, mat_t* out) {
	//probably slow af lmao
	float colbuf[b->rows];

	for (unsigned x=0; x < b->cols; x++) {
		float* colptr = b->vals + (x*b->rows);
		for (unsigned i=0; i<b->rows; i++) colbuf[i] = colptr[i];
		for (unsigned y=0; y<a->rows; y++) {
			float res=0;
			for (unsigned i=0; i<a->cols; i++)
				res += a->vals[(i*a->rows) + y]*colbuf[i];
			*mat_cell(out, x, y) = res;
		}
	}
}

typedef struct {
	float* vals;
	int diag;
	char lo;
} tri_mat_t;

tri_mat_t tri_mat_new(int diag, char lo) {
	tri_mat_t mat = {.diag=diag, .lo=lo, .vals=heap(sizeof(float)*((diag*(diag+1))/2))};
	memset(mat.vals, 0, sizeof(float)*(diag*(diag+1))/2);
	return mat;
}

float* tri_mat_cell(tri_mat_t* mat, int x, int y) {
	if (y>x) { //lower triangular, transpose
		x = mat->diag - x;
		y = mat->diag - y;
	}

	return mat->vals + ((x*(x+1))/2)-1 + y;
}

float* tri_mat_col(tri_mat_t* mat, int x) {
	return mat->lo ? mat->vals + (((mat->diag-x)*((mat->diag-x)+1))/2)-1 : mat->vals + ((x*(x+1))/2)-1;
}

void tri_mat_inverse(tri_mat_t* mat, tri_mat_t* inverse) {
	for (unsigned x=0; x<mat->diag; x++) {
		float dx = *tri_mat_cell(mat, x, x);
		*tri_mat_cell(inverse, x, x) = 1/dx;

		for (unsigned y=0; y<x; y++) {
			float coeff=0; //invert coeff subtracting against the diagonal
			//since triangular matrices skew on a new axis, we compare (the skew: column x and the diagonals)
			// with the old ones (modifications to the row)
			for (int x2=x; x2>=y; x2--) {
				coeff += *tri_mat_cell(mat, x2, y) * *tri_mat_cell(mat, x, x2);
			}

			*tri_mat_cell(inverse, x, y) = -coeff/dx;
		}
	}
}

void tri_mat_mul(tri_mat_t* a, tri_mat_t* b, mat_t* out) {
	float colbuf[b->diag];
	for (unsigned x=0; x < b->diag; x++) {
		float* colptr = b->vals + (x*(x+1)/2)-1;
		for (unsigned i=0; i<=x; i++) colbuf[b->lo ? b->diag-i : i] = colptr[i];

		for (unsigned y=0; y<a->diag; y++) {
			float res=0;
			for (unsigned i=0; i<=y; i++)
				res += a->vals[(i*(i+1)/2)-1 + y]*colbuf[a->lo ? a->diag-i : i];
			*mat_cell(out, b->lo ? b->diag-x : x, a->lo ? a->diag-y : y) = res;
		}
	}
}

float mat_det(mat_t* mat) {
	float d = 1;
	float d_mul = 1;

	int rows[mat->rows];
	float (*rowadd)[mat->rows] = heap(sizeof(float)*mat->rows*mat->rows);
	memset(rowadd, 0, sizeof(float)*mat->rows*mat->rows);

	for (int i=0; i<mat->rows; i++) rows[i] = i;

	for (int y=mat->rows-1; y>=0; y--) {
		float yval;

		if ((yval = *mat_cell(mat,y,rows[y])) == 0) {
			int swap=-1;

			for (int y2=y-1; y2>=0; y2--) {
				yval = *mat_cell(mat,y,rows[y2]);
				//resolve diagonal / apply transforms
				//if row becomes null, this will be found later on when y==y2 and there arent any rows left
				for (int y3=mat->rows-1; y3>y; y3--) {
					if (rowadd[rows[y2]][rows[y3]]==0) continue;
					yval += *mat_cell(mat,y,rows[y3]) * rowadd[rows[y2]][rows[y3]];
				}

				if (yval!=0) {
					swap=y2;
					break;
				}
			}

			if (swap==-1) {
				return 0; //singular
			}

			d_mul *= -1;

			int ry = rows[y];
			rows[y] = rows[swap];
			rows[swap] = ry;
		}

		for (int y2=y-1; y2>0; y2--) {
			rowadd[rows[y2]][rows[y]] -= *mat_cell(mat,y,rows[y2])/yval;
		}

		float yd = yval;
		for (int y2=mat->rows; y2>y; y2--) {
			if (rowadd[rows[y]][rows[y2]]==0) continue;
			yd += *mat_cell(mat,y,rows[y2]) * rowadd[rows[y]][rows[y2]]; //resolve diagonal
		}

		d *= yd;
	}

	return d/d_mul;
}

//nearly same
int mat_lu(mat_t* mat, tri_mat_t* l, tri_mat_t* u) {
	int rows[mat->rows];
	for (int i=0; i<mat->rows; i++) rows[i] = i;

	for (int y=mat->rows-1; y>=0; y--) {
		float yval;
		int swap = -1;

		if ((yval = *mat_cell(mat, y, rows[y])) == 0) {
			for (int y2 = y - 1; y2 >= 0; y2--) {
				yval = *mat_cell(mat, y, rows[y2]);

				for (int y3 = mat->rows-1; y3 > y; y3--) {
					float coeff = *tri_mat_cell(u, y2, y3);
					if (coeff == 0) continue;
					yval -= *mat_cell(mat, y, rows[y3]) * coeff;
				}

				if (yval != 0) {
					swap = y2;
					break;
				}
			}

			if (swap == -1) {
				return 0;
			}

			int ry = rows[y];
			rows[y] = rows[swap];
			rows[swap] = ry;

			//swap rows in upper matrix
			for (int x = y+1; x<mat->rows; x++) {
				float rcoeff = *tri_mat_cell(u, x, y);
				*tri_mat_cell(u, x, y) = *tri_mat_cell(u, x, swap);
				*tri_mat_cell(u, x, swap) = rcoeff;
			}
		}

		*tri_mat_cell(u, y, y) = 1;
		for (int y2 = y-1; y2 > 0; y2--) {
			*tri_mat_cell(u, y, y2) += *mat_cell(mat, y, rows[y2]) / yval;
		}
	}

	//rip efficiency
	float* colptr = l->vals;
	for (int x=0; x<mat->cols; x++) {
		for (int y=mat->rows; y>=x; y--) {
			float yval = *mat_cell(mat, x, y);
			//copypasta
			for (int y2 = mat->rows-1; y2 > y; y2--) {
				float coeff = *tri_mat_cell(u, y2, y2);
				if (coeff == 0) continue;
				yval -= *mat_cell(mat, y, rows[y2]) * coeff;
			}

			*colptr = yval;
			colptr++;
		}
	}

	return 1;
}

int mat_inverse(mat_t* mat, mat_t* inverse) {
	tri_mat_t l = tri_mat_new(mat->rows, 1);
	tri_mat_t u = tri_mat_new(mat->cols, 0);
	if (!mat_lu(mat, &l, &u)) {
		drop(l.vals);
		drop(u.vals);
		return 0;
	}

	tri_mat_t l_inv = tri_mat_new(mat->rows, 1);
	tri_mat_t u_inv = tri_mat_new(mat->rows, 0);
	tri_mat_inverse(&l, &l_inv);
	tri_mat_inverse(&u, &u_inv);

	tri_mat_mul(&u_inv, &l_inv, inverse);

	drop(l.vals);
	drop(u.vals);
	drop(l_inv.vals);
	drop(u_inv.vals);

	return 1;
}