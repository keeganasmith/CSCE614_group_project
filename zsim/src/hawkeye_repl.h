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
        uint64_t num_lines;          // total capacity in lines
        uint64_t set_count;           // number of sets in the cache
        uint32_t vector_size;         // length of history/occupancy per sampled set
        uint32_t num_ways; //associativity of the cache
        // Granularity parameter: number of accesses per time-quantum
        static constexpr uint32_t TIME_QUANTUM = 1;
        // Number of sets to sample for set dueling
        static constexpr uint32_t SAMPLE_SETS = 1024;
    
        //counter for each set to track when to advance time-quantum
        uint32_t* access_count;
        // Flags for which sets are sampled
        bool* is_sampled;
    
        // For each set, maintain a history buffer and occupancy vector
        // We allocate for all sets but only use for sampled ones
        uint64_t* history;
        uint32_t* occupancy_vector;
    
        // Wrap an index in the circular buffer
        inline int32_t wrap_index(int32_t idx) const {
            int result =  idx % vector_size;
            if(result < 0){
                result += vector_size;
            }
            return result;
        }
    
    public:
        explicit optgen(uint64_t _num_lines, uint32_t _numWays)
            : num_lines(_num_lines), num_ways(_numWays)
        {
            std::cout << "got to optgen constructor\n";
            // Compute reduced vector size using time quantization
            // Original vector_size = associativity * 8
            // With granularity, divide by TIME_QUANTUM
            //num_lines / set_count = num_ways
            vector_size = (num_ways * 8) / TIME_QUANTUM;
            std::cout << "vector size: " << vector_size << "\n";
            set_count = num_lines / num_ways;
            std::cout << "set count: " << set_count << "\n";
            // Initialize data structures
            history = new uint64_t[set_count * vector_size];
            occupancy_vector = new uint32_t[set_count * vector_size];
            access_count = new uint32_t[set_count];
            is_sampled = new bool[set_count];
            std::cout << "finished allocating data structures\n";
            for(unsigned int i = 0; i < set_count; i++){
                access_count[i] = 0;
                is_sampled[i] = false;
            }
            
            
    
            // Set Dueling: randomly select SAMPLE_SETS sets for OPT simulation
            std::mt19937 rng(0);  // fixed seed for reproducibility
            std::uniform_int_distribution<uint32_t> dist(0, set_count - 1);
            uint32_t sampled = 0;
            std::cout << "got to sample loop\n";
            while (sampled < SAMPLE_SETS) {
                uint32_t s = dist(rng);
                if (!is_sampled[s]) {
                    is_sampled[s] = true;
                    access_count[s] =0;
                    for(unsigned int i =0; i < vector_size; i++){
                        occupancy_vector[s * vector_size + i] = 0;
                        history[s * vector_size + i] = 0;
                    }
                    sampled++;
                }
            }
            std::cout << "got to end of optgen constructor\n";
            // Non-sampled sets will be skipped in simulation
        }
        ~optgen(){
            delete[] is_sampled;
            delete[] access_count;
            delete[] occupancy_vector;
            delete[] history;
        }
        bool sampled(uint32_t set){
            return is_sampled[set];
        }
        
        bool cache_access(uint64_t address, uint32_t set){ //returns true if cache hit, false otherwise
            if(!is_sampled[set]){
                return false;
            }
            access_count[set]++;
            unsigned int hist_idx = wrap_index(access_count[set] / TIME_QUANTUM);  
            unsigned int set_offset = set * vector_size;
            if(access_count[set] % TIME_QUANTUM == 0){
                occupancy_vector[set_offset + hist_idx] = 0;
                history[set_offset + hist_idx] = address;
            }
            
            unsigned int i = wrap_index(hist_idx - 1);
            while(i != hist_idx){
                if(history[set_offset + i] == address){
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
            unsigned int index = i;
            while(index != hist_idx){
                if(occupancy_vector[set_offset + index] >= lines_per_set){
                    return false; //no further action needed, cache miss
                }
                index = wrap_index(index + 1); 
            }

            //cache hit so we need to update all values in usage interval [i -> hist_idx)
            while(i != hist_idx){
                occupancy_vector[set_offset + i]++;
                i = wrap_index(i + 1);
            }
            return true;
            
        }
};
    
/*    Hawkeye Predictor
-- look up table to check if the cache insertion s friendly or averse*/
#define map_size 8192  // 8k entries, each 3 bits wide

class hawkeye_predictor {
private:
    uint8_t* predictor_map;
    uint32_t hash_instruction(uint64_t PC) const {
        // A simple hash function (can be improved depending on use case)
        return PC % map_size;
    }
    

public:
    hawkeye_predictor() {
        predictor_map = gm_calloc<uint8_t>(map_size);
    }

    ~hawkeye_predictor() {
        gm_free(predictor_map);
    }

    bool predict_instruction(uint64_t PC) {
        uint32_t index = hash_instruction(PC);
        uint8_t value = predictor_map[index];
        return value >= 4;
    }
    //trains the hash... 1 for positively train, 0 for negatively trained. takes in the PC
    //higher is better
    void train_instruction(uint64_t PC, bool taken) {
        uint32_t idx = hash_instruction(PC);
        uint8_t ctr = predictor_map[idx];
        if (taken) { 
            if (ctr < 7) ctr++;
        } else {
            if (ctr > 0) ctr--;
        }
        predictor_map[idx] = ctr;
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
        uint64_t setMask; 
        uint32_t smallest_set = INT32_MAX;
        uint32_t largest_set = 0;
        uint32_t blockOffsetBits;
        uint32_t indexSetBits;
        uint64_t* array_pcs;
        uint64_t min_address = UINT64_MAX;
        uint64_t max_address = 0;
    public:
        // add member methods here, refer to repl_policies.h
        explicit HawkeyeReplPolicy(uint32_t _numLines, uint32_t _numWays, uint32_t _lineSize) : numLines(_numLines), numWays(_numWays), Opt_Gen(_numLines, _numWays), predictor() {
            //set up the rrip portion
            std::cout << "got to constructor\n";
            array = gm_calloc<int8_t>(numLines);
            array_pcs = new uint64_t[numLines];
            for(uint32_t i = 0; i < numLines; i++) {
                array[i] = cache_averse;
                array_pcs[i] = 0;
            }
            size_t set_count = _numLines / _numWays;
            std::cout << "finished constructor\n";
            blockOffsetBits = __builtin_ctz(_lineSize);
            indexSetBits = __builtin_ctz(set_count);
            setMask = (1u << indexSetBits) - 1;
        }

        ~HawkeyeReplPolicy() {
            gm_free(array);
            delete[] array_pcs;
        }
        uint64_t get_set_index(Address lineAddr){
            // uint64_t offset = lineAddr & ((1 << blockOffsetBits) - 1);
            // uint64_t set_index = (lineAddr >> blockOffsetBits) & setMask;
            // uint64_t tag = lineAddr >> (blockOffsetBits + indexSetBits);
            
            // if(lineAddr < min_address){
            //     min_address = lineAddr;
            //     std::cout << "new address diff is: " << max_address - min_address << "\n";
            // }
            // if(lineAddr > max_address){
            //     max_address = lineAddr;
            //     std::cout << "new address diff is: " << max_address - min_address << "\n";
            // }
            return (uint64_t)((lineAddr >> blockOffsetBits) & setMask);
        }
        //recall: update is called on cache hit
        void update(uint32_t id, const MemReq* req) {
            uint32_t set_index = get_set_index(req->lineAddr);
            if(Opt_Gen.sampled(set_index)){
                uint64_t lineNumber = req->lineAddr >> blockOffsetBits;
                bool opt_hit = Opt_Gen.cache_access(lineNumber, set_index);
                predictor.train_instruction(req->pc, opt_hit);
            }
            bool prediction = predictor.predict_instruction(req->pc);
            if(prediction){
                array[id] = 0;
                array_pcs[id] = req->pc;
            }
            else{
                array[id] = cache_averse;
                array_pcs[id] = req->pc;
            }
        }

        //recall: replaced is called when id is inserted into cache after cache miss
        void replaced(uint32_t id) {
            
        }

        //find a victim, uses RRIP
        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint64_t set_index = get_set_index(req->lineAddr);
            if(Opt_Gen.sampled(set_index)){
                uint64_t lineNumber = req->lineAddr >> blockOffsetBits;
                bool opt_hit = Opt_Gen.cache_access(lineNumber, set_index);
                predictor.train_instruction(req->pc, opt_hit);
            }
            bool replace_prediction = predictor.predict_instruction(req->pc);
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
            uint32_t best_cand = 0;
            uint8_t largest = 0;
            for(auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                if(array[*ci] >= largest){
                    largest = array[*ci];
                    best_cand = *ci;
                }
            }
            if(replace_prediction){
                array[best_cand] = 0;
            }
            else{
                array[best_cand] = cache_averse;
            }
            predictor.train_instruction(array_pcs[best_cand], false); //detrain evicted cache friendly
            array_pcs[best_cand] = req->pc;
            return best_cand;
        }
        DECL_RANK_BINDINGS;
};
            
#endif // HAWKEYE_REPL_H_
