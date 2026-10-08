#include "search.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include "eval.h"
#include "movegen.h"
#include "nnue.h"
#include "platform.h"

TT g_tt;
#ifndef CORRHIST_SIZE
#define CORRHIST_SIZE 16384  // entries per side; ESP32 builds use a smaller table
#endif
static_assert((CORRHIST_SIZE & (CORRHIST_SIZE - 1)) == 0, "CORRHIST_SIZE must be a power of two");
std::atomic<bool> g_stop{false};
int g_move_overhead = 50;
int g_clock_reserve = 0;

namespace {

struct Searcher {
    Board board;
    Limits limits;
    TimeBudget budget;
    int64_t start;
    uint64_t nodes;
    bool stopped;
    int seldepth;
    Move killers[MAX_PLY][2];
    int16_t history[2][64][64];
    Move pv[MAX_PLY][MAX_PLY];
    uint64_t root_nodes[64][64];  // nodes spent below each root move this search (time management)
    int pv_len[MAX_PLY];
    uint8_t lmr[64][64];
    Accumulator acc[MAX_PLY + 1];  // acc[ply] matches the board at that ply
    int eval_stack[MAX_PLY + 1];   // static eval per ply (-INF when in check)
    int16_t corr[2][CORRHIST_SIZE];  // pawn-structure correction history, scaled by 256
};
Searcher S;  // static storage: these arrays must not live on the small ESP32 task stack

int eval_at(int ply) {
    const int e = nnue_ready() ? nnue_evaluate(S.acc[ply], S.board.stm, popcount(S.board.occ)) : evaluate(S.board);
    return std::clamp(e, -MATE_BOUND + 1, MATE_BOUND - 1);
}

// Static-evaluation correction history (chessprogramming.org "Static Evaluation Correction History"):
// learns how far the eval misjudges positions with this pawn structure, from search results.
unsigned pawn_index(const Board& b) {
    const uint64_t k = b.pieces[make_piece(WHITE, PAWN)] * 0x9E3779B97F4A7C15ull ^ b.pieces[make_piece(BLACK, PAWN)] * 0xC2B2AE3D27D4EB4Full;
    return unsigned(k >> 40) & (CORRHIST_SIZE - 1);
}

int corrected(int raw) {
    return std::clamp(raw + S.corr[S.board.stm][pawn_index(S.board)] / 256, -MATE_BOUND + 1, MATE_BOUND - 1);
}

bool should_stop() {
    if (S.stopped) return true;
    if (S.limits.nodes && S.nodes >= S.limits.nodes) return S.stopped = true;
    if ((S.nodes & 1023) == 0 &&
        (g_stop.load(std::memory_order_relaxed) || (S.budget.hard >= 0 && now_ms() - S.start >= S.budget.hard)))
        S.stopped = true;
    return S.stopped;
}

// Captures and promotions are scored as if SEE >= 0 and marked pending: pick() runs SEE only when such a move would
// be chosen, and demotes it to the losing-capture band if it fails. Same order as scoring SEE up front, fewer SEE calls.
void score_moves(const MoveList& list, int16_t* scores, bool* see_pending, Move tt_move, int ply) {
    for (int i = 0; i < list.size; ++i) {
        const Move m = list.moves[i];
        see_pending[i] = false;
        if (m == tt_move) scores[i] = 30000;
        else if (is_capture(m) || is_promo(m)) {  // MVV-LVA
            int victim = flags_of(m) == EP_CAPTURE ? PAWN : is_capture(m) ? int(type_of(S.board.mailbox[to_sq(m)])) : 0;
            int attacker = type_of(S.board.mailbox[from_sq(m)]);
            int promo = is_promo(m) && promo_type(m) == QUEEN ? 64 : 0;
            scores[i] = int16_t(20000 + victim * 8 - attacker + promo);  // losing captures (SEE < 0) go after quiets: pick()
            see_pending[i] = true;
        } else if (m == S.killers[ply][0]) scores[i] = 19000;
        else if (m == S.killers[ply][1]) scores[i] = 18999;
        else scores[i] = S.history[S.board.stm][from_sq(m)][to_sq(m)];  // bounded to +-16384
    }
}

Move pick(MoveList& list, int16_t* scores, bool* see_pending, int i) {  // selection sort, one step
    for (;;) {
        int best = i;
        for (int j = i + 1; j < list.size; ++j) if (scores[j] > scores[best]) best = j;
        if (see_pending[best]) {
            see_pending[best] = false;
            if (!see_ge(S.board, list.moves[best], 0)) { scores[best] = int16_t(scores[best] - 50000); continue; }
        }
        std::swap(list.moves[i], list.moves[best]);
        std::swap(scores[i], scores[best]);
        std::swap(see_pending[i], see_pending[best]);
        return list.moves[i];
    }
}

void update_history(Move m, int bonus) {  // "gravity": keeps values within +-16384
    int16_t& h = S.history[S.board.stm][from_sq(m)][to_sq(m)];
    h = int16_t(h + bonus - h * std::abs(bonus) / 16384);
}

int qsearch(int alpha, int beta, int ply) {
    ++S.nodes;
    if (should_stop()) return 0;
    if (ply > S.seldepth) S.seldepth = ply;
    if (ply >= MAX_PLY - 1) return eval_at(ply);
    const bool in_check = S.board.in_check();
    int best = -INF;
    if (!in_check) {  // stand pat
        best = corrected(eval_at(ply));
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }
    MoveList list;
    generate(S.board, list, !in_check);  // in check: all evasions
    int16_t scores[MAX_MOVES];
    bool see_pending[MAX_MOVES];
    score_moves(list, scores, see_pending, NO_MOVE, ply);
    int legal = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = pick(list, scores, see_pending, i);
        if (!in_check && scores[i] < 0) break;  // losing captures (SEE < 0, sorted last) can't beat the stand-pat
        if (!S.board.make(m)) continue;
        if (nnue_ready()) nnue_update(S.acc[ply], S.acc[ply + 1], S.board);
        ++legal;
        int score = -qsearch(-beta, -alpha, ply + 1);
        S.board.unmake(m);
        if (S.stopped) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) { alpha = score; if (score >= beta) break; }
        }
    }
    if (in_check && legal == 0) return -MATE + ply;
    return best;
}

