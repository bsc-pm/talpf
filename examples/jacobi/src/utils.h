#ifndef MPI_UTILS_H
#define MPI_UTILS_H

#include <lpf/core.h>
#include <mpi.h>

#include <stdlib.h>
#include <stdio.h>

#include "heat.h"

#define CHECK(f...)                                                       \
    {                                                                     \
        const lpf_err_t __r = f;                                          \
        if (__r != LPF_SUCCESS) {                                         \
            printf("Error: '%s' [%s:%i]: %i\n",#f,__FILE__,__LINE__,__r); \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    }

extern lpf_t lpf;
extern lpf_pid_t rank;
extern lpf_pid_t nranks;

extern lpf_memslot_t UPPER_HALO[2];
extern lpf_memslot_t LOWER_HALO[2];
extern lpf_memslot_t RESIDUAL;

void broadcastConfiguration(HeatConfiguration *configuration);
void setupLPF(const HeatConfiguration *conf, int64_t rows, int64_t cols);
void freeLPF(void);

#endif // MPI_UTILS_H
