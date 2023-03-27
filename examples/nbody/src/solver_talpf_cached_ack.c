#include "nbody.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mpi.h>
#include <nanos6/debug.h>

static void calculate_forces(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2, const int num_blocks);
static void update_particles(particles_block_t *particles, forces_block_t *forces, const int num_blocks, const float time_interval);
static void exchange_particles(const particles_block_t *sendbuf, const lpf_memslot_t *sendmem, particles_block_t *recvbuf, lpf_memslot_t *recvmem, const int num_blocks, forces_block_t *forces);
static void calculate_forces_block(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2);
static void update_particles_block(particles_block_t *particles, forces_block_t *forces, const float time_interval);
static void exchange_particles_block(const particles_block_t *sendbuf, const lpf_memslot_t *sendmem, particles_block_t *recvbuf, lpf_memslot_t *recvmem, int block_id, forces_block_t *forces);

void nbody_solve(nbody_t *nbody, const int num_blocks, const int timesteps, const float time_interval)
{
	assert(nbody != NULL);
	assert(timesteps > 0);
	
	particles_block_t *local = nbody->local;
	lpf_memslot_t *l = &LOCAL;
	particles_block_t *remote1 = nbody->remote1;
	lpf_memslot_t *r1 = &REMOTE1;
	particles_block_t *remote2 = nbody->remote2;
	lpf_memslot_t *r2 = &REMOTE2;
	forces_block_t *forces = nbody->forces;
	
	for (int t = 0; t < timesteps; t++) {
		particles_block_t *sendbuf = local;
		lpf_memslot_t *sendmem = l;
		particles_block_t *recvbuf = remote1;
		lpf_memslot_t *recvmem = r1;
		
		for (int r = 0; r < nranks; r++) {
			calculate_forces(forces, local, sendbuf, num_blocks);
			if (r < nranks - 1) {
				exchange_particles(sendbuf, sendmem, recvbuf, recvmem, num_blocks, forces);
				#pragma oss task inout({recvbuf[I], I=0;num_blocks})\
					inout({forces[I], I=0;num_blocks}) \
					label("sync") 
				talpf_sync(lpf, LPF_SYNC_CACHED);// | LPF_SYNC_BARRIER);
			}
			else {
				#pragma oss task inout({recvbuf[I], I=0;num_blocks})\
					inout({forces[I], I=0;num_blocks}) \
					label("sync")
				talpf_sync(lpf, LPF_SYNC_MSG(0) | LPF_SYNC_BARRIER);
			}
	
			
			
			particles_block_t *aux = recvbuf;
			lpf_memslot_t *auxmem = recvmem;
			recvbuf = (r != 0) ? sendbuf : remote2;
			recvmem = (r != 0) ? sendmem : r2;
			sendbuf = aux;
			sendmem = auxmem;
		}
		
		update_particles(local, forces, num_blocks, time_interval);
	}
	
	#pragma oss taskwait

	#pragma oss task label("sync")
	talpf_sync(lpf, LPF_SYNC_DEFAULT);
	#pragma oss taskwait

}

void calculate_forces_N2(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2, const int num_blocks)
{
	for (int i = 0; i < num_blocks; i++) {
		for (int j = 0; j < num_blocks; j++) {
			calculate_forces_block(forces+i, block1+i, block2+j);
		}
	}
}

void calculate_forces_NlogN(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2, const int num_blocks)
{
	for (int i = 0; i < num_blocks; i++) {
		for (int j = 0; j < LOG2(num_blocks); j++) {
			calculate_forces_block(forces+i, block1+i, block2+j);
		}
	}
}

void calculate_forces_N(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2, const int num_blocks)
{
	for (int i = 0; i < num_blocks - 1; i++) {
		calculate_forces_block(forces+i, block1+i, block2+i+1);
	}
}

void update_particles(particles_block_t *particles, forces_block_t *forces, const int num_blocks, const float time_interval)
{
	for (int i = 0; i < num_blocks; i++) {
		update_particles_block(particles+i, forces+i, time_interval);
	}
}

void exchange_particles(const particles_block_t *sendbuf, const lpf_memslot_t *sendmem, particles_block_t *recvbuf, lpf_memslot_t *recvmem, const int num_blocks, forces_block_t *forces)
{
	for (int i = 0; i < num_blocks; i++) {
		exchange_particles_block(sendbuf+i, sendmem, recvbuf+i, recvmem, i, forces+i);
	}
}

