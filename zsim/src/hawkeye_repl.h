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
        int8_t* array;
        int64_t* prevPC;    //tracks the last pc to access the respective rrip
        bool* wasReplaced;  //tracks if the id is to be replacedd

<<<<<<< Updated upstream
=======
        // new: per‐line recency tracking, for LRU portion
        uint64_t* lastAccess;
        uint64_t  accessCounter;

>>>>>>> Stashed changes
        vector<optgen> opt_sims;               //include an optgen fro each set
        hawkeye_predictor predictor;


    public:
        explicit HawkeyeReplPolicy(uint32_t _numSets, uint32_t _numWays, uint32_t _lineSize) 
<<<<<<< Updated upstream
            : numSets(_numSets), numWays(_numWays), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t>(numSets * numWays);
            wasReplaced = gm_calloc<bool>(numSets * numWays);
            for(uint32_t i = 0; i < (uint32_t)numSets * numWays; i++) {
                array[i] = CACHE_AVERSE;
                wasReplaced[i] = false;
=======
            : numSets(_numSets), numWays(_numWays), accessCounter(0), predictor() {
            //set up the rrip portion
            array = gm_calloc<int8_t>(numSets * numWays);
            wasReplaced = gm_calloc<bool>(numSets * numWays);
            lastAccess  = gm_calloc<uint64_t>(numSets * numWays);
            for(uint32_t i = 0; i < (uint32_t)numSets * numWays; i++) {
                array[i] = CACHE_AVERSE;
                wasReplaced[i] = false;
                lastAccess[i]  = 0;
>>>>>>> Stashed changes
            }


            prevPC = gm_calloc<int64_t>(numSets * numWays);
            //set up each optgen
            opt_sims.reserve(numSets);
            for (uint32_t s = 0; s < numSets; ++s)
                  opt_sims.emplace_back(numWays);

            //calculate the log2 for numSets and lineSize for set index extraction
            blockOffsetBits = static_cast<uint32_t>(std::log2(_lineSize));
            indexSetBits = static_cast<uint32_t>(std::log2(numSets));
        }

        ~HawkeyeReplPolicy() {
            gm_free(prevPC);
            gm_free(array);
            gm_free(wasReplaced);
<<<<<<< Updated upstream
            /*for(uint32_t i = 0; i < numSets; i++) {
                opt_sims[i].~optgen();
            }
            gm_free(opt_sims);*/
        }

        void update(uint32_t id, const MemReq* req) {
            assert(id < numSets * numWays);
            //need the set to know which optgen to access
            uint32_t set = calculate_set_idx(req->lineAddr);
            assert(set < numSets);
=======
            gm_free(lastAccess);
        }

        void update(uint32_t id, const MemReq* req) {
            //need the set to know which optgen to access
            uint32_t set = calculate_set_idx(req->lineAddr);
>>>>>>> Stashed changes

            //log the cache interaction, update the optgen
            int32_t response = opt_sims[set].cache_access(req);
            //do not train if this is the first time seeing this 
            if(response != -1) { 
<<<<<<< Updated upstream
                uint64_t last = opt_sims[set].find_last_pc(req->lineAddr);
                predictor.train_instruction(last, response);
=======
                uint64_t lastPC = opt_sims[set].find_last_pc(req->lineAddr);
                predictor.train_instruction(lastPC, response);
>>>>>>> Stashed changes
            }
            //predict the new cache value to be in the RRIP
            bool pred_friendly = predictor.predict_instruction(req->pc);
            
<<<<<<< Updated upstream
            if(response == 1) {
                array[id] = CACHE_FRIENDLY;
            }
            else {
                if(wasReplaced[id] && array[id] < CACHE_AVERSE) {
                    predictor.train_instruction(prevPC[id], 0); //detrain the value if cache friendly
                    // age just this one set
                    uint32_t base = set * numWays;
                    for (uint32_t w = 0; w < numWays; ++w) {
                    uint32_t idx = base + w;
                    if (idx != id && array[idx] < CACHE_AVERSE - 1)
                        array[idx]++;
                    }
                }
            }

            prevPC[id] = req->pc;
            array[id] = pred_friendly ? CACHE_FRIENDLY : CACHE_AVERSE;
            wasReplaced[id] = false;
        }
        void replaced(uint32_t id) {
            assert(id < numSets * numWays);
            wasReplaced[id] = true;
        }

        //find a victim, uses RRIP
        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            //search for a cache adverse candidate
            for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                assert(*ci < numSets * numWays);
                if(array[*ci] == (int8_t)CACHE_AVERSE) {
                    return *ci;// Evict first candidate with max RRP
                }
            }
            //if no cache adverse lines are found... look for the next max one
            for(uint32_t i = 0; i <= CACHE_AVERSE; i++) {
                //search for a max value cache friedly one
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    assert(*ci < numSets * numWays);
                    if(array[*ci] == (int8_t)(CACHE_AVERSE - 1)) {
                        return *ci;// Evict first candidate with max RRP
                    }
                }
                //no match found increment all status
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()) {
                    assert(*ci < numSets * numWays);
                    //cache adverse tag can only set by the predictor
                    if(array[*ci] < (int8_t)(CACHE_AVERSE-1))
                        array[*ci]++;
                }
            }
            panic("error no rank found\n");
            return -1;
=======
            if(wasReplaced[id]) {//if cache miss 
                if(array[id] < CACHE_AVERSE) //if the cache line to be replaced was friendly
                    predictor.train_instruction(prevPC[id], 0);//detrain the entry

                if(pred_friendly) {//friendly, age all lines
                    uint32_t base = set * numWays;
                    for (uint32_t way = 0; way < numWays; ++way) {
                        uint32_t idx = base + way;
                        if (array[idx] < CACHE_AVERSE - 1)
                            array[idx]++;
                    }
                }
            }
            array[id] = pred_friendly ? CACHE_FRIENDLY : CACHE_AVERSE; //set the RRIP value
            prevPC[id] = req->pc;
            wasReplaced[id] = false;

            lastAccess[id] = accessCounter++;
        }

        void replaced(uint32_t id) {
            wasReplaced[id] = true;
        }

        template <typename C>
        inline uint32_t rank(const MemReq* req, C cands) {
            // 1) Evict any cache‐averse immediately
            for (auto it = cands.begin(); it != cands.end(); it.inc()) {
                if (array[*it] == CACHE_AVERSE)
                    return *it;
            }

            // 2) Otherwise, pick the true LRU among the candidates
            uint32_t victim   = *cands.begin();
            uint64_t oldestTS = lastAccess[victim];
            for (auto it = cands.begin(); it != cands.end(); it.inc()) {
                uint32_t idx = *it;
                if (lastAccess[idx] < oldestTS) {
                    oldestTS = lastAccess[idx];
                    victim   = idx;
                }
            }
            return victim;
>>>>>>> Stashed changes
        }
        DECL_RANK_BINDINGS;


<<<<<<< Updated upstream
=======

>>>>>>> Stashed changes
    private:
        uint32_t calculate_set_idx(uint32_t lineAddr) {
            uint32_t setIdx = lineAddr >> blockOffsetBits;
            uint32_t mask = (1u << indexSetBits) - 1;
            setIdx = setIdx & mask; 
            return setIdx;
        }
};
            
#endif // HAWKEYE_REPL_H_