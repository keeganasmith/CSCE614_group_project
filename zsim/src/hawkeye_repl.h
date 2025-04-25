#ifndef HAWKEYE_REPL_H_
#define HAWKEYE_REPL_H_

#include <vector>
#include <cmath>
#include <iostream>
#include "hawkeye_helper.h"
#include "repl_policies.h"

#define CACHE_AVERSE 7
#define CACHE_FRIENDLY 0

using std::vector;

// Hawkeye Replacement policy
class HawkeyeReplPolicy : public ReplPolicy {
    protected:
        //meta data to go through information
        uint32_t numSets;
        uint32_t numWays;

        uint32_t blockOffsetBits; //in powers of 2, used in binary
        uint32_t indexSetBits;

        //status of all caches
        int8_t** array;
        int64_t** prevPC;    //tracks the last pc to access the respective rrip
        bool** wasReplaced;  //tracks if the id is to be replacedd

        vector<optgen> opt_sims;               //include an optgen fro each set
        hawkeye_predictor predictor;

    public:
        explicit HawkeyeReplPolicy(uint32_t _numSets, uint32_t _numWays, uint32_t _lineSize) 
            : numSets(_numSets), numWays(_numWays), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t*>(numSets);
            wasReplaced = gm_calloc<bool*>(numSets);
            prevPC = gm_calloc<int64_t*>(numSets);

            for(uint32_t i = 0; i < (uint32_t)numSets; i++) {
                array[i] = gm_calloc<int8_t>(numWays);
                wasReplaced[i] = gm_calloc<bool>(numWays);
                prevPC[i] = gm_calloc<int64_t>(numWays);
                for(uint32_t j = 0; j < (uint32_t)numWays; j++) {
                    array[i][j] = CACHE_AVERSE;
                    wasReplaced[i][j] = false;
                }
            }


            //set up each optgen
            opt_sims.reserve(numSets);
            for (uint32_t s = 0; s < numSets; ++s)
                  opt_sims.emplace_back(numWays);

            //calculate the log2 for numSets and lineSize for set index extraction
            blockOffsetBits = static_cast<uint32_t>(std::log2(_lineSize));
            indexSetBits = static_cast<uint32_t>(std::log2(numSets));
        }

        ~HawkeyeReplPolicy() {
            for (uint32_t i = 0; i < numSets; ++i) {
                gm_free(array[i]);
                gm_free(prevPC[i]);
                gm_free(wasReplaced[i]);
            }
            gm_free(prevPC);
            gm_free(array);
            gm_free(wasReplaced);
        }

        void update(uint32_t id, const MemReq* req) {
            //need the set to know which optgen to access
            uint32_t set = calculate_set_idx(req->lineAddr);
            uint32_t way = id % numWays;

            //log the cache interaction, update the optgen
            int32_t response = opt_sims[set].cache_access(req);
            //do not train if this is the first time seeing this 
            if(response != -1) { 
                uint64_t lastPC = opt_sims[set].find_last_pc(req->lineAddr);
                predictor.train_instruction(lastPC, response);
            }
            //predict the new cache value to be in the RRIP
            bool pred_friendly = predictor.predict_instruction(req->pc);
            
            if(wasReplaced[set][way]) {//if cache miss 
                if(array[set][way] < CACHE_AVERSE) //if the cache line to be replaced was friendly
                    predictor.train_instruction(prevPC[set][way], 0);//detrain the entry

                if(pred_friendly) {//friendly, age all lines
                    for (uint32_t w = 0; w < numWays; w++) {
                        if (array[set][w] < CACHE_AVERSE - 1)
                            array[set][w]++;
                    }
                }
            }
            array[set][way] = pred_friendly ? CACHE_FRIENDLY : CACHE_AVERSE;
            prevPC[set][way] = req->pc;
            wasReplaced[set][way] = false;
        }

        void replaced(uint32_t id) {
            uint32_t set = id / numWays;
            uint32_t way = id % numWays;
            wasReplaced[set][way] = true;
        }

        //find a victim, uses RRIP
        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t set = calculate_set_idx(req->lineAddr);
            for (int level = CACHE_AVERSE; level >= 0; --level) {
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    uint32_t way = *ci % numWays;
                    if (array[set][way] == (int8_t)level) {
                        if (level < CACHE_AVERSE)
                            predictor.train_instruction(prevPC[set][way], 0);
                        return *ci;
                    }
                }
            }
            panic("error no rank found\n");
            return -1;
        }
        DECL_RANK_BINDINGS;


    private:
        uint32_t calculate_set_idx(uint32_t lineAddr) {
            uint32_t setIdx = lineAddr >> blockOffsetBits;
            uint32_t mask = (1u << indexSetBits) - 1;
            setIdx = setIdx & mask; 
            /*if(current_set != setIdx) {
                //std::cout << "current set: " << current_set << "accessed " << set_access << "times\n";
                //current_set = setIdx;
                set_access=1;
            }
            else {
                set_access++;
            }*/
            return setIdx;
        }
};
            
#endif // HAWKEYE_REPL_H_