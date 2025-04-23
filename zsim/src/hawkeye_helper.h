#ifndef HAWKEYE_HELPER_H_
#define HAWKEYE_HELPER_H_

#include <vector>
#include <unordered_map>
#include "repl_policies.h"

using std::vector;

#define OCCUPANCY_VECTOR_MULTIPLIER 8
/*Optgen calculates the belady's algorithm using livness intervals with the occupancy vector
the size of the vector is the size of a set(ie the amount of ways) times 8*/
class optgen {
    private:
        uint64_t way_count;           // number of ways in the cache, number of entries in each cache line
        uint32_t hist_length;         // length of history/occupancy per sampled set
    
        vector<uint32_t> occupancy_vector;

        // wrap circular index
        inline uint32_t wrap(uint32_t idx) const {
            return idx % hist_length;
        }
    public:
        explicit optgen(uint64_t _way_count): way_count(_way_count)
        {
            hist_length = way_count * OCCUPANCY_VECTOR_MULTIPLIER;
            assert(hist_length > 0);
    
            // Initialize data structures
            occupancy_vector.resize(hist_length, 0);
        }
    
        // Record a cache access for OPT simulation
        void cache_access(uint32_t idx) { 
            occupancy_vector[wrap(idx)] = 0; 
        }

        // Check if address would have been in cache under OPT for this set
        bool is_in_cache(uint32_t prev_idx, uint32_t curr_idx) {
            // Check if occupancy between prev and curr would exceed associativity
            uint32_t idx = wrap(prev_idx + 1);
            while (idx != curr_idx) {
                if (occupancy_vector[idx] >= way_count)
                    return false; // Would have been a miss so occupancy vector is not modified
                idx = wrap(idx + 1);
            }
    
            // Update occupancy between prev and current
            idx = wrap(prev_idx + 1);
            while (idx != curr_idx) {
                occupancy_vector[idx]++;
                idx = wrap(idx + 1);
            }
            return true;
        }
};
    
/*    Hawkeye Predictor
-- look up table to check if the cache insertion s friendly or averse*/
#define map_size 8192  // 8000 entries, each 3 bits wide

class hawkeye_predictor {
private:
    uint8_t* predictor_map;

    uint32_t hash_instruction(uint32_t PC) const {
        return PC % (map_size);
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
        return (value & 0x4) >> 2;  // check if the most significant bit (bit 2) is 1
        //cache friendly is 1. cache averse is 0
    }

    //trains the hash... true for positively train, false for negatively trained. takes in the PC
    void train_instruction(uint32_t PC, bool taken) {
        uint32_t idx = hash_instruction(PC);
        uint8_t ctr = predictor_map[idx] & 0x7;
        if (taken) { //if taken. positively train by increasing value
            if (ctr < 7) ctr++;
        } else {
            if (ctr > 0) ctr--;
        }
        predictor_map[idx] = (predictor_map[idx] & ~0x7) | ctr;
    }
};

#endif