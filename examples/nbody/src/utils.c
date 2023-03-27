#include "nbody.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <ieee754.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <mpi.h>
#include <lpf/core.h>

int rank;
int nranks;
lpf_t lpf;
lpf_memslot_t LOCAL;
lpf_memslot_t REMOTE1;
lpf_memslot_t REMOTE2;


void nbody_generate_particles(const nbody_conf_t *conf, const nbody_file_t *file)
{
	
	char fname[1024];
	sprintf(fname, "%s.in", file->name);
	
	if (!access(fname, F_OK)) {
		return;
	}
	
	struct stat st = {0};
	if (stat("data", &st) == -1) {
		mkdir("data", 0755);
	}
	
	const int fd = open(fname, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IRGRP|S_IROTH);
	assert(fd >= 0);
	
	const int total_size = file->total_size;
	assert(total_size % PAGE_SIZE == 0);
	
	int err = ftruncate(fd, total_size);
	assert(!err);
	
	particles_block_t * const particles = mmap(NULL, total_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
	
	for(int i = 0; i < conf->num_blocks * nranks; i++) {
		nbody_particle_init(conf, particles+i);
	}
	
	err = munmap(particles, total_size);
	assert(!err);
	
	err = close(fd);
	assert(!err);
}

void nbody_check(const nbody_t *nbody)
{
	char fname[1024];
	sprintf(fname, "%s.ref", nbody->file.name);
	if (access(fname, F_OK) != 0) {
		if (!rank) fprintf(stderr, "Warning: %s file does not exist. Skipping the check...\n", fname);
		return;
	}
	
	const int fd = open(fname, O_RDONLY, 0);
	assert(fd >= 0);
	
	particles_block_t *reference = mmap(NULL, nbody->file.size, PROT_READ, MAP_SHARED, fd, nbody->file.offset);
	assert(reference != MAP_FAILED);
	
	int correct, correct_chunk;
	correct_chunk = nbody_compare_particles(nbody->local, reference, nbody->num_blocks);
	
	MPI_Reduce(&correct_chunk, &correct, 1, MPI_INT, MPI_LAND, 0, MPI_COMM_WORLD);
	
	if (!rank) {
		if (correct) {
			printf("Result validation: OK\n");
		} else {
			printf("Result validation: ERROR\n");
		}
	}
	
	int err = munmap(reference, nbody->file.size);
	assert(!err);
	
	err = close(fd);
	assert(!err);
}

nbody_file_t nbody_setup_file(const nbody_conf_t *conf)
{
	
	nbody_file_t file;
	file.size = conf->num_blocks * sizeof(particles_block_t);
	file.total_size = nranks * file.size;
	file.offset = rank * file.size;
	
	sprintf(file.name, "%s-%s-%d-%d-%d", conf->name, TOSTRING(BIGO), nranks * conf->num_blocks * BLOCK_SIZE, BLOCK_SIZE, conf->timesteps);
	return file;
}

particles_block_t * nbody_load_particles(const nbody_conf_t *conf, const nbody_file_t *file)
{
	char fname[1024];
	sprintf(fname, "%s.in", file->name);
	
	const int fd = open(fname, O_RDONLY, 0);
	assert(fd >= 0);
	
	void * const ptr = mmap(NULL, file->size, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, file->offset);
	assert(ptr != MAP_FAILED);
	
	int err = close(fd);
	assert(!err);
	
	return ptr;
}

nbody_t nbody_setup(const nbody_conf_t *conf)
{
	nbody_file_t file = nbody_setup_file(conf);
	
	if (!rank) nbody_generate_particles(conf, &file);
	#pragma oss task label("sync")
        talpf_sync(lpf, LPF_SYNC_DEFAULT);
	#pragma oss taskwait
	//MPI_Barrier(MPI_COMM_WORLD);
	
	nbody_t nbody;
	nbody.local = nbody_load_particles(conf, &file);
	assert(nbody.local != NULL);
	
	nbody.remote1 = nbody_alloc(conf->num_blocks * sizeof(particles_block_t));
	nbody.remote2 = nbody_alloc(conf->num_blocks * sizeof(particles_block_t));
	assert(nbody.remote1 != NULL);
	assert(nbody.remote2 != NULL);
	
	nbody.forces = nbody_alloc(conf->num_blocks * sizeof(forces_block_t));
	assert(nbody.forces != NULL);
	
	nbody.num_blocks = conf->num_blocks;
	nbody.timesteps = conf->timesteps;
	nbody.file = file;
	
	return nbody;
}

void nbody_save_particles(const nbody_t *nbody)
{
	
	char fname[1024];
	sprintf(fname, "%s.out", nbody->file.name);
	const int mode = MPI_MODE_CREATE | MPI_MODE_WRONLY;
	
	MPI_File outfile;
	int err = MPI_File_open(MPI_COMM_WORLD, fname, mode, MPI_INFO_NULL, &outfile);
	assert(err == MPI_SUCCESS);
	
	MPI_File_set_view(outfile, nbody->file.offset, MPI_BYTE, MPI_BYTE, "native", MPI_INFO_NULL);
	assert(err == MPI_SUCCESS);
	
	MPI_File_write(outfile, nbody->local, nbody->file.size, MPI_BYTE, MPI_STATUS_IGNORE);
	assert(err == MPI_SUCCESS);
	
	MPI_File_close(&outfile);
	assert(err == MPI_SUCCESS);
	
	MPI_Barrier(MPI_COMM_WORLD);
}

void nbody_free(nbody_t *nbody)
{
	const int particles_size = nbody->num_blocks * sizeof(particles_block_t);
	const int forces_size = nbody->num_blocks * sizeof(forces_block_t);
	
	int err = munmap(nbody->local, particles_size);
	err |= munmap(nbody->remote1, particles_size);
	err |= munmap(nbody->remote2, particles_size);
	err |= munmap(nbody->forces, forces_size);
	assert(!err);
}


void setupLPF(const nbody_t *nbody)
{
	const int particles_size = nbody->num_blocks * sizeof(particles_block_t);

        lpf_resize_message_queue(lpf, 3 *  nbody->num_blocks);
        lpf_resize_memory_register(lpf, 3);

        #pragma oss task label("sync")
        talpf_sync(lpf, LPF_SYNC_DEFAULT);
        #pragma oss taskwait

        lpf_register_global(lpf, &nbody->local[0], particles_size, &LOCAL);
        lpf_register_global(lpf, &nbody->remote1[0], particles_size, &REMOTE1);
        lpf_register_global(lpf, &nbody->remote2[0], particles_size, &REMOTE2);

        #pragma oss task label("sync")
        talpf_sync(lpf, LPF_SYNC_DEFAULT);
        #pragma oss taskwait

}

void freeLPF(void)
{
        lpf_deregister(lpf, LOCAL);
        lpf_deregister(lpf, REMOTE1);
        lpf_deregister(lpf, REMOTE2);
}

