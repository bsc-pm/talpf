#include <stdint.h>

void computeBlock(const int64_t rows, const int64_t cols,
		const int rstart, const int rend,
		const int cstart, const int cend,
		const double srcM[rows][cols],
		double dstM[rows][cols])
{
	for (int r = rstart; r <= rend; ++r) {
		for (int c = cstart; c <= cend; ++c) {
			dstM[r][c] = 0.25*(srcM[r-1][c] + srcM[r+1][c] + srcM[r][c-1] + srcM[r][c+1]);
		}
	}
}

double computeBlockResidual(const int64_t rows, const int64_t cols,
		const int rstart, const int rend,
		const int cstart, const int cend,
		const double srcM[rows][cols],
		double dstM[rows][cols])
{
	double sum = 0.0;
	for (int r = rstart; r <= rend; ++r) {
		for (int c = cstart; c <= cend; ++c) {
			const double value = 0.25*(srcM[r-1][c] + srcM[r+1][c] + srcM[r][c-1] + srcM[r][c+1]);
			const double diff = value - srcM[r][c];
			sum += diff*diff;
			dstM[r][c] = value;
		}
	}
	return sum;
}
