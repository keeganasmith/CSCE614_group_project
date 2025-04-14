#ifndef RRIP_REPL_H_
#define RRIP_REPL_H_

#include "repl_policies.h"

// Static RRIP
class SRRIPReplPolicy : public ReplPolicy {
    protected:
        // add class member variables here
        uint32_t numLines;
        uint32_t rpvmax;
        uint64_t *array;
    public:
        // add member methods here, refer to repl_policies.h
        explicit SRRIPReplPolicy(uint32_t _numLines, uint32_t _rpvmax) : numLines(_numLines), rpvmax(_rpvmax) {
            array = gm_calloc<uint64_t>(numLines);

            for(uint32_t i = 0; i < numLines; ++i){
                array[i] = rpvmax;
            }
        }

        ~SRRIPReplPolicy() {
            gm_free(array);
        }

        void update(uint32_t id, const MemReq* req) {
            if (array[id] == rpvmax + 1) {
                array[id] -= 2;
            } else {
                array[id] = 0;
            } 
        }

        void replaced(uint32_t id) {
            array[id] = rpvmax + 1;
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t bestCand = -1;

            while (bestCand == (uint32_t)-1) {
                for (auto ci = cands.begin(); ci != cands.end(); ci.inc()){
                    if (array[*ci] == rpvmax) {
                        bestCand = *ci;
                        break;
                    }
                }

                if (bestCand == (uint32_t)-1) {
                    for (auto ci = cands.begin(); ci != cands.end(); ci.inc()){
                        ++array[*ci];
                    }
                }
            }

            return bestCand;
        }

        DECL_RANK_BINDINGS;
};

#endif // RRIP_REPL_H_
