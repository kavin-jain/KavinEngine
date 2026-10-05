#include "test.h"
#include "../src/uci.h"

static Board B;

TEST(uci_position_with_moves) {
    uci_command("position startpos moves e2e4 e7e5 g1f3");
    CHECK(uci_board().fen() == "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2");
    uci_command("position fen 8/P6k/8/8/8/8/8/K7 w - - 0 1 moves a7a8q");
    CHECK(uci_board().fen() == "Q7/7k/8/8/8/8/8/K7 b - - 0 1");
    uci_command("position fen not a fen");  // invalid input falls back to startpos
    CHECK(uci_board().fen() == START_FEN);
}

TEST(bench_positions_are_valid) {
    for (int i = 0; i < BENCH_COUNT; ++i) CHECK(B.set_fen(BENCH_FENS[i]));
}
