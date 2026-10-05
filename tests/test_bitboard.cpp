#include "test.h"
#include "../src/bitboard.h"

TEST(leaper_attacks) {
    CHECK_EQ(popcount(KnightAttacks[0]), 2);    // a1: b3, c2
    CHECK_EQ(popcount(KnightAttacks[27]), 8);   // d4
    CHECK_EQ(popcount(KingAttacks[0]), 3);
    CHECK_EQ(popcount(KingAttacks[27]), 8);
    CHECK(PawnAttacks[0][12] == (bb(19) | bb(21)));  // white e2 attacks d3, f3
    CHECK(PawnAttacks[1][52] == (bb(43) | bb(45)));  // black e7 attacks d6, f6
    CHECK(PawnAttacks[0][8] == bb(17));              // a2 attacks only b3
}

TEST(magics_match_slow_rays) {
    uint64_t s = 12345;
    for (int i = 0; i < 20000; ++i) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        Bitboard occ = s & (s >> 3);  // sparse-ish random occupancy
        int sq = i & 63;
        CHECK(rook_attacks(sq, occ) == slider_attacks_slow(sq, occ, true));
        CHECK(bishop_attacks(sq, occ) == slider_attacks_slow(sq, occ, false));
    }
    CHECK_EQ(popcount(rook_attacks(0, 0)), 14);
    CHECK_EQ(popcount(bishop_attacks(27, 0)), 13);
}