int negamax(int alpha, int beta, int depth, int ply, bool null_ok) {
    const bool pv_node = beta - alpha > 1;
    S.pv_len[ply] = ply;
    if (ply > 0) {
        if (S.board.is_draw()) return 0;
        if (ply >= MAX_PLY - 1) return eval_at(ply);
    }
    const bool in_check = S.board.in_check();
    if (in_check) ++depth;  // check extension
    if (depth <= 0) return qsearch(alpha, beta, ply);
    ++S.nodes;
    if (should_stop()) return 0;
    if (ply > S.seldepth) S.seldepth = ply;

    TTEntry tte;
    Move tt_move = NO_MOVE;
    if (g_tt.probe(S.board.key, tte)) {
        tt_move = tte.move;
        const int s = score_from_tt(tte.score, ply);
        if (!pv_node && tte.depth >= depth &&
            (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && s >= beta) || (tte.bound == BOUND_UPPER && s <= alpha)))
            return s;
    }

    // Internal iterative reduction: with no TT move to try first, ordering is weak here; search shallower.
    if (depth >= 4 && tt_move == NO_MOVE) --depth;

    const int raw_eval = in_check ? -INF : eval_at(ply);
    const int static_eval = in_check ? -INF : corrected(raw_eval);
    S.eval_stack[ply] = static_eval;

    // Reverse futility pruning: this far above beta near the leaves, assume the node fails high.
    if (!pv_node && !in_check && depth <= 8 && std::abs(beta) < MATE_BOUND && static_eval - 80 * depth >= beta)
        return static_eval;

    // Null-move pruning: if passing still fails high, this node is very likely a cut-node.
    if (!pv_node && !in_check && null_ok && depth >= 3 && S.board.has_non_pawn_material(S.board.stm)
        && static_eval >= beta) {
        S.board.make_null();
        S.acc[ply + 1] = S.acc[ply];
        int s = -negamax(-beta, -beta + 1, depth - 1 - (3 + depth / 6), ply + 1, false);
        S.board.unmake_null();
        if (S.stopped) return 0;
        if (s >= beta) return s >= MATE_BOUND ? beta : s;
    }

    MoveList list;
    generate(S.board, list, false);
    int16_t scores[MAX_MOVES];
    bool see_pending[MAX_MOVES];
    score_moves(list, scores, see_pending, tt_move, ply);
    Move quiets[64];
    int n_quiets = 0, legal = 0, best = -INF;
    const int alpha0 = alpha;
    Move best_move = NO_MOVE;
    for (int i = 0; i < list.size; ++i) {
        const Move m = pick(list, scores, see_pending, i);
        const bool quiet = !is_capture(m) && !is_promo(m);
        // SEE pruning: near the leaves, skip captures that lose material by force...
        if (!pv_node && !in_check && !quiet && best > -MATE_BOUND && depth <= 8 && !see_ge(S.board, m, -20 * depth * depth))
            continue;
        // ...and quiet moves that hang the moved piece (decided below, after make(), so checks are kept).
        // Futility pruning: a quiet move cannot lift this static eval above alpha so close to the leaves. Checks are
        // kept (they may mate), so the check test runs after make(); a futile move needs no SEE (both prunes need !check).
        const bool futile = !pv_node && !in_check && quiet && best > -MATE_BOUND && depth <= 6 && static_eval + 100 + 100 * depth <= alpha;
        const bool quiet_hangs = !futile && quiet && !pv_node && !in_check && best > -MATE_BOUND && depth <= 8 && !see_ge(S.board, m, -50 * depth);
        const int hist = quiet ? S.history[S.board.stm][from_sq(m)][to_sq(m)] : 0;  // before make(): our side's table
        if (!S.board.make(m)) continue;
        if (futile && !S.board.in_check()) { S.board.unmake(m); continue; }
        if (quiet_hangs && !S.board.in_check()) { S.board.unmake(m); continue; }
        if (nnue_ready()) nnue_update(S.acc[ply], S.acc[ply + 1], S.board);
        ++legal;
        const int new_depth = depth - 1;
        const uint64_t nodes_before = S.nodes;
        int score;
        if (legal == 1) {
            score = -negamax(-beta, -alpha, new_depth, ply + 1, true);
        } else {
            int r = 0;  // late move reductions for quiet, non-checking moves
            if (depth >= 3 && legal > 3 && quiet && !in_check && !S.board.in_check())
                // History-adjusted: moves that often caused cutoffs are reduced less, failures more (+-2 plies).
                r = std::clamp(S.lmr[std::min(depth, 63)][std::min(legal, 63)] - hist / 8192, 0, std::max(0, new_depth - 1));
            score = -negamax(-alpha - 1, -alpha, new_depth - r, ply + 1, true);
            if (score > alpha && r > 0) score = -negamax(-alpha - 1, -alpha, new_depth, ply + 1, true);
            if (score > alpha && score < beta) score = -negamax(-beta, -alpha, new_depth, ply + 1, true);
        }
        S.board.unmake(m);
        if (ply == 0) S.root_nodes[from_sq(m)][to_sq(m)] += S.nodes - nodes_before;
        if (S.stopped) return 0;
        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                S.pv[ply][ply] = m;
                for (int j = ply + 1; j < S.pv_len[ply + 1]; ++j) S.pv[ply][j] = S.pv[ply + 1][j];
                S.pv_len[ply] = std::max(S.pv_len[ply + 1], ply + 1);
                if (score >= beta) {
                    if (quiet) {
                        if (S.killers[ply][0] != m) { S.killers[ply][1] = S.killers[ply][0]; S.killers[ply][0] = m; }
                        const int bonus = std::min(depth * depth, 1200);
                        update_history(m, bonus);
                        for (int q = 0; q < n_quiets; ++q) update_history(quiets[q], -bonus);
                    }
                    break;
                }
            }
        }
        if (quiet && n_quiets < 64) quiets[n_quiets++] = m;
    }
    if (legal == 0) return in_check ? -MATE + ply : 0;
    // Learn from the search result unless the bound says nothing about the eval's error.
    if (!in_check && (best_move == NO_MOVE || !is_capture(best_move)) && std::abs(best) < MATE_BOUND
        && !(best >= beta && best <= static_eval) && !(best <= alpha0 && best >= static_eval)) {
        int16_t& c = S.corr[S.board.stm][pawn_index(S.board)];
        const int w = std::min(depth + 1, 16);
        c = int16_t(std::clamp((c * (256 - w) + (best - raw_eval) * 256 * w) / 256, -256 * 32, 256 * 32));
    }
    g_tt.store(S.board.key, best_move, score_to_tt(best, ply), depth,
               best >= beta ? BOUND_LOWER : best > alpha0 ? BOUND_EXACT : BOUND_UPPER);
    return best;
}

