#include "test.h"
#include <string>
#include "../src/board.h"

static Board B;  // Board is ~25 KB: keep it off the stack

TEST(fen_roundtrip) {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 3",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    };
    for (const char* f : fens) {
        CHECK(B.set_fen(f));
        CHECK(B.fen() == f);
        CHECK(B.key == B.compute_key());
    }
}

TEST(fen_defaults_and_rejects) {
    CHECK(B.set_fen("8/8/8/4k3/8/8/8/4K3 w - -"));  // move counters optional
    CHECK(B.fen() == "8/8/8/4k3/8/8/8/4K3 w - - 0 1");
    CHECK(!B.set_fen("garbage"));
    CHECK(!B.set_fen("8/8/8/8/8/8/8/8 w - - 0 1"));               // no kings
    CHECK(!B.set_fen("8/8/8/4k3/8/8/8/4K3 x - - 0 1"));           // bad side
    CHECK(!B.set_fen("P7/8/8/4k3/8/8/8/4K3 w - - 0 1"));          // pawn on rank 8
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/4K2r w - - 0 1"));           // side to move in check: legal
    CHECK(!B.set_fen("4k3/8/8/8/8/8/8/4K2r b - - 0 1"));          // side not to move is in check: illegal
}

TEST(castling_rights_need_pieces_on_home_squares) {
    CHECK(B.set_fen("4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1"));  // no rooks: rights dropped
    CHECK(B.fen() == "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
}
