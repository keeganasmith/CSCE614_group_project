#ifndef HAWKEYE_HELPER_H_
#define HAWKEYE_HELPER_H_

//#include <vector>
#include "repl_policies.h"

//using std::vector; 
#define OCCUPANCY_VECTOR_MULTIPLIER 8
/*Optgen calculates the belady's algorithm using livness intervals with the occupancy vector
the size of the vector is the size of a set(ie the amount of ways) times 8*/
class optgen {
    private:
        uint64_t way_count;                // number of ways in the cache, number of entries in each cache line
        uint32_t occupancy_length;         // length of history/occupancy per sampled set

        uint32_t* occupancy;                //stores liveness of all values
        uint32_t* history_addr;             //stores the history of each address
        uint32_t* history_pc;               //stores the last pc for each index

        uint64_t idx;                       //index to the current access point of the array            

        // wrap circular index
        inline uint32_t wrap(uint32_t i) const {
            return i % occupancy_length;
        }
    public:
        explicit optgen(uint64_t _way_count): way_count(_way_count), idx(0)
        {
            occupancy_length = way_count * OCCUPANCY_VECTOR_MULTIPLIER;
            assert(occupancy_length > 0);
            
            // Initialize data structures
            history_addr = gm_calloc<uint32_t>(occupancy_length);
            history_pc = gm_calloc<uint32_t>(occupancy_length);
            occupancy = gm_calloc<uint32_t>(occupancy_length);
        }
    
        ~optgen() {
            gm_free(occupancy);
            gm_free(history_addr);
            gm_free(history_pc);
        }
        // Record a cache access for OPT simulation, return the prediction for beladys
        //output: 1 -- hit, 0 -- miss, -1 -- not in the cache yet
        int32_t cache_access(const MemReq* req) {
            int32_t return_val = -1; 
            //log the entry
            assert(idx < occupancy_length);
            //0 if assuming misses will bypass cache. 1 if no bypassing
            occupancy[idx] = 0;
            history_addr[idx] = req->lineAddr;
            history_pc[idx] = req->pc;

            //see if it is in the cache 
            int32_t prev_addr_idx = last_used_addr_idx(req->lineAddr);
            if (prev_addr_idx != -1) {//not the first time in the cache
                bool was_hit= is_in_cache(prev_addr_idx);
                return_val = was_hit ? 1 : 0;
            }
            idx = wrap(idx+1);
            return return_val;
        }

        // Check if address would have been in cache under OPT for this set
        bool is_in_cache(uint32_t prev_idx) {
            // Check if occupancy between prev and curr would exceed associativity
            uint32_t i = wrap(prev_idx);
            while (i != idx) {
                assert(i < occupancy_length);
                if (occupancy[i] >= way_count)
                    return false; // Would have been a miss so occupancy vector is not modified
                i = wrap(i + 1);
            }
    
            // Update occupancy between prev and current
            i = wrap(prev_idx);
            while (i != idx) {
                assert(i < occupancy_length);
                occupancy[i]++;
                i = wrap(i + 1);
            }
            return true;
        }

        //looks for the last use of the address in the history of that set
        int32_t last_used_addr_idx(uint32_t address) { 
            // Scan history buffer for the last access of the address
            for (uint32_t i = 1; i < occupancy_length; ++i) {
                uint32_t it = wrap(idx - i);
                assert(it < occupancy_length);
                if (history_addr[it] == address) { 
                        return it;
                }
            }
            return -1;
        }

        uint64_t find_last_pc(uint32_t address) {
            // Scan history buffer for the last access of the address
            for (uint32_t i = 1; i < occupancy_length; ++i) {
                uint32_t it = wrap(idx - i);
                assert(it < occupancy_length);
                if (history_addr[it] == address) { 
                        return history_pc[it];
                }
            }
            return -1;
        }
};
    
/*    Hawkeye Predictor
-- look up table to check if the cache insertion s friendly or averse
higher values are more friendly*/
#define MAP_SIZE 16384  // 8000 entries, each 3 bits wide

class hawkeye_predictor {
private:
    uint8_t* predictor_map;

    // Improved hash: XOR-fold into MAP_SIZE bits
    uint32_t hash_instruction(uint32_t PC) const {
        uint32_t x = PC;
        x ^= x >> 16;
        x ^= x >> 8;
        return x & (MAP_SIZE - 1);
    }

public:
    hawkeye_predictor() {
        predictor_map = gm_calloc<uint8_t>(MAP_SIZE);
    }

    ~hawkeye_predictor() {
        gm_free(predictor_map);
    }
    bool predict_instruction(uint32_t PC) {
        uint32_t index = hash_instruction(PC);
        assert(index < (uint32_t)MAP_SIZE);
        uint8_t value = predictor_map[index] & 0x7;  // extract 3-bit value
        return (value >> 2) & 0x1;  // check if the most significant bit (bit 2) is 1
        //cache friendly is 1. cache averse is 0
    }

    //trains the hash... true for positively train, false for negatively trained. takes in the PC
    //trains on the PC that last accessed X
    void train_instruction(uint32_t PC, int32_t taken) {
        uint32_t idx = hash_instruction(PC);
        assert(idx < (uint32_t)MAP_SIZE);

        if (taken == 1) { //if taken. positively train by increasing value
            if (predictor_map[idx] < 7) predictor_map[idx]++; //max value is 7
        } else {
            if (predictor_map[idx] > 0) predictor_map[idx]--; //minimum value is 0
        }
    }
};

#endif