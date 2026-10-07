// Self-play training data with game results (WDL labels), in bullet's ChessBoard format.
// Each game: 8-9 random plies from the start position, rejected if a quick search finds it unbalanced, then
// fixed-node self-play. A position is recorded when the side to move is not in check, the chosen move is quiet
// and the score is not a mate score (the usual filters: captures and checks make static labels noisy).
// Usage: datagen <out.bin> <games> <seed> [nodes=5000]   (one process per core; seeds must differ)
#include <cstdio>
#include <cstdlib>
#include <random>
#include <utility>
#include <vector>
#include "chessboard_record.h"
#include "../src/movegen.h"
#include "../src/uci.h"

namespace {
constexpr int WIN_SCORE = 2000, WIN_PLIES = 4;  // adjudicate a win after 4 plies at >= 2000 cp (white view)
constexpr int DRAW_SCORE = 10, DRAW_PLIES = 12, DRAW_MIN_PLY = 80;
constexpr int OPENING_MAX_SCORE = 1000;

std::mt19937_64 rng;
Board g_board;  // ~24 KB: static, not on the stack

bool random_opening(Board& b) {
    b.set_fen(START_FEN);
    const int plies = 8 + int(rng() & 1);
    for (int i = 0; i < plies; ++i) {
        MoveList l;
        generate(b, l, false);
        std::vector<Move> legal;
        for (int j = 0; j < l.size; ++j)
            if (b.make(l.moves[j])) { b.unmake(l.moves[j]); legal.push_back(l.moves[j]); }
        if (legal.empty()) return false;
        b.make(legal[rng() % legal.size()]);
    }
    Limits q; q.nodes = 10000;
    return std::abs(search(b, q, false).score) <= OPENING_MAX_SCORE;
}

// Plays one game from `b` and appends its records to `out`. Returns the number of records written.
size_t play_game(Board& b, uint64_t nodes, FILE* out) {
    clear_search_state();
    std::vector<std::pair<ChessBoardRecord, Color>> recs;
    int result = -1, win_run = 0, loss_run = 0, draw_run = 0;  // result: 0 Black won, 1 draw, 2 White won
    for (int ply = 0; result < 0; ++ply) {
        if (b.is_draw()) { result = 1; break; }
        Limits l; l.nodes = nodes;
        const SearchResult r = search(b, l, false);
        if (r.best == NO_MOVE) { result = b.in_check() ? (b.stm == WHITE ? 0 : 2) : 1; break; }
        const int ws = b.stm == WHITE ? r.score : -r.score;
        win_run  = ws >=  WIN_SCORE ? win_run + 1 : 0;
        loss_run = ws <= -WIN_SCORE ? loss_run + 1 : 0;
        draw_run = std::abs(ws) <= DRAW_SCORE ? draw_run + 1 : 0;
        if (win_run >= WIN_PLIES) result = 2;
        else if (loss_run >= WIN_PLIES) result = 0;
        else if (ply >= DRAW_MIN_PLY && draw_run >= DRAW_PLIES) result = 1;
        if (!b.in_check() && !is_capture(r.best) && !is_promo(r.best) && std::abs(r.score) < MATE_BOUND)
            recs.push_back({make_record(b, ws, 1), b.stm});
        b.make(r.best);
        b.trim_history();
    }
    for (auto& [rec, stm] : recs) rec.result = uint8_t(stm == WHITE ? result : 2 - result);
    for (auto& pr : recs) std::fwrite(&pr.first, sizeof pr.first, 1, out);
    return recs.size();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: datagen <out.bin> <games> <seed> [nodes=5000]\n"); return 1; }
    const long games = std::atol(argv[2]);
    rng.seed(std::strtoull(argv[3], nullptr, 10));
    const uint64_t nodes = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 5000;
    engine_init();
    FILE* out = std::fopen(argv[1], "ab");
    if (!out) { std::perror(argv[1]); return 1; }
    size_t total = 0;
    for (long g = 0; g < games; ++g) {
        while (!random_opening(g_board)) {}
        total += play_game(g_board, nodes, out);
        if ((g + 1) % 100 == 0) { std::fflush(out); std::fprintf(stderr, "games %ld positions %zu\n", g + 1, total); }
    }
    std::fclose(out);
    std::fprintf(stderr, "done: games %ld positions %zu\n", games, total);
    return 0;
}
