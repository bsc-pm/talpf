

#module load gcc/11.2.0

export LPF_HOME=$HOME/lpf/
export ompss_2_HOME=/apps/PM/ompss-2/git/
export ovni_HOME=/apps/PM/ovni/git/

export PATH=$LPF_HOME/bin:$PATH
export PATH=$ompss_2_HOME/bin:$PATH
export PATH=$ovni_HOME/bin:$PATH

export LD_LIBRARY_PATH=$LPF_HOME/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$ompss_2_HOME/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$ovni_HOME/lib:$LD_LIBRARY_PATH
