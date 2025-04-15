#!/bin/sh

# Check for exactly 2 arguments: suite and repl_policy.
if [ "$#" -ne 2 ]; then
    echo ""
    echo "Usage: ./run_all.sh <suite> <repl_policy>"
    echo "    (suite) benchmarks: "
    echo "      -- SPEC: bzip2 gcc mcf hmmer sjeng libquantum xalancbmk milc cactusADM leslie3d namd soplex calculix lbm"
    echo "      -- PARSEC: blackscholes bodytrack canneal dedup fluidanimate freqmine streamcluster swaptions x264"
    echo "    repl_policy: LRU LFU SRRIP SHIP"
    exit 1
fi

suite=$1
repl=$2

if [ "$suite" = "SPEC" ]; then
    # List of SPEC benchmarks.
    SPEC_BENCHS="bzip2 gcc mcf hmmer xalan milc cactusADM leslie3d namd calculix sjeng libquantum soplex lbm"
    for bench in $SPEC_BENCHS; do
        # Create the output directory for this benchmark.
        mkdir -p outputs/hw4/$repl/${bench}
        # Build the command (for logging) and run zsim in background.
        echo "./build/opt/zsim configs/hw4/$repl/${bench}.cfg > outputs/hw4/$repl/${bench}/${bench}.log 2>&1 &"
        ./build/opt/zsim configs/hw4/$repl/${bench}.cfg > outputs/hw4/$repl/${bench}/${bench}.log 2>&1 & 
    done
elif [ "$suite" = "PARSEC" ]; then
    # List of PARSEC benchmarks.
    PARSEC_BENCHS="blackschoels bodytrack fluidanimate streamcluster swaptions canneal x264"
    for bench in $PARSEC_BENCHS; do
        # Create the output directory; note the PARSEC configs use an _8c_simlarge suffix.
        mkdir -p outputs/hw4/$repl/${bench}_8c_simlarge
        echo "./build/opt/zsim configs/hw4/$repl/${bench}_8c_simlarge.cfg > outputs/hw4/$repl/${bench}_8c_simlarge/${bench}.log 2>&1 &"
        ./build/opt/zsim configs/hw4/$repl/${bench}_8c_simlarge.cfg > outputs/hw4/$repl/${bench}_8c_simlarge/${bench}.log 2>&1 &
    done
else
    echo "No such benchmark suite, please specify SPEC or PARSEC"
    exit 1
fi

# Wait for all background jobs to finish.
wait

