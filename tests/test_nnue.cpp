#include "test.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "../src/movegen.h"
#include "../src/nnue.h"

static Board B;
static Accumulator A, R, Stack[512];

TEST(nnue_loaded) { CHECK(nnue_ready()); }

TEST(nnue_rejects_out_of_range_weights) {  // the int16/int32 arithmetic is only exact for |W1| <= 127
    std::vector<unsigned char> bad(NNUE_DATA, NNUE_DATA + NNUE_DATA_SIZE);
    int16_t w = 128;
    std::memcpy(bad.data() + (NNUE_INPUTS * NNUE_HIDDEN + NNUE_HIDDEN) * sizeof(int16_t), &w, sizeof w);  // W1[0]
    CHECK(!nnue_load(bad.data(), bad.size()));
    CHECK(nnue_load(NNUE_DATA, NNUE_DATA_SIZE));
}

TEST(incremental_matches_refresh) {  // random playouts hit captures, castling, promotions, en passant
    uint64_t s = 99;
    for (int game = 0; game < 200; ++game) {
        CHECK(B.set_fen(game % 2 ? START_FEN : "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"));
        nnue_refresh(B, Stack[0]);
        int d = 0;  // plies actually played; Stack[d] matches the current position
        for (int tries = 0; tries < 400 && d < 120; ++tries) {
            MoveList l;
            generate(B, l, false);
            if (l.size == 0) break;
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            const Move m = l.moves[s % l.size];
            if (!B.make(m)) continue;  // illegal pseudo-move: board unchanged, try another
            nnue_update(Stack[d], Stack[d + 1], B);
            ++d;
            nnue_refresh(B, R);
            CHECK(std::memcmp(&Stack[d], &R, sizeof R) == 0);
            if (B.is_draw()) break;
        }
    }
}

TEST(nnue_matches_trainer_float_evals) {
#ifndef EVALS_TXT
#define EVALS_TXT "nets/m2-256.evals.txt"
#endif
    std::ifstream f(EVALS_TXT);  // the trainer's float evals for the net that was built in
    CHECK(f.good());
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        const size_t bar = line.find('|');
        const std::string fen = line.substr(0, bar);
        const double expect = std::stod(line.substr(bar + 1));
        CHECK(B.set_fen(fen));
        nnue_refresh(B, A);
        const int got = nnue_evaluate(A, B.stm);
        const double tol = std::max(10.0, 0.03 * std::fabs(expect));  // quantisation error budget
        if (std::fabs(got - expect) > tol) std::printf("  eval %s: engine %d vs trainer %.2f\n", fen.c_str(), got, expect);
        CHECK(std::fabs(got - expect) <= tol);
        ++n;
    }
    CHECK_EQ(n, 6);
}
