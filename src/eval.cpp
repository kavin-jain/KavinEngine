// Tapered PeSTO evaluation (M1 bootstrap).
#include "eval.h"
#include "pesto_tables.h"

namespace {
int MgTable[12][64], EgTable[12][64];
const int PhaseInc[6] = {0, 1, 1, 2, 4, 0};
}

void eval_init() {
    for (int t = PAWN; t <= KING; ++t)
        for (int sq = 0; sq < 64; ++sq) {
#ifdef MATERIAL_ONLY  // handicapped build for the SPRT harness self-check
            const int wm = 0, we = 0, bm = 0, be = 0;
#else
            const int wm = MG_PST[t][sq ^ 56], we = EG_PST[t][sq ^ 56];  // our a1 = 0, tables have a8 = 0
            const int bm = MG_PST[t][sq], be = EG_PST[t][sq];
#endif
            MgTable[t][sq] = MG_VALUE[t] + wm;
            EgTable[t][sq] = EG_VALUE[t] + we;
            MgTable[t + 6][sq] = MG_VALUE[t] + bm;
            EgTable[t + 6][sq] = EG_VALUE[t] + be;
        }
}

int evaluate(const Board& b) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    for (int pc = 0; pc < 12; ++pc)
        for (Bitboard x = b.pieces[pc]; x;) {
            int sq = pop_lsb(x);
            mg[color_of(pc)] += MgTable[pc][sq];
            eg[color_of(pc)] += EgTable[pc][sq];
            phase += PhaseInc[type_of(pc)];
        }
    const int us = b.stm, them = us ^ 1;
    const int mgp = phase > 24 ? 24 : phase;
    return ((mg[us] - mg[them]) * mgp + (eg[us] - eg[them]) * (24 - mgp)) / 24;
}
