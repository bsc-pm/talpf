#!/bin/bash
#SBATCH --job-name="heat"
#SBATCH -D .
#SBATCH --output=out_%j.out
#SBATCH --error=err_%j.err
#SBATCH --time=00:30:00

N=$SLURM_JOB_NUM_NODES
c=$SLURM_CPUS_PER_TASK
n=$SLURM_NPROCS
export TALPF_POLLING_FREQUENCY=100
export LPF_ENGINE="ibverbs"

#4 ranks per node
NANOS6_CONFIG_OVERRIDE="version.instrument=ovni" srun -n $((4*$n)) -N $N -c $(($c/4)) ./heat_talpf_cached_sleep.bin -c $((1024 * 16)) -r $((1024 * 16)) -b 512 -t 10
srun -n $n -N $N -c $c ovnisync
