#include "nbody.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <lpf/mpi.h>
#include <mpi.h>

#ifdef _OMPSS_2
#include <nanos6/debug.h>
#endif


const int LPF_MPI_AUTO_INITIALIZE = 0;

void application(lpf_t ctx, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args);

int main(int argc, char** argv)
{
	const int required = MPI_THREAD_SERIALIZED;

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
        lpf_mpi_initialize_with_mpicomm(MPI_COMM_WORLD, &init);

        lpf_hook(init, &application, args);

        lpf_mpi_finalize(init);

        MPI_Finalize();

}
void application(lpf_t ctx, lpf_pid_t pid, lpf_pid_t nprocs, lpf_args_t args)
{
	char **argv = (char **) args.input;
        int argc = args.input_size;
	int err;
	
	lpf = ctx;
	rank = pid;
	nranks = nprocs;
	
	nbody_conf_t conf;
	if (!rank) {
		nbody_get_conf(argc, argv, &conf);
	}
	MPI_Bcast(&conf, sizeof(nbody_conf_t), MPI_BYTE, 0, MPI_COMM_WORLD);
	assert(conf.num_particles > 0);
	assert(conf.timesteps > 0);
	


	int total_particles = ROUNDUP(conf.num_particles, MIN_PARTICLES);
	int my_particles = total_particles / nranks;
	assert(my_particles >= BLOCK_SIZE);
	conf.num_particles = my_particles;
	
	conf.num_blocks = my_particles / BLOCK_SIZE;
	assert(conf.num_blocks > 0);
	
	nbody_t nbody = nbody_setup(&conf);

	setupLPF(&nbody);
	
	double start = get_time();
	nbody_solve(&nbody, conf.num_blocks, conf.timesteps, conf.time_interval);
	double end = get_time();
	
	if(rank == 0) fprintf(stderr, "%f\n", nbody_compute_throughput(total_particles, (&nbody)->timesteps, end - start));
	nbody_stats(&nbody, &conf, end - start);
	
	if (conf.save_result) nbody_save_particles(&nbody);
	if (conf.check_result) nbody_check(&nbody);
	nbody_free(&nbody);

	freeLPF();

       // err = finalize(&conf);
        //assert(!err);
	
}
		
