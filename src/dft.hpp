#ifndef CORECOMMON_SRC_DFT_HPP_
#define CORECOMMON_SRC_DFT_HPP_

#include <complex>
#include <vector>

std::vector<std::complex<float>> fft_single(float const* v, std::complex<float>* coeffs, int res);

#endif //CORECOMMON_SRC_DFT_HPP_
