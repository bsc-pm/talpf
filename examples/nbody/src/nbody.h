#ifndef NBODY_MPI_H
#define NBODY_MPI_H

#include "nbody_common.h"
#include <lpf/core.h>

// Application structures
struct nbody_file_t {
	size_t total_size;
	size_t size;
	size_t offset;
	char name[1000];
};

struct nbody_t {
	particles_block_t *local;
	particles_block_t *remote1;
	particles_block_t *remote2;
	forces_block_t *forces;
	int num_blocks;
	int timesteps;
	nbody_file_t file;
};

extern int rank;
extern int nranks;
extern lpf_t lpf;
extern lpf_memslot_t LOCAL;
extern lpf_memslot_t REMOTE1;
extern lpf_memslot_t REMOTE2;

void setupLPF(const nbody_t *nbody);
void freeLPF(void);

#endif // NBODY_MPI_H

