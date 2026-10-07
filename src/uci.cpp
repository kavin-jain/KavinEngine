#include "uci.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include "eval.h"
#include "movegen.h"
#include "nnue.h"
#include "platform.h"

// 32 positions taken in file order from the Lichess CC0 eval database (2026-10-05), 8 per piece-count band
// (>=26, 18-25, 10-17, <=9 pieces). Fixed forever: the bench node count is the functional-change signature.
const char* const BENCH_FENS[] = {
    "r1b2rk1/1p2bppp/p1nppn2/q7/2P1P3/N1N5/PP2BPPP/R1BQ1RK1 w - - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r2qk2r/3n2p1/1pp1p3/3pPpb1/P2P1nBp/1NB4P/1PP2P2/R3QR1K w kq f6 0 1",
    "1r2kb1r/pBp2ppp/4pn2/5b2/Q1pq4/6P1/PP1NPP1P/R1B2RK1 b k - 0 1",
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkbnr/pp2pppp/2p5/3P4/3P4/8/PP2PPPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
    "rnbqkb1r/pp2pp1p/5np1/2pp4/3P4/1P1BPN2/P1P2PPP/RNBQK2R b KQkq - 0 1",
    "1R4k1/3q1pp1/6n1/b2p2Pp/2pP2b1/p1P5/P1BQrPPB/5NK1 b - - 0 1",
    "1k1r1r2/pbp3pp/1p1q1p2/2p2Q2/4P3/1P1PB3/P1P3PP/4RRK1 w - - 0 1",
    "1R6/3q1ppk/6n1/b2p2Pp/2pP2b1/p1P5/P1B1rPPB/2Q2NK1 b - - 0 1",
    "r2k2r1/pppb1p1p/2p5/8/3Bn3/8/PPP2PPP/2KR1B1R b - - 0 1",
    "r1b2rk1/pp3ppp/1q2p3/2npP1N1/8/8/PPQ2PPP/R3RBK1 b - - 0 1",
    "8/p5p1/3kp1p1/PPp1np2/2P1p3/4P2P/3KBPP1/8 w - - 0 1",
    "8/p4pp1/3kp1p1/PPp1n3/2P1p3/4P2P/4BPP1/4K3 w - - 0 1",
    "8/p4pp1/3kp1p1/PPp1n3/2P1p3/4P2P/3KBPP1/8 b - - 0 1",
    "7r/1p3k2/p1bPR3/5p2/2B2P1p/8/PP4P1/3K4 b - - 0 1",
    "8/4r3/2R2pk1/6pp/3P4/6P1/5K1P/8 b - - 0 1",
    "8/1r6/2R2pk1/6pp/3P4/6P1/5K1P/8 w - - 0 1",
    "8/3B4/8/p4p1k/5P1p/Pb6/1P4P1/6K1 w - - 0 1",
    "3r4/1p3k2/p1bPR3/5p2/2B2P1p/8/PP4P1/3K4 w - - 0 1",
    "3r4/6k1/2bPR3/pp3p2/2B2P1p/P7/1P3KP1/8 w - - 0 1",
    "2r1r1k1/5ppp/8/8/Q7/8/5PPP/4R1K1 w - - 0 1",
    "2r1r1k1/3Q1p2/7R/4N1p1/P4n2/1q5P/5PP1/4R1K1 w - - 0 1",
    "6k1/6p1/8/4K3/4NN2/8/8/8 w - - 0 1",
    "6k1/4Rppp/8/8/8/8/5PPP/6K1 w - - 0 1",
    "6k1/6p1/6N1/4K3/4N3/8/8/8 b - - 0 1",
    "8/8/2N2k2/8/1p2p3/p7/K7/8 b - - 0 1",
    "8/5kp1/6N1/4K3/4N3/8/8/8 w - - 0 1",
    "8/4k3/8/4K3/8/4P3/8/8 b - - 0 1",
    "k1K5/8/8/1P6/8/8/8/8 b - - 0 1",
    "8/8/2b5/5B1k/1P3P1p/7K/6P1/8 w - - 0 1",
};
const int BENCH_COUNT = int(sizeof(BENCH_FENS) / sizeof(BENCH_FENS[0]));

