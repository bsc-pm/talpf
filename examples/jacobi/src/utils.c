#include <mpi.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "heat.h"

lpf_t lpf;
lpf_pid_t rank;
lpf_pid_t nranks;

lpf_memslot_t UPPER_HALO[2];
lpf_memslot_t LOWER_HALO[2];
lpf_memslot_t RESIDUAL;

void broadcastConfiguration(HeatConfiguration *conf)
{
	MPI_Bcast(conf, sizeof(HeatConfiguration), MPI_BYTE, 0, MPI_COMM_WORLD);

	const int heatSourcesSize = sizeof(HeatSource)*conf->numHeatSources;
	if (rank > 0) {
		// Received heat sources pointer is not valid
		conf->heatSources = (HeatSource *) malloc(heatSourcesSize);
		assert(conf->heatSources != NULL);
	}
	MPI_Bcast(conf->heatSources, heatSourcesSize, MPI_BYTE, 0, MPI_COMM_WORLD);
}

void setupLPF(const HeatConfiguration *conf, int64_t rows, int64_t cols)
{
	CHECK(lpf_resize_message_queue(lpf, 4*conf->cols/conf->cbs ));
	CHECK(lpf_resize_memory_register(lpf, 4));

	#pragma oss task label("sync")
	CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
	#pragma oss taskwait

	CHECK(lpf_register_global(lpf, &conf->matrix[0], 2*cols*sizeof(double), &UPPER_HALO[0]));
	CHECK(lpf_register_global(lpf, &conf->matrixAux[0], 2*cols*sizeof(double), &UPPER_HALO[1]));
	CHECK(lpf_register_global(lpf, &conf->matrix[(rows-2)*cols], 2*cols*sizeof(double), &LOWER_HALO[0]));
	CHECK(lpf_register_global(lpf, &conf->matrixAux[(rows-2)*cols], 2*cols*sizeof(double), &LOWER_HALO[1]));


	#pragma oss task label("sync")
	CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
	#pragma oss taskwait
}

void freeLPF(void)
{
	CHECK(lpf_deregister(lpf, UPPER_HALO[0]));
	CHECK(lpf_deregister(lpf, UPPER_HALO[1]));
	CHECK(lpf_deregister(lpf, LOWER_HALO[0]));
	CHECK(lpf_deregister(lpf, LOWER_HALO[1]));
}