void print_info(int depth, int score) {
    const int64_t ms = now_ms() - S.start;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(S.seldepth) + " score ";
    if (score >= MATE_BOUND) s += "mate " + std::to_string((MATE - score + 1) / 2);
    else if (score <= -MATE_BOUND) s += "mate " + std::to_string(-(MATE + score) / 2);
    else s += "cp " + std::to_string(score);
    s += " nodes " + std::to_string(S.nodes) + " nps " + std::to_string(ms > 0 ? S.nodes * 1000 / uint64_t(ms) : 0)
       + " time " + std::to_string(ms) + " pv";
    for (int i = 0; i < S.pv_len[0]; ++i) s += " " + move_to_uci(S.pv[0][i]);
    write_line(s);
}

}  // namespace

TimeBudget compute_budget(const Limits& l, Color us) {
    const int64_t overhead = g_move_overhead;
    if (l.movetime > 0) { int64_t t = std::max<int64_t>(1, l.movetime - overhead); return {t, t}; }
    if (l.infinite || l.time[us] < 0) return {-1, -1};
    const int64_t t = l.time[us], inc = l.inc[us];
    // Clock Reserve: the budget plans only with the time above it, so a network outage shorter than the reserve cannot
    // flag the Lichess bot. At or below it the engine spends about half the increment, so the clock stays near the
    // reserve (with no increment it plans with half the clock). 0 (default) leaves the budget unchanged.
    const int64_t plan = g_clock_reserve > 0 ? std::max<int64_t>(t - g_clock_reserve, inc ? 0 : t / 2) : t;
    int64_t soft = (l.movestogo > 0 ? plan / (l.movestogo + 1) : plan / 20) + inc / 2;
    // Online play: set Move Overhead above the network lag, or the hard limit (which can reach t - overhead) flags.
    // A quarter-clock cap on top failed non-regression at 8+0.08 (-12.7 +- 6.2, 2026-10-08).
    int64_t hard = std::max<int64_t>(1, std::min(3 * soft, t - overhead));
    return {std::min(soft, hard), hard};
}