#pragma oss task in(*block1, *block2) inout(*forces) label("calculate_forces_block")
void calculate_forces_block(forces_block_t *forces, const particles_block_t *block1, const particles_block_t *block2)
{
	float *x = forces->x;
	float *y = forces->y;
	float *z = forces->z;
	
	const int same_block = (block1 == block2);
	const float *pos_x1 = block1->position_x;
	const float *pos_y1 = block1->position_y;
	const float *pos_z1 = block1->position_z;
	const float *mass1  = block1->mass ;
	
	const float *pos_x2 = block2->position_x;
	const float *pos_y2 = block2->position_y;
	const float *pos_z2 = block2->position_z;
	const float *mass2  = block2->mass;
	
	for (int i = 0; i < BLOCK_SIZE; i++) {
		float fx = x[i], fy = y[i], fz = z[i];
		for (int j = 0; j < BLOCK_SIZE; j++) {
			const float diff_x = pos_x2[j] - pos_x1[i];
			const float diff_y = pos_y2[j] - pos_y1[i];
			const float diff_z = pos_z2[j] - pos_z1[i];
			
			const float distance_squared = diff_x * diff_x + diff_y * diff_y + diff_z * diff_z;
			const float distance = sqrtf(distance_squared);
			
			float force = 0.0f;
			if (!same_block || distance_squared != 0.0f) {
				force = (mass1[i] / (distance_squared * distance)) * (mass2[j] * gravitational_constant);
			}
			fx += force * diff_x;
			fy += force * diff_y;
			fz += force * diff_z;
		}
		x[i] = fx;
		y[i] = fy;
		z[i] = fz;
	}
}

#pragma oss task inout(*particles, *forces) label("update_particles_block")
void update_particles_block(particles_block_t *particles, forces_block_t *forces, const float time_interval)
{
	for (int e = 0; e < BLOCK_SIZE; e++){
		const float mass       = particles->mass[e];
		const float velocity_x = particles->velocity_x[e];
		const float velocity_y = particles->velocity_y[e];
		const float velocity_z = particles->velocity_z[e];
		const float position_x = particles->position_x[e];
		const float position_y = particles->position_y[e];
		const float position_z = particles->position_z[e];
		
		const float time_by_mass       = time_interval / mass;
		const float half_time_interval = 0.5f * time_interval;
		
		const float velocity_change_x = forces->x[e] * time_by_mass;
		const float velocity_change_y = forces->y[e] * time_by_mass;
		const float velocity_change_z = forces->z[e] * time_by_mass;
		const float position_change_x = velocity_x + velocity_change_x * half_time_interval;
		const float position_change_y = velocity_y + velocity_change_y * half_time_interval;
		const float position_change_z = velocity_z + velocity_change_z * half_time_interval;
		
		particles->velocity_x[e] = velocity_x + velocity_change_x;
		particles->velocity_y[e] = velocity_y + velocity_change_y;
		particles->velocity_z[e] = velocity_z + velocity_change_z;
		particles->position_x[e] = position_x + position_change_x;
		particles->position_y[e] = position_y + position_change_y;
		particles->position_z[e] = position_z + position_change_z;
	}
	
	memset(forces, 0, sizeof(forces_block_t));
}
#pragma oss task in(*sendbuf) out(*recvbuf) inout(*forces) label(exchange_particles_block)
void exchange_particles_block(const particles_block_t *sendbuf, const lpf_memslot_t *sendmem, particles_block_t *recvbuf, lpf_memslot_t *recvmem, int block_id, forces_block_t *forces)
{
	int dst = MOD(rank + 1, nranks);
	int src = MOD(rank - 1, nranks);
	int size = sizeof(particles_block_t);
	
	talpf_put(lpf, *sendmem, size*block_id, dst, *recvmem, size*block_id, size, LPF_MSG_DEFAULT);
	talpf_put(lpf, *sendmem, 0, src, *recvmem, 0, 0, LPF_MSG_DEFAULT);
}

void nbody_stats(const nbody_t *nbody, const nbody_conf_t *conf, double time)
{
	
	if (!rank) {
		int particles = nbody->num_blocks * BLOCK_SIZE;
		int total_particles = particles * nranks;
		
		printf("bigo, %s, processes, %d, threads, %d, timesteps, %d, total_particles, %d, particles_per_proc, %d, block_size, %d, blocks_per_proc, %d, time, %.2f, performance, %.2f\n",
			TOSTRING(BIGO), nranks, nanos6_get_num_cpus(), nbody->timesteps, total_particles, particles, BLOCK_SIZE,
			nbody->num_blocks, time, nbody_compute_throughput(total_particles, nbody->timesteps, time)
		);
	}
}
