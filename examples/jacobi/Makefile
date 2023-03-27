# Compilers
CC=gcc
MCC=mcc
LPFCC=lpfcc
CC_WRAPPERS=I_MPI_CC=$(CC) MPICH_CC=$(CC) OMPI_CC=$(CC) LPF_CC=$(CC)
MCC_WRAPPERS=I_MPI_CC=$(MCC) MPICH_CC=$(MCC) OMPI_CC=$(MCC) LPF_CC=$(MCC)


# Preprocessor flags
CPPFLAGS=-Isrc

# Compiler flags
CFLAGS=-Ofast -ffast-math -std=gnu99
#CFLAGS=-Ofast -march=native -ffast-math -std=gnu99
MCCFLAGS=--ompss-2 --Wn,-Ofast,-march=native,-ffast-math --Wl,-Ofast,-march=native,-ffast-math -std=gnu99

# Kernel flags
KFLAGS=$(CFLAGS)

# Linker flags
LDFLAGS=-lrt -lm

# TAMPI flags

# LPF flags
LPF_CFLAGS=-engine ibverbs
OVNI_FLAGS=-L/apps/PM/ovni/git/lib -I/apps/PM/ovni/git/include -lovni
# List of programs
BIN=heat_talpf_default.bin
BIN+=heat_talpf_cached.bin
BIN+=heat_talpf_cached_sleep.bin

# Sources
TALPF_SRC=src/misc.c src/utils.c src/main.c kernel.o

all: $(BIN)


heat_talpf_default.bin: $(TALPF_SRC) src/solver_talpf_default.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS) $(OVNI_FLAGS)

heat_talpf_cached.bin: $(TALPF_SRC) src/solver_talpf_cached.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS) $(OVNI_FLAGS)

heat_talpf_cached_sleep.bin: $(TALPF_SRC) src/solver_talpf_cached_sleep.c
	$(MCC_WRAPPERS) $(LPFCC) $(LPF_CFLAGS) $(CPPFLAGS) $(MCCFLAGS) -o $@ $^ $(LDFLAGS) $(OVNI_FLAGS)

kernel.o: src/kernel.c
	$(CC) $(CPPFLAGS) $(KFLAGS) -c -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o *.bin
