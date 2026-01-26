

#export OMP_NUM_THREADS=10
#export OMP_PLACES=cores
#export OMP_PROC_BIND=true

sudo jetson_clocks

sudo OMP_NUM_THREADS=8 ./FCS

