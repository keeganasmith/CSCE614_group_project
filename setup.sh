#!/bin/bash

# HDF5 path
export PATH=${PWD}/tools/local/bin:$PATH

# Pin path
export PINPATH=${PWD}/tools/pin-2.14-71313-gcc.4.4.7-linux

# libconfig path
export LIBCONFIGPATH=${PWD}/tools/local

# PARSEC
export PARSEC=${PWD}/benchmarks/parsec-2.1
export PARSECBIN=${PWD}/benchmarks/parsec-2.1/bin
export PARSECINPUTS=${PWD}/benchmarks/parsec-2.1/inputs

# SPEC CPU2006
export CPU2006=${PWD}/benchmarks/spec_cpu2006
export CPU2006BIN=${PWD}/benchmarks/spec_cpu2006/bin
export CPU2006DATA=${PWD}/benchmarks/spec_cpu2006/data

# ZSim
export ZSIMPATH=${PWD}/zsim
export ZSIMHOOKS=${PWD}/zsim/misc/hooks
export PATH=/bin:$PATH