void search_init() {
    for (int d = 0; d < 64; ++d)
        for (int m = 0; m < 64; ++m)
            S.lmr[d][m] = (d && m) ? uint8_t(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25) : 0;
}

void clear_search_state() {
    g_tt.clear();
    std::memset(S.history, 0, sizeof S.history);
    std::memset(S.killers, 0, sizeof S.killers);
    std::memset(S.corr, 0, sizeof S.corr);
}

SearchResult search(const Board& root, const Limits& limits, bool verbose) {
    S.board = root;
    if (nnue_ready()) nnue_refresh(S.board, S.acc[0]);
    S.limits = limits;
    S.start = now_ms();
    S.budget = compute_budget(limits, root.stm);
    S.nodes = 0;
    S.stopped = false;
    std::memset(S.killers, 0, sizeof S.killers);
    SearchResult res{NO_MOVE, 0, 0, 0};
    {  // fallback: first legal move, so we always have something to play
        MoveList l;
        generate(S.board, l, false);
        for (int i = 0; i < l.size && res.best == NO_MOVE; ++i)
            if (S.board.make(l.moves[i])) { S.board.unmake(l.moves[i]); res.best = l.moves[i]; }
    }
    if (res.best == NO_MOVE) return res;  // checkmated or stalemated at the root
    int prev_score = 0, stable = 0;
    Move prev_best = NO_MOVE;
    std::memset(S.root_nodes, 0, sizeof S.root_nodes);
    for (int d = 1; d <= limits.depth && d < MAX_PLY - 1; ++d) {
        S.seldepth = 0;
        int delta = 25, alpha = -INF, beta = INF;
        if (d >= 4) { alpha = std::max(prev_score - delta, -INF); beta = std::min(prev_score + delta, INF); }
        int score;
        for (;;) {  // re-search with a wider window until the score lands inside it
            score = negamax(alpha, beta, d, 0, true);
            if (S.stopped) break;
            if (score <= alpha) { beta = (alpha + beta) / 2; alpha = std::max(score - delta, -INF); }
            else if (score >= beta) beta = std::min(score + delta, INF);
            else break;
            delta *= 2;
        }
        const bool complete = !S.stopped;
        if (S.pv_len[0] > 0 && (complete || d == 1)) { res.best = S.pv[0][0]; res.score = score; res.depth = d; }
        if (!complete) break;
        prev_score = score;
        if (verbose) print_info(d, score);
        // Stop sooner when the best move took most of the effort and has stayed best, later otherwise
        // (node-share and stability scaling of the soft limit; the constants are SPSA candidates).
        stable = res.best == prev_best ? std::min(stable + 1, 4) : 0;
        prev_best = res.best;
        if (S.budget.soft >= 0) {
            static constexpr double STABILITY[5] = {1.25, 1.10, 1.00, 0.90, 0.80};
            const double share = double(S.root_nodes[from_sq(res.best)][to_sq(res.best)]) / double(std::max<uint64_t>(S.nodes, 1));
            if (now_ms() - S.start >= int64_t(double(S.budget.soft) * (1.5 - share) * 1.35 * STABILITY[stable])) break;
        }
        if (limits.nodes && S.nodes >= limits.nodes) break;
    }
    res.nodes = S.nodes;
    return res;
}
