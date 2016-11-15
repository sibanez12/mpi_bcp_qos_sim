#!/bin/bash

rm -rf plots/*
cd ../ && tar -czf mpi_bcp_qos_sim.tar.gz mpi_bcp_qos_sim
ssh ${USER}@maverick-1.stanford.edu 'rm -rf workspace/mpi_bcp_qos_sim*'
scp mpi_bcp_qos_sim.tar.gz ${USER}@maverick-1.stanford.edu:~/workspace/
ssh ${USER}@maverick-1.stanford.edu 'cd ~/workspace && tar -xzvf mpi_bcp_qos_sim.tar.gz && cd mpi_bcp_qos_sim/Debug && make clean && make'

