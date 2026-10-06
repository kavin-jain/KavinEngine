#include "test.h"
#include <string>
#include "../src/movegen.h"

static Board B;

struct PerftCase { const char* fen; int depth; uint64_t nodes; };
static const PerftCase CASES[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
};

TEST(perft_suite) {
    for (const auto& c : CASES) {
        CHECK(B.set_fen(c.fen));
        CHECK_EQ(perft(B, c.depth), c.nodes);
        CHECK(B.fen() == c.fen);  // perft must leave the board untouched
    }
}

TEST(make_unmake_restores_everything) {
    CHECK(B.set_fen(CASES[1].fen));
    const std::string start = B.fen();
    const Key key = B.key;
    MoveList l1;
    generate(B, l1, false);
    for (int i = 0; i < l1.size; ++i) {
        if (!B.make(l1.moves[i])) continue;
        CHECK(B.key == B.compute_key());
        MoveList l2;
        generate(B, l2, false);
        for (int j = 0; j < l2.size; ++j) {
            if (!B.make(l2.moves[j])) continue;
            CHECK(B.key == B.compute_key());
            B.unmake(l2.moves[j]);
        }
        B.unmake(l1.moves[i]);
        CHECK(B.fen() == start);
        CHECK(B.key == key);
    }
}

TEST(captures_only_generation) {
    CHECK(B.set_fen(CASES[1].fen));
    MoveList l;
    generate(B, l, true);
    int legal = 0;
    for (int i = 0; i < l.size; ++i) {
        CHECK(is_capture(l.moves[i]) || is_promo(l.moves[i]));
        if (B.make(l.moves[i])) { ++legal; B.unmake(l.moves[i]); }
    }
    CHECK_EQ(legal, 8);  // Kiwipete depth-1 captures per chessprogramming.org Perft Results
}

TEST(see_pawn_takes_free_pawn) {
    Board b; CHECK(b.set_fen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
    const Move m = make_move(28, 35, CAPTURE);  // exd5
    CHECK(see_ge(b, m, 100));
    CHECK(!see_ge(b, m, 101));
}
TEST(see_queen_takes_defended_pawn) {
    Board b; CHECK(b.set_fen("4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1"));
    const Move m = make_move(3, 35, CAPTURE);   // Qxd5, cxd5
    CHECK(see_ge(b, m, -800));
    CHECK(!see_ge(b, m, -799));
    CHECK(!see_ge(b, m, 0));
}
TEST(see_xray_rook_battery) {
    Board b; CHECK(b.set_fen("3r3k/3r4/8/8/8/8/3R4/3R3K w - - 0 1"));
    const Move m = make_move(11, 51, CAPTURE);  // Rxd7 Rxd7 Rxd7: the d1 rook joins through d2
    CHECK(see_ge(b, m, 500));
    CHECK(!see_ge(b, m, 501));
}
TEST(see_xray_added_after_capture) {  // fails if the x-ray update lines are missing (-200 instead of -100)
    Board b; CHECK(b.set_fen("3rk3/8/4p3/3p4/8/2N5/3R4/3R2K1 w - - 0 1"));
    const Move m = make_move(18, 35, CAPTURE);  // Nxd5
    CHECK(see_ge(b, m, -100));
    CHECK(!see_ge(b, m, -99));
}
TEST(see_special_moves_count_as_zero) {
    Board b; CHECK(b.set_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
    const Move ep = make_move(36, 43, EP_CAPTURE);
    CHECK(see_ge(b, ep, 0));
    CHECK(!see_ge(b, ep, 1));
}
