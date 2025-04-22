#ifndef HAWKEYE_REPL_H_
#define HAWKEYE_REPL_H_

#include <vector>
#include <cstdint>
#include <algorithm> // for std::min, std::max
#include <random>    // for set dueling sampling
#include "repl_policies.h"

#define cache_averse 7
#define cache_friendly 0

#define OPTGEN_SIZE 128

using std::vector;

/*OPTgen -- stores fixed history of past access, simulates belady in the past 
         -- tells predictor if it is a hit or miss
        includes
            -- occupancy vector - every cache set, shows number of active lines in that set
                                - has a sampled cache that tracks history
*/


class optgen {
    private:
        uint64_t cache_size;          // total capacity in lines
        uint64_t set_count;           // number of sets in the cache
        uint32_t hist_idx;            // global time pointer into history/occupancy vectors
        uint32_t vector_size;         // length of history/occupancy per sampled set
    
        // Granularity parameter: number of accesses per time-quantum
        static constexpr uint32_t TIME_QUANTUM = 4;
        // Number of sets to sample for set dueling
        static constexpr uint32_t SAMPLE_SETS = 64;
    
        // Per-set counters to track when to advance time-quantum
        vector<uint32_t> access_count;
        // Flags for which sets are sampled
        vector<bool> is_sampled;
    
        // For each set, maintain a history buffer and occupancy vector
        // We allocate for all sets but only use for sampled ones
        vector<vector<uint32_t>> history;
        vector<vector<uint32_t>> occupancy_vector;
    
        // Wrap an index in the circular buffer
        inline int32_t wrap_index(int32_t idx) const {
            return (idx + vector_size) % vector_size;
        }
    
    public:
        explicit optgen(uint64_t _cache_size, uint64_t _set_count)
            : cache_size(_cache_size), set_count(_set_count), hist_idx(0)
        {
            // Compute reduced vector size using time quantization
            // Original vector_size = associativity * 8
            // With granularity, divide by TIME_QUANTUM
            vector_size = (cache_size / set_count * 8) / TIME_QUANTUM;
            if (vector_size == 0) vector_size = 1;
    
            // Initialize data structures
            history.resize(set_count, vector<uint32_t>(vector_size, 0));
            occupancy_vector.resize(set_count, vector<uint32_t>(vector_size, 0));
            access_count.resize(set_count, 0);
            is_sampled.resize(set_count, false);
    
            // Set Dueling: randomly select SAMPLE_SETS sets for OPT simulation
            std::mt19937 rng(0);  // fixed seed for reproducibility
            std::uniform_int_distribution<uint32_t> dist(0, set_count - 1);
            uint32_t sampled = 0;
            while (sampled < SAMPLE_SETS) {
                uint32_t s = dist(rng);
                if (!is_sampled[s]) {
                    is_sampled[s] = true;
                    sampled++;
                }
            }
            // Non-sampled sets will be skipped in simulation
        }
    
        // Check if address would have been in cache under OPT for this set
        bool is_in_cache(uint32_t address, uint32_t set) {
            // Set Dueling: only process sampled sets
            if (!is_sampled[set])
                return false;
    
            int32_t prev_idx = -1, curr_idx = -1;
            auto& hist = history[set];
            auto& occ  = occupancy_vector[set];
    
            // Scan history buffer for last two accesses to this address
            for (uint32_t i = 1; i <= vector_size; ++i) {
                uint32_t idx = wrap_index(hist_idx - i);
                if (hist[idx] == address) {
                    if (prev_idx == -1)
                        prev_idx = idx;
                    else if (curr_idx == -1) {
                        curr_idx = idx;
                        break;
                    }
                }
            }
            if (prev_idx == -1 || curr_idx == -1)
                return false; // Not enough history
    
            // Check if occupancy between prev and curr would exceed associativity
            int32_t idx = wrap_index(prev_idx + 1);
            while (idx != curr_idx) {
                if (occ[idx] >= (size_t)(cache_size / set_count))
                    return false; // Would have been a miss
                idx = wrap_index(idx + 1);
            }
    
            // Update occupancy between prev and current
            idx = wrap_index(prev_idx + 1);
            while (idx != curr_idx) {
                occ[idx]++;
                idx = wrap_index(idx + 1);
            }
    
            return true;
        }
    
