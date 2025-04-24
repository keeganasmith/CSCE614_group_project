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
        uint32_t num_ways; //associativity of the cache
        // Granularity parameter: number of accesses per time-quantum
        static constexpr uint32_t TIME_QUANTUM = 4;
        // Number of sets to sample for set dueling
        static constexpr uint32_t SAMPLE_SETS = 64;
    
        // global counter to track when to advance time-quantum
        uint32_t access_count;
        // Flags for which sets are sampled
        vector<bool> is_sampled;
    
        // For each set, maintain a history buffer and occupancy vector
        // We allocate for all sets but only use for sampled ones
        vector<vector<uint32_t>> history;
        vector<vector<uint32_t>> occupancy_vector;
    
        // Wrap an index in the circular buffer
        inline int32_t wrap_index(int32_t idx) const {
            int result =  idx % vector_size;
            if(result < 0){
                result += vector_size;
            }
            return result;
        }
    
    public:
        explicit optgen(uint64_t _cache_size, uint64_t _set_count, uint32_t _numWays)
            : cache_size(_cache_size), set_count(_set_count), hist_idx(0), num_ways(_numWays)
        {
            // Compute reduced vector size using time quantization
            // Original vector_size = associativity * 8
            // With granularity, divide by TIME_QUANTUM
            vector_size = (cache_size / set_count * 8) / TIME_QUANTUM;
    
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
        bool sampled(uint32_t set){
            return is_sampled[set];
        }
        
        bool cache_access(uint32_t address, uint32_t set){ //returns true if cache hit, false otherwise
            if(!is_sampled[set]){
                return false;
            }
            access_count++;
            if(access_count % TIME_QUANTUM == 0){
                hist_idx = wrap_index(hist_idx+1);
                occupancy_vector[set][hist_idx] = 0;
                history[set][hist_idx] = address;
            }
            
            int i = wrap_index(hist_idx - 1);
            int last_accessed_index = -1;
            while(i != hist_idx){
                if(history[set][i] == address){
                    break;
                }
                i = wrap_index(i - 1);
            }
            if(i == hist_idx){ //means first access, so don't modify occupancy
                return false;
            }
            /*If X is not a first-time load, OPTgen checks to see if
            every element corresponding to the usage interval is
            less than the cache capacity: If so, then OPT would
            have placed X in the cache, so the shaded portions of
            the occupancy vector are incremented; if not, then X
            would have been a cache miss, so the occupancy vector
            is not modified.*/

            size_t lines_per_set = num_ways;
            
            //so first we check if any elements have #overlapping liveness intervals >= capacity -> cache miss
            //recall occupancy[set][idx] = #overlapping liveness intervals at idx
            //in theory occupancy_vector[set][idx] should never exceed lines per set, but we check just in case.
            int index = i;
            while(index != hist_idx){
                if(occupancy_vector[set][index] >= lines_per_set){
                    return false; //no further action needed, cache miss
                }
                index = wrap_index(index + 1); 
            }

            //cache hit so we need to update all values in usage interval [i -> hist_idx)
            while(i != hist_idx){
                occupancy_vector[set][i]++;
                i = wrap_index(i + 1);
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
        return (value & 0x4) != 0;  // check if the most significant bit (bit 2) is 1, 1 is good
    }
    //trains the hash... 1 for positively train, 0 for negatively trained. takes in the PC
    //higher is better
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
        uint64_t SET_MASK = (1ull<<SET_BITS) - 1;
        bool replace_prediction = false;
    public:
        // add member methods here, refer to repl_policies.h
        explicit HawkeyeReplPolicy(uint32_t _numLines, uint32_t _numWays) : numLines(_numLines), numWays(_numWays), Opt_Gen(_numLines, _numWays), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t>(numLines);
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] = cache_averse;
            }

            size_t set_count = _numLines / _numWays;
            unsigned SET_BITS = __builtin_ctz(SET_COUNT);
            SET_MASK = set_count - 1;
            
        }

        ~HawkeyeReplPolicy() {
            gm_free(array);
        }
        uint32_t get_set_index(Address lineAddr){
            return uint32_t(lineAddr & SET_MASK);
        }

        //recall: update is called on cache hit
        void update(uint32_t id, const MemReq* req) {
            uint32_t set_index = get_set_index(req->lineAddr)
            if(Opt_Gen.sampled(set_index)){
                bool opt_hit = Opt_Gen.cache_access(req->lineAddr, set_index);
                predictor.train_instruction(req->pc, opt_hit);
            }
            bool prediction = predictor.predict_instruction(req->pc);
            if(prediction){
                array[id] = 0;
            }
            else{
                array[id] = cache_averse;
            }
        }

        //recall: replaced is called when id is inserted into cache after cache miss
        void replaced(uint32_t id) {
            if(replace_prediction){
                array[id] = 0;
            }
            else{
                array[id] = cache_averse;
            }
        }

        //find a victim, uses RRIP
        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t set_index = get_set_index(req->lineAddr)
            if(Opt_Gen.sampled(set_index)){
                bool opt_hit = Opt_Gen.cache_access(req->lineAddr, set_index);
                predictor.train_instruction(req->pc, opt_hit);
            }
            replace_prediction = predictor.predict_instruction(req->pc);
            if(!replace_prediction){ //means we need to age all lines
                for(auto ci = cands.begin(); ci != cands.end(); ci.inc()){
                    if(array[*ci] < cache_averse - 1){
                        array[*ci]++;
                    }
                }
            }
            //search for a cache adverse candidate
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                if(array[*ci] == (int8_t)cache_averse) {
                    return *ci;// Evict first candidate with max RRP
                }
            }
            //if no cache adverse lines are found... look for the next max one
            uint32_t best_cand = -1;
            uint8_t largest = 0;
            for(auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                if(array[*ci] > largest){
                    largest = array[*ci];
                    best_cand = *ci;
                }
            }
            return best_cand;
        }
        DECL_RANK_BINDINGS;
};
            
#endif // HAWKEYE_REPL_H_
