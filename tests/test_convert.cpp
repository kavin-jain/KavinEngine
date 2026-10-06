#include "test.h"
#include <cstring>
#include "../tools/chessboard_record.h"
#include "../tools/lichess_parse.h"

static Board B;

TEST(record_startpos_matches_bulletformat) {
    CHECK(B.set_fen(START_FEN));
    ChessBoardRecord r = make_record(B, 37, 1);
    CHECK(r.occ == 0xFFFF00000000FFFFull);
    const uint8_t expect[16] = {0x13, 0x42, 0x25, 0x31, 0, 0, 0, 0, 0x88, 0x88, 0x88, 0x88, 0x9B, 0xCA, 0xAD, 0xB9};
    CHECK(std::memcmp(r.pcs, expect, 16) == 0);
    CHECK_EQ(r.score, 37);
    CHECK_EQ(r.result, 1);
    CHECK_EQ(r.ksq, 4);
    CHECK_EQ(r.opp_ksq, 4);  // e8 = 60, flipped ^56 = 4
}

TEST(record_black_to_move_is_flipped) {
    CHECK(B.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));
    ChessBoardRecord r = make_record(B, 50, 2);  // white-relative +50, white won
    CHECK_EQ(r.score, -50);
    CHECK_EQ(r.result, 0);
    CHECK_EQ(r.ksq, 4);       // black king e8 -> relative e1
    CHECK(r.occ & (1ull << 36));   // white pawn e4 (28) -> relative 28 ^ 56 = 36
}

TEST(record_pieces_round_trip) {
    CHECK(B.set_fen("r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP3PPP/R2QKB1R b KQ - 3 8"));
    Bitboard got[12];
    record_pieces(make_record(B, 0, 1), got);
    for (int pc = 0; pc < 12; ++pc) {  // black to move: colours swap and ranks mirror
        Bitboard want = 0;
        for (Bitboard x = B.pieces[pc]; x;) want |= 1ull << (pop_lsb(x) ^ 56);
        CHECK(got[make_piece(~color_of(pc), type_of(pc))] == want);
    }
}

TEST(parse_lichess_line_picks_deepest_eval) {
    const std::string line =
        R"({"fen":"8/4r3/2R2pk1/6pp/3P4/6P1/5K1P/8 b - -","evals":[{"pvs":[{"cp":10,"line":"e7a7 f2e3"}],"knodes":5,"depth":30},)"
        R"({"pvs":[{"cp":-7,"line":"e7e4 h2h4"},{"cp":0,"line":"g6f5 c6c5"}],"knodes":9,"depth":57}]})";
    EvalLine e;
    CHECK(parse_eval_line(line, e));
    CHECK(e.fen == "8/4r3/2R2pk1/6pp/3P4/6P1/5K1P/8 b - -");
    CHECK_EQ(e.depth, 57);
    CHECK_EQ(e.cp, -7);
    CHECK(!e.mate);
    CHECK(e.first_move == "e7e4");
    const std::string mate = R"({"fen":"6k1/6p1/8/4K3/4NN2/8/8/8 w - -","evals":[{"pvs":[{"mate":15,"line":"e5e6 g8f8"}],"knodes":1,"depth":95}]})";
    CHECK(parse_eval_line(mate, e) && e.mate);
    CHECK(!parse_eval_line("not json", e));
}

TEST(classify_filters) {
    EvalLine e{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", 20, false, 30, "e2e4"};
    CHECK(classify(e, B) == KEEP);
    e.depth = 19;                     CHECK(classify(e, B) == TOO_SHALLOW);
    e.depth = 30; e.cp = 3500;        CHECK(classify(e, B) == TOO_BIG);
    e.cp = 20; e.mate = true;         CHECK(classify(e, B) == MATE_EVAL);
    e.mate = false;
    e.fen = "4k3/8/8/3q4/4P3/8/8/4K3 w - -"; e.first_move = "e4d5";   CHECK(classify(e, B) == NOISY);  // capture
    e.fen = "4k3/P7/8/8/8/8/8/4K3 w - -";    e.first_move = "a7a8q";  CHECK(classify(e, B) == NOISY);  // promotion
    e.fen = "4k3/8/8/8/8/8/8/R3K2R w KQ -";  e.first_move = "e1h1";   CHECK(classify(e, B) == KEEP);   // chess960 castle
    e.fen = "4k3/8/8/8/8/8/8/4K2r w - -";    e.first_move = "e1d2";   CHECK(classify(e, B) == IN_CHECK);
    e.fen = "garbage";                                                CHECK(classify(e, B) == BAD_FEN);
    e.fen = "rnbqk1nr/1pp2ppp/pbnp4/3Pp3/B3P3/2P2N2/PP3PPP/RNBQKBNR b KQkq -"; e.first_move = "a6a5";
    CHECK(classify(e, B) == BAD_FEN);  // 33 pieces (real Lichess line): would overflow the 16-byte pcs array
}