        // Record a cache access for OPT simulation
        void cache_access(uint32_t address, uint32_t set) {
            // Set Dueling: only process sampled sets
            if (!is_sampled[set])
                return;
    
            // Granularity: advance history only every TIME_QUANTUM accesses
            access_count[set]++;
            if (access_count[set] % TIME_QUANTUM != 0)
                return;
    
            hist_idx = wrap_index(hist_idx + 1);                // move time pointer
            occupancy_vector[set][hist_idx] = 0;                // reset occupancy
            history[set][hist_idx] = address;                   // record address
        }
};
    
/*    Hawkeye Predictor
-- look up table to check if the cache insertion s friendly or averse*/
#define map_size 8192  // 8000 entries, each 3 bits wide

class hawkeye_predictor {
private:
    uint8_t* predictor_map;

    uint32_t hash_instruction(uint32_t PC) const {
        // A simple hash function (can be improved depending on use case)
        return PC % (map_size - 1);
    }

public:
    hawkeye_predictor() {
        predictor_map = gm_calloc<uint8_t>(map_size);
    }

    ~hawkeye_predictor() {
        gm_free(predictor_map);
    }

    bool predict_instruction(uint32_t PC) {
        uint32_t index = hash_instruction(PC);
        uint8_t value = predictor_map[index] & 0x7;  // extract 3-bit value
        return (value & 0x4) != 0;  // check if the most significant bit (bit 2) is 1
    }
    //trains the hash... 1 for positively train, 0 for negatively trained. takes in the PC
    void train_instruction(uint32_t PC, bool taken) {
        uint32_t idx = hash_instruction(PC);
        uint8_t ctr = predictor_map[idx] & 0x7;
        if (taken) { 
            if (ctr < 7) ctr++;
        } else {
            if (ctr > 0) ctr--;
        }
        predictor_map[idx] = (predictor_map[idx] & ~0x7) | ctr;
    }
};

// Hawkeye Replacement policy
class HawkeyeReplPolicy : public ReplPolicy {
    protected:
        //meta data to go through information
        uint32_t numLines;
        //status of all caches
        int8_t* array;
        uint32_t numWays;
        //Used for hawkeye
        optgen Opt_Gen;
        hawkeye_predictor predictor;

    public:
        // add member methods here, refer to repl_policies.h
        explicit HawkeyeReplPolicy(uint32_t _numLines, uint32_t _numWays) : numLines(_numLines), numWays(_numWays), Opt_Gen(_numLines, _numWays), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t>(numLines);
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] = cache_averse;
            }
        }

        ~HawkeyeReplPolicy() {
            gm_free(array);
        }

        void update(uint32_t id, const MemReq* req) {
            //check to make sure that srcId reall stands for what cache way 
            assert(req->srcId < numWays);
            bool prediction = Opt_Gen.is_in_cache(req->lineAddr, req->srcId);
            predictor.train_instruction(req->pc, prediction);
            if (array[id] == cache_averse + 1) {//cache miss
                array[id] -= 2;
            } else {
                Opt_Gen.cache_access(req->lineAddr, req->srcId);
                array[id] = cache_friendly;
            } 
        }
        void replaced(uint32_t id) {
            array[id] = cache_averse + 1;
        }

        //find a victim, uses RRIP
        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            //search for a cache adverse candidate
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                if(array[*ci] == (int8_t)cache_averse) {
                    return *ci;// Evict first candidate with max RRP
                }
            }
            //if no cache adverse lines are found... look for the next max one
            for(uint32_t i = 0; i <= cache_averse; i++) {
                //search for a max value cache friedly one
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    if(array[*ci] == (int8_t)(cache_averse - 1)) {
                        return *ci;// Evict first candidate with max RRP
                    }
                }
                //no match found increment all status
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    //cache adverse tag can only set by the predictor
                    if(array[*ci] < (int8_t)(cache_averse-1))
                        array[*ci]++;
                }
            }
            info("error no rank found\n");
            return -1;
        }
        DECL_RANK_BINDINGS;
};
            
#endif // HAWKEYE_REPL_H_
