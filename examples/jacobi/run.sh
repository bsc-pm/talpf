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
#export LPF_LOG_LEVEL=1

echo "TALPF (DEFAULT):"
srun -n $n -N $N -c $c ./heat_talpf_default.bin -c $((1024 * 16)) -r $((1024 * 16)) -b 512 -t 100
echo "TALPF (CACHED):"
srun -n $n -N $N -c $c ./heat_talpf_cached.bin -c $((1024 * 16)) -r $((1024 * 16)) -b 512 -t 100
echo "TALPF (SLEEP):"
srun -n $n -N $N -c $c ./heat_talpf_cached_sleep.bin -c $((1024 * 16)) -r $((1024 * 16)) -b 512 -t 100
