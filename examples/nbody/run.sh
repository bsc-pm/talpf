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

echo "TALPF (DEFAULT)"
srun -n $n -N $N -c $c ./nbody_talpf_default.N2.512bs.bin -p $((1024 * 64)) -t 100 
echo "TALPF (CACHED)"
srun -n $n -N $N -c $c ./nbody_talpf_cached.N2.512bs.bin -p $((1024 * 64)) -t 100
echo "TALPF (ACK)"
srun -n $n -N $N -c $c ./nbody_talpf_cached_ack.N2.512bs.bin -p $((1024 * 64)) -t 100
