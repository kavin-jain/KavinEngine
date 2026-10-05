#include "test.h"
#include "../src/movegen.h"

static Board B;

static Move find(const char* uci) {
    MoveList l;
    generate(B, l, false);
    for (int i = 0; i < l.size; ++i) if (move_to_uci(l.moves[i]) == uci) return l.moves[i];
    return NO_MOVE;
}

TEST(repetition) {
    CHECK(B.set_fen(START_FEN));
    const char* seq[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int i = 0; i < 4; ++i) {
        CHECK(!B.is_repetition());
        Move m = find(seq[i]);
        CHECK(m != NO_MOVE && B.make(m));
    }
    CHECK(B.is_repetition());
    CHECK(B.is_draw());
}

TEST(fifty_move_and_material) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 100 80"));
    CHECK(B.is_draw());
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1"));
    CHECK(B.is_draw());   // K+N vs K
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
    CHECK(!B.is_draw());  // K+R vs K is a win
    CHECK(B.has_non_pawn_material(WHITE));
    CHECK(!B.has_non_pawn_material(BLACK));
}

TEST(null_move_roundtrip) {
    CHECK(B.set_fen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3"));
    const std::string before = B.fen();
    const Key key = B.key;
    B.make_null();
    CHECK(B.stm == BLACK && B.ep == NO_SQ);
    CHECK(B.key == B.compute_key());
    B.unmake_null();
    CHECK(B.fen() == before);
    CHECK(B.key == key);
}