namespace {

Board g_board;   // static: Board is ~25 KB
Board g_scratch;
Limits g_limits;

Move parse_move(Board& b, const std::string& s) {
    MoveList list;
    generate(b, list, false);
    for (int i = 0; i < list.size; ++i) if (move_to_uci(list.moves[i]) == s) return list.moves[i];
    return NO_MOVE;
}

void stop_search() { g_stop = true; join_worker(); g_stop = false; }  // reset: nothing is running now

void search_worker(void*) {
    SearchResult r = search(g_board, g_limits, true);
    write_line("bestmove " + move_to_uci(r.best));
}

int g_bench_depth = BENCH_DEPTH;
void bench_worker(void*) { run_bench(g_bench_depth); }  // worker thread: the ESP32 loop task has only ~8 KB of stack

void set_position(std::istringstream& ss) {
    std::string token, fen;
    ss >> token;
    if (token == "startpos") { fen = START_FEN; ss >> token; }
    else if (token == "fen") { while (ss >> token && token != "moves") fen += token + " "; }
    if (fen.empty() || !g_board.set_fen(fen)) {
        write_line("info string invalid position, using startpos");
        g_board.set_fen(START_FEN);
        return;
    }
    while (ss >> token) {
        Move m = parse_move(g_board, token);
        if (m == NO_MOVE || !g_board.make(m)) { write_line("info string illegal move " + token); return; }
        if (g_board.game_ply >= MAX_GAME_PLY - MAX_PLY - 8) g_board.trim_history();
    }
}

void perft_divide(int depth) {
    if (depth < 1) return;
    const int64_t t0 = now_ms();
    MoveList list;
    generate(g_board, list, false);
    uint64_t total = 0;
    for (int i = 0; i < list.size; ++i) {
        if (!g_board.make(list.moves[i])) continue;
        uint64_t n = perft(g_board, depth - 1);
        g_board.unmake(list.moves[i]);
        total += n;
        write_line(move_to_uci(list.moves[i]) + ": " + std::to_string(n));
    }
    const int64_t ms = std::max<int64_t>(1, now_ms() - t0);
    write_line("info string perft depth " + std::to_string(depth) + " nodes " + std::to_string(total) +
               " time " + std::to_string(ms) + " nps " + std::to_string(total * 1000 / uint64_t(ms)));
}

void start_go(std::istringstream& ss) {
    Limits l;
    std::string t;
    while (ss >> t) {
        int64_t v = 0;
        if (t == "infinite") { l.infinite = true; continue; }
        if (!(ss >> v)) break;
        if (t == "wtime") l.time[WHITE] = std::max<int64_t>(0, v);
        else if (t == "btime") l.time[BLACK] = std::max<int64_t>(0, v);
        else if (t == "winc") l.inc[WHITE] = std::max<int64_t>(0, v);
        else if (t == "binc") l.inc[BLACK] = std::max<int64_t>(0, v);
        else if (t == "movestogo") l.movestogo = int(std::max<int64_t>(0, v));
        else if (t == "depth") l.depth = int(std::clamp<int64_t>(v, 1, MAX_PLY - 2));
        else if (t == "nodes") l.nodes = uint64_t(std::max<int64_t>(1, v));
        else if (t == "movetime") l.movetime = std::max<int64_t>(1, v);
        else if (t == "perft") { perft_divide(int(v)); return; }
    }
    g_limits = l;
    g_stop = false;
    start_worker(search_worker, nullptr);
}

void set_option(std::istringstream& ss) {
    std::string t, name, value;
    ss >> t;  // "name"
    while (ss >> t && t != "value") name += (name.empty() ? "" : " ") + t;
    ss >> value;
    if (name == "Hash") {
        if (TT_FAST) { write_line("info string Hash is fixed on this device"); return; }  // SRAM table, not MB-sized
        long mb = std::clamp(std::strtol(value.c_str(), nullptr, 10), 1L, 4096L);
        if (!g_tt.resize(size_t(mb) << 20, TT_FAST)) {
            write_line("info string hash allocation failed, using default size");
            g_tt.resize(TT_DEFAULT_BYTES, TT_FAST);
        }
    } else if (name == "Move Overhead") {
        g_move_overhead = int(std::clamp(std::strtol(value.c_str(), nullptr, 10), 0L, 5000L));
    }
}

}  // namespace

void engine_init() {
    static bool done = false;
    if (done) return;
    done = true;
    init_bitboards();
    Board::init();
    eval_init();
    search_init();
#ifndef USE_PESTO
    if (!nnue_load(NNUE_DATA, NNUE_DATA_SIZE)) {  // a misbuilt engine must not play: fail loudly
        write_line("info string FATAL: NNUE size " + std::to_string(NNUE_DATA_SIZE) + " expected " +
                   std::to_string(nnue_expected_size()) + " (EVALFILE and NNUE_HIDDEN disagree), or weights outside the int16 range");
        std::abort();
    }
#endif
    if (!g_tt.resize(TT_DEFAULT_BYTES, TT_FAST)) write_line("info string TT allocation failed");
    g_board.set_fen(START_FEN);
}

const Board& uci_board() { return g_board; }

bool uci_command(const std::string& raw) {
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "uci") {
        write_line("id name " ENGINE_NAME);
        write_line("id author Kavin Jain");
        write_line("option name Hash type spin default " + std::to_string(std::max(1u, unsigned(TT_DEFAULT_BYTES >> 20))) + " min 1 max 4096");
        write_line("option name Move Overhead type spin default 50 min 0 max 5000");
        write_line("uciok");
    } else if (cmd == "isready") write_line("readyok");
    else if (cmd == "ucinewgame") { stop_search(); clear_search_state(); }
    else if (cmd == "position") { stop_search(); set_position(ss); }
    else if (cmd == "go") { stop_search(); start_go(ss); }
    else if (cmd == "stop") stop_search();
    else if (cmd == "setoption") { stop_search(); set_option(ss); }
    else if (cmd == "bench") { stop_search(); int d = 0; ss >> d; g_bench_depth = d > 0 ? d : BENCH_DEPTH; start_worker(bench_worker, nullptr); }
    else if (cmd == "d") write_line(g_board.fen());
    else if (cmd == "quit") { stop_search(); return false; }
    else if (!cmd.empty()) write_line("info string unknown command: " + cmd);
    return true;
}

void uci_loop() {
    std::string line;
    while (read_line(line) && uci_command(line)) {}
    stop_search();
}

uint64_t run_bench(int depth) {
    uint64_t total = 0;
    const int64_t t0 = now_ms();
    for (int i = 0; i < BENCH_COUNT; ++i) {
        g_scratch.set_fen(BENCH_FENS[i]);
        clear_search_state();
        Limits l;
        l.depth = depth;
        total += search(g_scratch, l, false).nodes;
    }
    const int64_t ms = std::max<int64_t>(1, now_ms() - t0);
    write_line(std::to_string(total) + " nodes " + std::to_string(total * 1000 / uint64_t(ms)) + " nps");
    return total;
}

int uci_main(int argc, char** argv) {
    engine_init();
    if (argc > 1 && std::string(argv[1]) == "bench") {
        run_bench(argc > 2 ? std::atoi(argv[2]) : BENCH_DEPTH);
        return 0;
    }
    uci_loop();
    return 0;
}
