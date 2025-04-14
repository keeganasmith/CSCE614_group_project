#ifndef SHIP_REPL_H_
#define SHIP_REPL_H_

#include <functional>
#include <cstdbool>
#include <cstdint>

#include "repl_policies.h"
#include "rrip_repl.h"

// Signature-based Hit Prediction
class SHiPReplPolicy : public ReplPolicy {
    private:
        struct ReplData {
            bool is_rereferenced; 
            uint8_t RRPV;       // 0: near-immediate, 1: intermediate, 2: distant
            uint16_t signature; // 15-bit hash
        };

        enum RRPV {
            MIN = 0,
            MID = 2,
            MAX = 3
        };

        static const uint32_t SHCT_SIZE = 16384;
        static const uint8_t SHCT_SAT_COUNTER_MAX = 255;

        uint8_t *SHCT;
        ReplData *repl_data;

    public:
        explicit SHiPReplPolicy(uint32_t num_lines) {
            SHCT = gm_calloc<uint8_t>(SHCT_SIZE);
            repl_data = gm_calloc<ReplData>(num_lines);

            for (size_t i = 0; i < num_lines; i++) {
                repl_data[i].is_rereferenced = false;
                repl_data[i].RRPV = RRPV::MAX;
                repl_data[i].signature = 0;
            }
        }

        ~SHiPReplPolicy() {
            gm_free(SHCT);
            gm_free(repl_data);
        }

        void update(uint32_t id, const MemReq* req) {
            uint16_t signature = std::hash<std::string>{}(std::to_string(req->pc)) & 0x3FFF;

            // increment the SHCT entry indexed by the signature
            saturating_increment(signature, SHCT_SAT_COUNTER_MAX);

            // update the cache line's re-reference prediction
            if (repl_data[id].RRPV == RRPV::MAX) {
                repl_data[id].RRPV = RRPV::MID;
            } else {
                repl_data[id].RRPV = RRPV::MIN;
            }
            
            repl_data[id].is_rereferenced = true;
            repl_data[id].signature = signature;
        }

        void replaced(uint32_t id) {
            // update the cache line's re-reference prediction
            if (SHCT[repl_data[id].signature] == 0) {
                repl_data[id].RRPV = RRPV::MAX;
            } else {
                repl_data[id].RRPV = RRPV::MID;
            }

            repl_data[id].is_rereferenced = false;
        }

        template <typename C> inline uint32_t rank(const MemReq* req, C cands) {
            uint32_t best_cand = -1;
            // uint8_t highest_RRPV = RRPV::MIN;
            // uint8_t curr_RRPV = RRPV::MIN;
            
            // select the victim cache line with the highest RRPV
            // for (auto c = cands.begin(); c != cands.end(); c.inc()) {
            //     curr_RRPV = repl_data[*c].RRPV;

            //     if (curr_RRPV >= highest_RRPV) {
            //         highest_RRPV = curr_RRPV;
            //         best_cand = *c;
            //     }
            // }

            while (best_cand == (uint32_t)-1) {
                for (auto c = cands.begin(); c != cands.end(); c.inc()){
                    if (repl_data[*c].RRPV == RRPV::MAX) {
                        best_cand = *c;
                        break;
                    }
                }

                if (best_cand == (uint32_t)-1) {
                    for (auto c = cands.begin(); c != cands.end(); c.inc()){
                        repl_data[*c].RRPV++;
                    }
                }
            }

            // update SHCT entry of the victim cache line
            if (!repl_data[best_cand].is_rereferenced) {
                saturating_decrement(repl_data[best_cand].signature);
            }

            // obtain the signature of the incoming cache line
            repl_data[best_cand].signature = std::hash<std::string>{}(std::to_string(req->pc)) & 0x3FFF;

            return best_cand;
        }

        DECL_RANK_BINDINGS;

        void saturating_increment(size_t signature, size_t sat_counter_max) {
            if (SHCT[signature] != sat_counter_max) {
                SHCT[signature]++;
            }
        }

        void saturating_decrement(size_t signature) {
            if (SHCT[signature] != 0) {
                SHCT[signature]--;
            }
        }
};

#endif // SHIP_REPL_H_
