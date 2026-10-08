#include "test.h"
#include <cstdlib>
#include "../src/search.h"

static Board B;

static SearchResult run(const char* fen, int depth) {
    CHECK(B.set_fen(fen));
    clear_search_state();
    Limits l;
    l.depth = depth;
    return search(B, l, false);
}

TEST(finds_mate_in_one) {
    SearchResult r = run("6k1/4Rppp/8/8/8/8/5PPP/6K1 w - - 0 1", 3);
    CHECK(move_to_uci(r.best) == "e7e8");
    CHECK_EQ(r.score, MATE - 1);
}

TEST(finds_mate_in_two) {
    // e.g. 1.Ra7 Kg8 2.Rb8#. Checks mate scoring. The depth needed depends on the net, a known search weakness (quiet
    // mates in won positions are found late: IIR at non-PV nodes with RFP; roadmap): m2 depth 3, KB10 9, Leela 11.
    SearchResult r = run("7k/8/8/8/8/8/R7/1R4K1 w - - 0 1", 12);
    CHECK_EQ(r.score, MATE - 3);
}

TEST(bare_kings_is_draw) {
    SearchResult r = run("8/8/8/8/8/8/8/K6k w - - 0 1", 4);
    CHECK_EQ(r.score, 0);
    CHECK(r.best != NO_MOVE);
}

TEST(no_legal_moves_returns_no_move) {
    SearchResult r = run("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3);  // black is stalemated
    CHECK(r.best == NO_MOVE);
}

TEST(takes_free_queen) {
    SearchResult r = run("4k3/8/8/3q4/4P3/8/8/4K3 w - - 0 1", 4);
    CHECK(move_to_uci(r.best) == "e4d5");
}

TEST(node_limit_is_respected) {
    CHECK(B.set_fen(START_FEN));
    clear_search_state();
    Limits l;
    l.nodes = 5000;
    SearchResult r = search(B, l, false);
    CHECK(r.nodes <= 5000 + 64);
    CHECK(r.best != NO_MOVE);
}

TEST(time_budget) {
    Limits l;
    l.time[WHITE] = 60000; l.inc[WHITE] = 600;
    TimeBudget t = compute_budget(l, WHITE);
    CHECK_EQ(t.soft, 3300);   // 60000/20 + 600/2
    CHECK_EQ(t.hard, 9900);   // 3 * soft, below 60000 - 50
    l.time[WHITE] = 40; l.inc[WHITE] = 0;
    t = compute_budget(l, WHITE);
    CHECK_EQ(t.hard, 1);
    CHECK_EQ(t.soft, 1);
    Limits m; m.movetime = 1000;
    t = compute_budget(m, BLACK);
    CHECK_EQ(t.soft, 950); CHECK_EQ(t.hard, 950);
    Limits d;  // depth-only search: no clock
    t = compute_budget(d, WHITE);
    CHECK_EQ(t.hard, -1);
}
