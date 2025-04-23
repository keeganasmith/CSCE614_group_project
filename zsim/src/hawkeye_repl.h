#ifndef HAWKEYE_REPL_H_
#define HAWKEYE_REPL_H_

#include <vector>
#include <cmath>
#include <iostream>
#include "hawkeye_helper.h"
#include "repl_policies.h"

#define cache_averse 7
#define cache_friendly 0

using std::vector;

/*OPTgen -- stores fixed history of past access, simulates belady in the past 
         -- tells predictor if it is a hit or miss
        includes
            -- occupancy vector - every cache set, shows number of active lines in that set
                                - has a sampled cache that tracks history
*/


// Hawkeye Replacement policy
class HawkeyeReplPolicy : public ReplPolicy {
    protected:
        //meta data to go through information
        uint32_t numSets;
        uint32_t numWays;

        uint32_t lineSize; //in powers of 2, used in binary
        uint32_t setSize;

        //status of all caches
        int8_t* array;

        vector<vector<uint32_t>> history; //vector holding the history for each way
        uint32_t* hist_idx_arr;            // global time pointer into history/occupancy vectors
        uint32_t hist_size;

        //Used for hawkeye
        vector<optgen> opt_sims;
        hawkeye_predictor predictor;

        inline int32_t wrap_index(int32_t idx) const {
            return idx % (numWays * 8);
        }
    public:
        // add member methods here, refer to repl_policies.h
        explicit HawkeyeReplPolicy(uint32_t _numSets, uint32_t _numWays, uint32_t _lineSize) 
            : numSets(_numSets), numWays(_numWays), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t>(numSets * numWays);
            for(uint32_t i = 0; i < numSets * numWays; i++) {
                array[i] = cache_averse;
            }
            //set up the history, the history size must be 8x the size of the set
            hist_idx_arr = gm_calloc<uint32_t>(numSets);
            hist_size = numWays * 8;
            history.resize(numSets, vector<uint32_t>(hist_size, 0));

            //set up each optgen
            for (uint32_t i = 0; i < numSets; i++) {
                opt_sims.emplace_back(numWays);
            }

            //calculate the log2 for numSets and lineSize for set index extraction
            lineSize = static_cast<uint32_t>(std::log2(_lineSize));
            setSize = static_cast<uint32_t>(std::log2(numSets));

            //std::cout << "line size: " << _lineSize << " in bits: " << lineSize << std::endl;
            //std::cout << "Set size:  " << numSets << " in bits: " << setSize << std::endl;

        }

        ~HawkeyeReplPolicy() {
            gm_free(array);
            gm_free(hist_idx_arr);
        }

        void update(uint32_t id, const MemReq* req) {
            //find the set index
            uint32_t set = calculate_set_idx(req->lineAddr);
            assert(set < numSets);

            //log the cache interaction
            uint32_t idx = wrap_index(hist_idx_arr[set]);
            history[set][idx] = req->lineAddr;
            opt_sims[set].cache_access(req->lineAddr);

            //predict the new cache value to be in the RRIP
            bool new_cache_val = predictor.predict_instruction(req->pc);
            array[id] = new_cache_val ? cache_friendly : cache_averse;

            //Train the predictor with reuse info, if we saw this line before
            int32_t prev_idx = last_used_addr(req->lineAddr, set);
            if (prev_idx != -1) {
                bool prediction = opt_sims[set].is_in_cache(prev_idx, hist_idx_arr[set]);
                predictor.train_instruction(req->pc, prediction);
            }

            hist_idx_arr[set] = wrap_index(hist_idx_arr[set] + 1);

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

        uint32_t calculate_set_idx(uint32_t lineAddr) {
            //need the cache block size, lineSize,, bit shift the to the right
            uint32_t setIdx = lineAddr >> lineSize;
            //filter the rest out
            uint32_t mask = (1U << setSize) - 1; // Create a mask with setSize number of 1's
            setIdx = setIdx & mask;
            assert(setIdx < numSets);
            return setIdx;
        }
        //looks for the last use of the address in the history of that set
        uint32_t last_used_addr(uint32_t address, uint32_t set) { 
            // Scan history buffer for last two accesses to this address
            for (uint32_t i = 1; i <= hist_size; ++i) {
                uint32_t idx = wrap_index(hist_idx_arr[set] - i);
                if (history[set][idx] == address) { 
                     return idx;
                }
            }
            return -1;
        }
};
            
#endif // HAWKEYE_REPL_H_