import numpy as np
import h5py
import os

def get_stat(policy, benchmark):
    stat_file = h5py.File(os.path.join(policy, benchmark, "zsim-ev.h5"), 'r')
    dataset = stat_file["stats"]["root"]

    total_l3_misses = np.sum(dataset[-1]['l3']['mGETS']) + np.sum(dataset[-1]['l3']['mGETXIM']) + np.sum(dataset[-1]['l3']['mGETXSM'])
    total_instructions = np.sum(dataset[-1]['westmere']['instrs'])
    total_cycles = np.sum(dataset[-1]['westmere']['cycles']) + np.sum(dataset[-1]['westmere']['cCycles'])

    mpki = (total_l3_misses * 1000 / total_instructions) if total_instructions > 0 else 0.0
    ipc  = total_instructions / total_cycles if total_instructions > 0 and total_cycles > 0 else 0.0

    print(f"  {benchmark}:")
    print("    MPKI:   {:.2f}".format(mpki))
    print("    IPC:    {:.2f}".format(ipc))
    print(f"    Cycles: {total_cycles}")
    print("-------------------------")

policies = ["LFU", "LRU", "SRRIP"]

PARSEC_benchmarks = ["blackscholes_8c_simlarge", "bodytrack_8c_simlarge", "canneal_8c_simlarge", "fluidanimate_8c_simlarge", "streamcluster_8c_simlarge", "swaptions_8c_simlarge", "x264_8c_simlarge"]
for benchmark in PARSEC_benchmarks:
    for policy in policies:
        print(f"{policy:}")
        get_stat(policy, benchmark)

SPEC_benchmarks = ["bzip2", "cactusADM", "calculix", "gcc", "hmmer", "lbm", "libquantum", "mcf", "namd", "sjeng", "soplex", "xalan"]
for benchmark in SPEC_benchmarks:
    for policy in policies:
        print(f"{policy:}")
        get_stat(policy, benchmark)