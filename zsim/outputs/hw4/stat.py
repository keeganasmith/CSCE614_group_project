import numpy as np
import matplotlib.pyplot as plt

import h5py
import os
def get_results_for_stat(benchmark_stat_mapping, stat):
    benchmarks = list(benchmark_stat_mapping['LRU'].keys())
    results = {}
    for benchmark in benchmarks:
        values = []
        for policy in benchmark_stat_mapping.keys():
            values.append(benchmark_stat_mapping[policy][benchmark][stat])
        max_val = max(values)
        for policy in benchmark_stat_mapping.keys():
            if(results.get(policy, None) is None):
                results[policy] = []
            results[policy].append(benchmark_stat_mapping[policy][benchmark][stat] / max_val)
    return results    

def graph_results(benchmark_stat_mapping, identifier):
    results = {}
    stats = []
    policies = list(benchmark_stat_mapping.keys());
    benchmarks = list(benchmark_stat_mapping[policies[0]].keys())
    stats = list(benchmark_stat_mapping[policies[0]][benchmarks[0]].keys()) 
    for stat in stats:
        results[stat] = get_results_for_stat(benchmark_stat_mapping, stat)

    x = np.arange(len(benchmarks))
    width = 0.25

    colors = ["blue", "red", "green"]
    for stat in stats:
        plt.figure(figsize=(12, 6))
        index = 0
        for policy in policies:    
            offset = (index - len(policies) / 2) * width
            plt.bar(x + offset, results[stat][policy], width, label=policy,color=colors[index % len(colors)],alpha=0.7)
            index += 1

        plt.xlabel(f"{identifier} Benchmarks")
        plt.ylabel(f"{stat} (Normalized)")
        plt.title(f"{stat} Comparison")
        plt.xticks(ticks=x, labels=benchmarks, rotation=45)
        plt.legend()
        plt.grid(axis='y', linestyle='--', alpha=0.6)
        plt.tight_layout()
        file_name = f"./graphs/{stat}_{identifier}.png"
        plt.savefig(file_name)
        print("Saved ", file_name)


def get_stat(policy, benchmark):
    stat_file = h5py.File(os.path.join(policy, benchmark, "zsim-ev.h5"), 'r')
    dataset = stat_file["stats"]["root"]

    total_l3_misses = np.sum(dataset[-1]['l3']['mGETS']) + np.sum(dataset[-1]['l3']['mGETXIM']) + np.sum(dataset[-1]['l3']['mGETXSM'])
    total_instructions = np.sum(dataset[-1]['westmere']['instrs'])
    total_cycles = np.sum(dataset[-1]['westmere']['cycles']) + np.sum(dataset[-1]['westmere']['cCycles'])

    mpki = (total_l3_misses * 1000 / total_instructions) if total_instructions > 0 else 0.0
    ipc  = total_instructions / total_cycles if total_instructions > 0 and total_cycles > 0 else 0.0

    print(f"  {benchmark}:")
    print("    MPKI:   {:.16f}".format(mpki))
    print("    IPC:    {:.16f}".format(ipc))
    print(f"    Cycles: {total_cycles}")
    print("-------------------------")
    return {
            "MPKI": mpki,
            "IPC": ipc,
            "Cycles": total_cycles
            }

policies = ["LFU", "LRU", "SHIP"]

PARSEC_benchmarks = ["blackscholes_8c_simlarge", "bodytrack_8c_simlarge", "canneal_8c_simlarge", "fluidanimate_8c_simlarge", "streamcluster_8c_simlarge", "swaptions_8c_simlarge", "x264_8c_simlarge"]
benchmark_stat_mapping = {}
for benchmark in PARSEC_benchmarks:
    for policy in policies:
        print(f"{policy:}")
        stats = get_stat(policy, benchmark)
        if(benchmark_stat_mapping.get(policy, None) is None):
            benchmark_stat_mapping[policy] = {}
        benchmark_stat_mapping[policy][benchmark] = stats
graph_results(benchmark_stat_mapping, "PARSEC")

benchmark_stat_mapping = {}
SPEC_benchmarks = ["bzip2", "gcc", "mcf", "hmmer", "xalan", "cactusADM", "namd", "calculix", "sjeng", "libquantum", "soplex","lbm"]
for benchmark in SPEC_benchmarks:
    for policy in policies:
        print(f"{policy:}")
        stats = get_stat(policy, benchmark)
        if(benchmark_stat_mapping.get(policy, None) is None):
            benchmark_stat_mapping[policy] = {}
        benchmark_stat_mapping[policy][benchmark] = stats
graph_results(benchmark_stat_mapping, "SPEC")


