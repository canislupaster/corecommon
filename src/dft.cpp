#include "dft.hpp"

void fft_rec(std::complex<float>* fftres, float const* v, std::complex<float>* coeffs, int res, int stride, int offset) {
	if (res % 2 == 0) {
		fft_rec(fftres, v, coeffs, res/2, stride*2, offset);
		fft_rec(fftres, v+stride, coeffs, res/2, stride*2, offset+stride);
	} else {
		for (unsigned i=0; i<res; i++) {
			for (unsigned vi=0; vi<res; vi++) {
				std::complex<float> x = std::complex(v[vi*stride]) * coeffs[(stride*(i*vi) + i*offset) % res];
				for (int c=0; c<stride; c++) fftres[i+res*c] += x;
			}
		}
	}
}

std::vector<std::complex<float>> fft_single(float const* v, std::complex<float>* coeffs, int res) {
	std::vector<std::complex<float>> fftres(res, 0);
	fft_rec(fftres.data(), v, coeffs, res, 1, 0);
	return fftres;
}

//utility to sample e^ix, then downscaled in recursive fft functions to reduce divisions
std::vector<std::complex<float>> fft_coeffs(unsigned N) {
	std::vector<std::complex<float>> coeffs(N);

	float fn = static_cast<float>(N);
	for (unsigned i=0; i<N; i++) {
		coeffs.emplace_back(std::exp(std::complex(0.0f, static_cast<float>(i)/fn)));
	}

	return coeffs;
}
