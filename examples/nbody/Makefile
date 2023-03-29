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
nbody_talpf_default.$(BIGO).$(BS)bs.bin \
nbody_talpf_cached.$(BIGO).$(BS)bs.bin \
nbody_talpf_cached_ack.$(BIGO).$(BS)bs.bin

all: $(BIN)

nbody_talpf_default.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/solver_talpf_default.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(OVNI_FLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)

nbody_talpf_cached.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/solver_talpf_cached.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(OVNI_FLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)

nbody_talpf_cached_ack.$(BIGO).$(BS)bs.bin: $(TALPF_SOURCES) src/solver_talpf_cached_ack.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(OVNI_FLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS)


clean:
	rm -f *.o *.bin
