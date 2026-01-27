

#export OMP_NUM_THREADS=10
#export OMP_PLACES=cores
#export OMP_PROC_BIND=true

sudo jetson_clocks

#sudo OMP_NUM_THREADS=8 ~/emc_wrapper.sh \
#/opt/nvidia/nsight-systems/2024.5.4/bin/nsys profile -t cuda,osrt,nvtx -y 15 -d 0.1 \
sudo ~/emc_wrapper.sh ./FCS

