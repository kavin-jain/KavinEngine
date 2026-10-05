#include "test.h"
#include "../src/tt.h"

TEST(tt_store_probe_replace) {
    TT tt;
    CHECK(tt.resize(1 << 20, false));
    TTEntry e;
    CHECK(!tt.probe(0x1234, e));
    tt.store(0x1234, make_move(12, 28, DOUBLE_PUSH), 55, 6, BOUND_EXACT);
    CHECK(tt.probe(0x1234, e));
    CHECK(e.move == make_move(12, 28, DOUBLE_PUSH) && e.score == 55 && e.depth == 6 && e.bound == BOUND_EXACT);
    tt.store(0x1234, NO_MOVE, 10, 3, BOUND_LOWER);  // shallower non-exact: keep the deeper entry
    CHECK(tt.probe(0x1234, e) && e.depth == 6);
    tt.clear();
    CHECK(!tt.probe(0x1234, e));
}

TEST(tt_mate_scores_are_ply_relative) {
    CHECK_EQ(score_to_tt(MATE - 5, 3), MATE - 2);
    CHECK_EQ(score_from_tt(MATE - 2, 3), MATE - 5);
    CHECK_EQ(score_to_tt(-MATE + 5, 3), -MATE + 2);
    CHECK_EQ(score_from_tt(-MATE + 2, 3), -MATE + 5);
    CHECK_EQ(score_to_tt(120, 7), 120);
}
