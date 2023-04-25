#include <lpf/core.h>
#include <lpf/mpi.h>
#include <mpi.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "heat.h"

#ifdef _OMPSS_2
#include <nanos6/debug.h>
#endif

const int LPF_MPI_AUTO_INITIALIZE = 0;

void application(lpf_t ctx, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args);
void generateImage(const HeatConfiguration *conf, int64_t rows, int64_t cols, int64_t rowsPerRank);

int main(int argc, char **argv)
{
	const int required = MPI_THREAD_SERIALIZED;
	//const int required = MPI_THREAD_MULTIPLE;

	int provided;
	MPI_Init_thread(&argc, &argv, required, &provided);
	if (provided != required) {
		fprintf(stderr, "Error: MPI threading level not supported!\n");
		return 1;
	}

	lpf_args_t args;
	memset(&args, 0, sizeof(lpf_args_t));
	args.input = argv;
	args.input_size = argc;

	lpf_init_t init;
	CHECK(lpf_mpi_initialize_with_mpicomm(MPI_COMM_WORLD, &init));

	CHECK(lpf_hook(init, &application, args));

	CHECK(lpf_mpi_finalize(init));

	MPI_Finalize();
}

void application(lpf_t ctx, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args)
{
	char **argv = (char **) args.input;
	int argc = args.input_size;

	lpf = ctx;
	rank = pid;
	nranks = nprocs;

	HeatConfiguration conf;
	if (!rank) {
		readConfiguration(argc, argv, &conf);
		refineConfiguration(&conf, nranks*conf.rbs, conf.cbs);
		if (conf.verbose) printConfiguration(&conf);
	}
	broadcastConfiguration(&conf);

	int64_t rows = conf.rows+2;
	int64_t cols = conf.cols+2;
	int64_t rowsPerRank = conf.rows/nranks+2;

	int err = initialize(&conf, rowsPerRank, cols, (rowsPerRank-2)*rank);
	assert(!err);

	setupLPF(&conf, rowsPerRank, cols);

	if (conf.warmup) {
		#pragma oss task label("sync")
		CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
		#pragma oss taskwait

		solve(&conf, rowsPerRank, cols, 1, NULL);
	}

	#pragma oss task label("sync")
	CHECK(talpf_sync(lpf, LPF_SYNC_DEFAULT));
	#pragma oss taskwait

	// Solve the problem
	double start = getTime();
	solve(&conf, rowsPerRank, cols, conf.timesteps, NULL);
	double end = getTime();

	if (!rank) {
		int64_t totalElements = conf.rows*conf.cols;
		double throughput = (totalElements*conf.timesteps)/(end-start);
		throughput = throughput/1000000.0;

#ifdef _OMPSS_2
		int threads = nanos6_get_num_cpus();
#else
		int threads = 1;
#endif
		fprintf(stderr, "%f\n", throughput);
		fprintf(stdout, "rows, %ld, cols, %ld, rows/rank, %ld, total, %ld, total/rank, %ld, rbs, %d, "
				"cbs, %d, ranks, %d, threads, %d, timesteps, %d, time, %f, Mupdates/s, %f\n",
				conf.rows, conf.cols, conf.rows/nranks, totalElements, totalElements/nranks,
				conf.rbs, conf.cbs, nranks, threads, conf.timesteps, end-start, throughput);
	}

	if (conf.generateImage) {
		generateImage(&conf, rows, cols, rowsPerRank);
	}

	freeLPF();

	err = finalize(&conf);
	assert(!err);
}

void generateImage(const HeatConfiguration *conf, int64_t rows, int64_t cols, int64_t rowsPerRank)
{
	double *auxMatrix = NULL;

	if (!rank) {
		auxMatrix = (double *) malloc(rows*cols*sizeof(double));
		if (auxMatrix == NULL) {
			fprintf(stderr, "Memory cannot be allocated!\n");
			exit(1);
		}

		initializeMatrix(conf, auxMatrix, rows, cols, 0);
	}

	int count = (rowsPerRank-2)*cols;
	MPI_Gather(
		&conf->matrix[cols], count, MPI_DOUBLE,
		&auxMatrix[cols], count, MPI_DOUBLE,
		0, MPI_COMM_WORLD
	);

	if (!rank) {
		int err = writeImage(conf->imageFileName, auxMatrix, rows, cols);
		assert(!err);

		free(auxMatrix);
	}
}
