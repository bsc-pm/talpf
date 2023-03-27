# Compilers
CC=gcc
MCC=mcc
LPFCC=lpfcc
MPI_WRAPPERS=I_MPI_CC=$(MCC) MPICH_CC=$(MCC) OMPI_CC=$(MCC)
MCC_WRAPPERS=I_MPI_CC=$(MCC) MPICH_CC=$(MCC) OMPI_CC=$(MCC) LPF_CC=$(MCC)


# Nbody parameters
BIGO?=N2
BS?=512

# Preprocessor flags
CPPFLAGS= -Isrc -DBIGO=$(BIGO) -D_BIGO_$(BIGO) -DBLOCK_SIZE=$(BS)

# Compiler flags
CFLAGS= -Ofast -march=native -ffast-math -std=gnu99   #-O3 -std=gnu11
MCCFLAGS= --ompss-2 --Wn,-Ofast,-march=native,-ffast-math --Wl,-Ofast,-march=native,-ffast-math -std=gnu99  #--ompss-2 $(CFLAGS)

# Linker flags
LDFLAGS= -lrt -lm

LPF_CFLAGS=-engine ibverbs
OVNI_FLAGS=-L/apps/PM/ovni/git/lib -I/apps/PM/ovni/git/include -lovni

TALPF_SOURCES=                          \
    src/common.c			\
    src/common_utils.c	\
    src/utils.c		\
    src/main.c

BIN=	\
08.nbody_talpf_default_ompss_tasks.$(BIGO).$(BS)bs.bin \
09.nbody_talpf_cached_ompss_tasks.$(BIGO).$(BS)bs.bin \
10.nbody_talpf_cached_ack_ompss_tasks.$(BIGO).$(BS)bs.bin

all: $(BIN)

08.nbody_talpf_default_ompss_tasks.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/08.solver_talpf_default_ompss_tasks.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(OVNI_FLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)

09.nbody_talpf_cached_ompss_tasks.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/09.solver_talpf_cached_ompss_tasks.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)

10.nbody_talpf_cached_ack_ompss_tasks.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/10.solver_talpf_cached_ack_ompss_tasks.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)


clean:
	rm -f *.o *.bin
