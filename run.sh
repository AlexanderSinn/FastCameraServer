

#export OMP_NUM_THREADS=10
#export OMP_PLACES=cores
#export OMP_PROC_BIND=true

sudo jetson_clocks

# sudo systemctl stop gdm

#sudo OMP_NUM_THREADS=8 ~/emc_wrapper.sh \
#/opt/nvidia/nsight-systems/2024.5.4/bin/nsys profile -t cuda,osrt,nvtx ./FCS

sudo ~/emc_wrapper.sh ./FCS

# sudo systemctl start gdm





# cd /home/jetson-user/Desktop/jetson_doocs
# /opt/nvidia/nsight-systems/2024.5.4/bin/nsys profile -t cuda,osrt,nvtx -y 30 -d 10 \
# ./builddir/jetson_doocs_server -c ./jetson_doocs.conf
