#include <lpf/core.h>

#include "utils.h"
#include "heat.h"

#include <time.h>
struct timespec start, end;

static int serial;
static int serial2;

static inline void put(lpf_memslot_t lslot, int64_t loffset, int dst, lpf_memslot_t rslot, int64_t roffset, int nelems)
{
	CHECK(talpf_put(lpf, lslot, loffset*sizeof(double), dst, rslot, roffset*sizeof(double), nelems*sizeof(double), LPF_MSG_DEFAULT));
}

static inline void jacobiSolver(int64_t rows, int64_t cols,
		int rbs, int cbs, int nrb, int ncb,
		double srcM[rows][cols], double dstM[rows][cols],
		char srcReps[nrb][ncb], char dstReps[nrb][ncb],
		lpf_memslot_t upper, lpf_memslot_t lower)
{
	for (int R = 1; R < nrb-1; ++R) {
		for (int C = 1; C < ncb-1; ++C) {
			#pragma oss task label("block computation") \
					in(srcReps[R-1][C]) in(srcReps[R+1][C]) \
					in(srcReps[R][C-1]) in(srcReps[R][C+1]) \
					in(srcReps[R][C]) out(dstReps[R][C])
			computeBlock(rows, cols, (R-1)*rbs+1, R*rbs, (C-1)*cbs+1, C*cbs, srcM, dstM);
		}
	}

	for (int C = 1; C < ncb-1; ++C) {
		if (rank != 0) {
			#pragma oss task label("put upper inner") in(dstReps[1][C])
			put(upper, cols+(C-1)*cbs+1, rank-1, lower, cols+(C-1)*cbs+1, cbs);
		}

		if (rank != nranks-1) {
			#pragma oss task label("put lower inner") in(dstReps[nrb-2][C])
			put(lower, (C-1)*cbs+1, rank+1, upper, (C-1)*cbs+1, cbs);
		}
	}

	#pragma oss task label("sync") \
			inout({dstReps[1][C], C=1;ncb-2}) \
			inout({dstReps[nrb-2][C], C=1;ncb-2}) \
			inout({dstReps[0][C], C=1;ncb-2}) \
			inout({dstReps[nrb-1][C], C=1;ncb-2})
	CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
}

double solve(HeatConfiguration *conf, int64_t rows, int64_t cols, int timesteps, void *extraData)
{
	double (*matrix1)[cols] = (double (*)[cols]) conf->matrix;
	double (*matrix2)[cols] = (double (*)[cols]) conf->matrixAux;
	const int rbs = conf->rbs;
	const int cbs = conf->cbs;

	const int nrb = (rows-2)/rbs+2;
	const int ncb = (cols-2)/cbs+2;

	char representatives[nrb][ncb];
	char representativesAux[nrb][ncb];
	char (*reps1)[ncb] = representatives;
	char (*reps2)[ncb] = representativesAux;

	lpf_memslot_t upper1 = UPPER_HALO[0];
	lpf_memslot_t upper2 = UPPER_HALO[1];
	lpf_memslot_t lower1 = LOWER_HALO[0];
	lpf_memslot_t lower2 = LOWER_HALO[1];

	for (int t = 0; t < timesteps; ++t) {

		jacobiSolver(rows, cols, conf->rbs, conf->cbs, nrb, ncb, matrix1, matrix2, reps1, reps2, upper2, lower2);

		SWAP(matrix1, matrix2);
		SWAP(reps1, reps2);
		SWAP(upper1, upper2);
		SWAP(lower1, lower2);
	}
	#pragma oss taskwait

	if (conf->timesteps % 2 != 0)
		SWAP(conf->matrix, conf->matrixAux);

	#pragma oss task label("sync")
	CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
	#pragma oss taskwait

	return IGNORE_RESIDUAL;
}

