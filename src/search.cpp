#include "search.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#if SEARCH_THREADS_MAX > 1
#include <chrono>
#include <thread>
#include <vector>
#endif
#include "eval.h"
#include "movegen.h"
#include "nnue.h"
#include "platform.h"

TT g_tt;
#ifndef HISTORY_THREATS
#define HISTORY_THREATS 1  // threat-aware quiet history (+14 Elo on PC); the ESP32 build turns it off to save 48 KB
#endif
constexpr int HT = HISTORY_THREATS ? 2 : 1;
#ifndef CORRHIST_SIZE
#define CORRHIST_SIZE 16384  // entries per side; ESP32 builds use a smaller table
#endif
static_assert((CORRHIST_SIZE & (CORRHIST_SIZE - 1)) == 0, "CORRHIST_SIZE must be a power of two");
std::atomic<bool> g_stop{false};
std::atomic<bool> g_pondering{false};
int g_move_overhead = 50;
int g_clock_reserve = 0;
#ifndef DEFAULT_THREADS
#define DEFAULT_THREADS 1
#endif
int g_threads = DEFAULT_THREADS;

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
    int16_t history[2][HT][HT][64][64];  // [side][from attacked][to attacked][from][to]; HT = 1 on the ESP32 (RAM)
    Move pv[MAX_PLY][MAX_PLY];
    uint64_t root_nodes[64][64];  // nodes spent below each root move this search (time management)
    int pv_len[MAX_PLY];
    Accumulator acc[MAX_PLY + 1];  // acc[ply] matches the board at that ply
    int eval_stack[MAX_PLY + 1];   // static eval per ply (-INF when in check)
    int16_t corr[2][CORRHIST_SIZE];  // pawn-structure correction history, scaled by 256
};
// Static storage: these arrays must not live on the small ESP32 task stack. One searcher per thread; S is this
// thread's. The TT is shared (Lazy SMP); node counts of helpers are read racily, for reporting only.
Searcher searchers[SEARCH_THREADS_MAX];
SEARCH_TLS Searcher* S = &searchers[0];
uint8_t LMR[64][64];
std::atomic<bool> g_helpers_stop{false};  // set when the main thread has finished: helpers stop too

uint64_t total_nodes() {
    uint64_t n = 0;
    for (int t = 0; t < std::min(g_threads, SEARCH_THREADS_MAX); ++t) n += searchers[t].nodes;
    return n;
}

// Against a bare king a queen or rook always mates, but the net rates all such positions alike ("won"), so the search
// had no gradient towards mate: a K+Q+2P vs K game on Lichess took 37 moves for a tablebase mate in 6 (2026-10-08).
// There the eval is a known win plus material (PeSTO) plus a mop-up term: the lone king to the edge, our king close
// (chessprogramming.org "Mop-up Evaluation"). The known-win offset makes trading into such an ending attractive.
// Returns 0 (not applicable) otherwise.
constexpr int KNOWN_WIN = 10000;
int bare_king_eval(const Board& b) {
    for (const Color strong : {WHITE, BLACK}) {
        const Color weak = Color(strong ^ 1);
        Bitboard weak_men = 0;
        for (int t = PAWN; t < KING; ++t) weak_men |= b.pieces[make_piece(weak, PieceType(t))];
        if (weak_men || !(b.pieces[make_piece(strong, QUEEN)] | b.pieces[make_piece(strong, ROOK)])) continue;
        const int wk = b.king_sq(weak), sk = b.king_sq(strong);
        const int edge = std::max(3 - (wk & 7), (wk & 7) - 4) + std::max(3 - (wk >> 3), (wk >> 3) - 4);  // 0 centre .. 6 corner
        const int near = 14 - std::abs((wk & 7) - (sk & 7)) - std::abs((wk >> 3) - (sk >> 3));
        const int sign = b.stm == strong ? 1 : -1;
        return sign * (KNOWN_WIN + 50 * edge + 20 * near) + evaluate(b);
    }
    return 0;
}

int eval_at(int ply) {
    if (const int k = bare_king_eval(S->board)) return k;
    const int e = nnue_ready() ? nnue_evaluate(S->acc[ply], S->board.stm, popcount(S->board.occ)) : evaluate(S->board);
    return std::clamp(e, -MATE_BOUND + 1, MATE_BOUND - 1);
}

// Static-evaluation correction history (chessprogramming.org "Static Evaluation Correction History"):
// learns how far the eval misjudges positions with this pawn structure, from search results.
unsigned pawn_index(const Board& b) {
    const uint64_t k = b.pieces[make_piece(WHITE, PAWN)] * 0x9E3779B97F4A7C15ull ^ b.pieces[make_piece(BLACK, PAWN)] * 0xC2B2AE3D27D4EB4Full;
    return unsigned(k >> 40) & (CORRHIST_SIZE - 1);
}

int corrected(int raw) {
    return std::clamp(raw + S->corr[S->board.stm][pawn_index(S->board)] / 256, -MATE_BOUND + 1, MATE_BOUND - 1);
}

bool should_stop() {
    if (S->stopped) return true;
    if (S->limits.nodes && S->nodes >= S->limits.nodes) return S->stopped = true;
    if ((S->nodes & 1023) == 0 &&
        (g_stop.load(std::memory_order_relaxed) || g_helpers_stop.load(std::memory_order_relaxed)
         || (S->budget.hard >= 0 && !g_pondering.load(std::memory_order_relaxed) && now_ms() - S->start >= S->budget.hard)))
        S->stopped = true;
    return S->stopped;
}

// Squares the side `by` attacks. Quiet-move history is kept separately for moves from/to attacked squares: moving a
// threatened piece away, or onto a defended square, is a different idea from the same move without the threat.
Bitboard attacked_by(const Board& b, Color by) {
    const Bitboard* p = b.pieces + int(by) * 6;
    Bitboard a = KingAttacks[lsb(p[KING])], x;
    for (x = p[PAWN]; x;) a |= PawnAttacks[by][pop_lsb(x)];
    for (x = p[KNIGHT]; x;) a |= KnightAttacks[pop_lsb(x)];
    for (x = p[BISHOP] | p[QUEEN]; x;) a |= bishop_attacks(pop_lsb(x), b.occ);
    for (x = p[ROOK] | p[QUEEN]; x;) a |= rook_attacks(pop_lsb(x), b.occ);
    return a;
}

int16_t& history_of(Move m, Bitboard threats) {
    const int f = from_sq(m), t = to_sq(m);
    return S->history[S->board.stm][HISTORY_THREATS ? (threats >> f) & 1 : 0][HISTORY_THREATS ? (threats >> t) & 1 : 0][f][t];
}

// Captures and promotions are scored as if SEE >= 0 and marked pending: pick() runs SEE only when such a move would
// be chosen, and demotes it to the losing-capture band if it fails. Same order as scoring SEE up front, fewer SEE calls.
void score_moves(const MoveList& list, int16_t* scores, bool* see_pending, Move tt_move, int ply, Bitboard threats) {
    for (int i = 0; i < list.size; ++i) {
        const Move m = list.moves[i];
        see_pending[i] = false;
        if (m == tt_move) scores[i] = 30000;
        else if (is_capture(m) || is_promo(m)) {  // MVV-LVA
            int victim = flags_of(m) == EP_CAPTURE ? PAWN : is_capture(m) ? int(type_of(S->board.mailbox[to_sq(m)])) : 0;
            int attacker = type_of(S->board.mailbox[from_sq(m)]);
            int promo = is_promo(m) && promo_type(m) == QUEEN ? 64 : 0;
            scores[i] = int16_t(20000 + victim * 8 - attacker + promo);  // losing captures (SEE < 0) go after quiets: pick()
            see_pending[i] = true;
        } else if (m == S->killers[ply][0]) scores[i] = 19000;
        else if (m == S->killers[ply][1]) scores[i] = 18999;
        else scores[i] = history_of(m, threats);  // bounded to +-16384
    }
}

Move pick(MoveList& list, int16_t* scores, bool* see_pending, int i) {  // selection sort, one step
    for (;;) {
        int best = i;
        for (int j = i + 1; j < list.size; ++j) if (scores[j] > scores[best]) best = j;
        if (see_pending[best]) {
            see_pending[best] = false;
            if (!see_ge(S->board, list.moves[best], 0)) { scores[best] = int16_t(scores[best] - 50000); continue; }
        }
        std::swap(list.moves[i], list.moves[best]);
        std::swap(scores[i], scores[best]);
        std::swap(see_pending[i], see_pending[best]);
        return list.moves[i];
    }
}

void update_history(Move m, int bonus, Bitboard threats) {  // "gravity": keeps values within +-16384
    int16_t& h = history_of(m, threats);
    h = int16_t(h + bonus - h * std::abs(bonus) / 16384);
}

int qsearch(int alpha, int beta, int ply) {
    ++S->nodes;
    if (should_stop()) return 0;
    if (ply > S->seldepth) S->seldepth = ply;
    if (ply >= MAX_PLY - 1) return eval_at(ply);
    // TT: any entry is deep enough here (depth 0); its move also orders the captures.
    const bool pv_node = beta - alpha > 1;
    TTEntry tte;
    Move tt_move = NO_MOVE;
    if (g_tt.probe(S->board.key, tte)) {
        tt_move = tte.move;
        const int s = score_from_tt(tte.score, ply);
        if (!pv_node && (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && s >= beta) || (tte.bound == BOUND_UPPER && s <= alpha)))
            return s;
    }
    const int alpha0 = alpha;
    Move best_move = NO_MOVE;
    const bool in_check = S->board.in_check();
    int best = -INF;
    if (!in_check) {  // stand pat
        best = corrected(eval_at(ply));
        if (best >= beta) return best;
        if (best > alpha) alpha = best;
    }
    MoveList list;
    generate(S->board, list, !in_check);  // in check: all evasions
    int16_t scores[MAX_MOVES];
    bool see_pending[MAX_MOVES];
    score_moves(list, scores, see_pending, tt_move, ply, in_check ? attacked_by(S->board, ~S->board.stm) : 0);  // quiets only in check
    int legal = 0;
    for (int i = 0; i < list.size; ++i) {
        Move m = pick(list, scores, see_pending, i);
        if (!in_check && scores[i] < 0) break;  // losing captures (SEE < 0, sorted last) can't beat the stand-pat
        if (!S->board.make(m)) continue;
        if (nnue_ready()) nnue_update(S->acc[ply], S->acc[ply + 1], S->board);
        ++legal;
        int score = -qsearch(-beta, -alpha, ply + 1);
        S->board.unmake(m);
        if (S->stopped) return 0;
        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) { alpha = score; if (score >= beta) break; }
        }
    }
    if (in_check && legal == 0) return -MATE + ply;
    g_tt.store(S->board.key, best_move, score_to_tt(best, ply), 0,
               best >= beta ? BOUND_LOWER : best > alpha0 ? BOUND_EXACT : BOUND_UPPER);
    return best;
}

// cutnode: a zero-window node expected to fail high (the parent's reduced or null-move search); reduced harder.
int negamax(int alpha, int beta, int depth, int ply, bool null_ok, bool cutnode, Move excluded = NO_MOVE) {
    const bool pv_node = beta - alpha > 1;
    S->pv_len[ply] = ply;
    if (ply > 0) {
        if (S->board.is_draw(ply)) return 0;
        if (ply >= MAX_PLY - 1) return eval_at(ply);
    }
    const bool in_check = S->board.in_check();
    if (in_check) ++depth;  // check extension
    if (depth <= 0) return qsearch(alpha, beta, ply);
    ++S->nodes;
    if (should_stop()) return 0;
    if (ply > S->seldepth) S->seldepth = ply;

    TTEntry tte;
    Move tt_move = NO_MOVE;
    const bool tt_hit = g_tt.probe(S->board.key, tte);
    if (tt_hit) {
        tt_move = tte.move;
        const int s = score_from_tt(tte.score, ply);
        if (!pv_node && excluded == NO_MOVE && tte.depth >= depth &&
            (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && s >= beta) || (tte.bound == BOUND_UPPER && s <= alpha)))
            return s;
    }

    // Internal iterative reduction: with no TT move to try first, ordering is weak here; search shallower.
    if (depth >= 4 && tt_move == NO_MOVE) --depth;

    const int raw_eval = in_check ? -INF : eval_at(ply);
    const int static_eval = in_check ? -INF : corrected(raw_eval);
    S->eval_stack[ply] = static_eval;

    // Reverse futility pruning: this far above beta near the leaves, assume the node fails high.
    if (!pv_node && !in_check && excluded == NO_MOVE && depth <= 8 && std::abs(beta) < MATE_BOUND && static_eval - 80 * depth >= beta)
        return static_eval;

    // Null-move pruning: if passing still fails high, this node is very likely a cut-node.
    if (!pv_node && !in_check && null_ok && excluded == NO_MOVE && depth >= 3 && S->board.has_non_pawn_material(S->board.stm)
        && static_eval >= beta) {
        S->board.make_null();
        S->acc[ply + 1] = S->acc[ply];
        int s = -negamax(-beta, -beta + 1, depth - 1 - (3 + depth / 6), ply + 1, false, !cutnode);
        S->board.unmake_null();
        if (S->stopped) return 0;
        if (s >= beta) return s >= MATE_BOUND ? beta : s;
    }

    // Singular extension: if every move but the TT move fails low against a margin below the TT score, the TT move
    // is the only good one here, so search it one ply deeper. If another move also beats beta, two moves refute this
    // node and it can be cut (multi-cut). If another move is nearly as good and the TT score already beats beta, the TT
    // move is searched one ply shallower (negative extension).
    int extension = 0;
    if (ply > 0 && excluded == NO_MOVE && depth >= 8 && tt_hit && tt_move != NO_MOVE && tte.depth >= depth - 3
        && (tte.bound & BOUND_LOWER) && std::abs(score_from_tt(tte.score, ply)) < MATE_BOUND) {
        const int singular_beta = score_from_tt(tte.score, ply) - 2 * depth;
        const int s = negamax(singular_beta - 1, singular_beta, (depth - 1) / 2, ply, false, cutnode, tt_move);
        if (S->stopped) return 0;
        if (s < singular_beta) extension = 1;
        else if (singular_beta >= beta) return singular_beta;
        else if (score_from_tt(tte.score, ply) >= beta) extension = -1;
    }

    MoveList list;
    generate(S->board, list, false);
    int16_t scores[MAX_MOVES];
    bool see_pending[MAX_MOVES];
    const Bitboard threats = attacked_by(S->board, ~S->board.stm);
    score_moves(list, scores, see_pending, tt_move, ply, threats);
    Move quiets[64];
    int n_quiets = 0, legal = 0, best = -INF;
    const int alpha0 = alpha;
    Move best_move = NO_MOVE;
    for (int i = 0; i < list.size; ++i) {
        const Move m = pick(list, scores, see_pending, i);
        if (m == excluded) continue;
        const bool quiet = !is_capture(m) && !is_promo(m);
        // SEE pruning: near the leaves, skip captures that lose material by force...
        if (!pv_node && !in_check && !quiet && best > -MATE_BOUND && depth <= 8 && !see_ge(S->board, m, -20 * depth * depth))
            continue;
        // ...and quiet moves that hang the moved piece (decided below, after make(), so checks are kept).
        // Futility pruning: a quiet move cannot lift this static eval above alpha so close to the leaves. Checks are
        // kept (they may mate), so the check test runs after make(); a futile move needs no SEE (both prunes need !check).
        const bool futile = !pv_node && !in_check && quiet && best > -MATE_BOUND && depth <= 6 && static_eval + 100 + 100 * depth <= alpha;
        const bool quiet_hangs = !futile && quiet && !pv_node && !in_check && best > -MATE_BOUND && depth <= 8 && !see_ge(S->board, m, -50 * depth);
        if (!S->board.make(m)) continue;
        if (futile && !S->board.in_check()) { S->board.unmake(m); continue; }
        if (quiet_hangs && !S->board.in_check()) { S->board.unmake(m); continue; }
        if (nnue_ready()) nnue_update(S->acc[ply], S->acc[ply + 1], S->board);
        ++legal;
        const int new_depth = depth - 1 + (m == tt_move ? extension : 0);
        const uint64_t nodes_before = S->nodes;
        int score;
        if (legal == 1) {
            score = -negamax(-beta, -alpha, new_depth, ply + 1, true, !pv_node && !cutnode);
        } else {
            int r = 0;  // late move reductions for quiet, non-checking moves
            if (depth >= 3 && legal > 3 && quiet && !in_check && !S->board.in_check())
                r = std::max(0, std::min<int>(LMR[std::min(depth, 63)][std::min(legal, 63)] + cutnode, new_depth - 1));
            score = -negamax(-alpha - 1, -alpha, new_depth - r, ply + 1, true, true);
            if (score > alpha && r > 0) score = -negamax(-alpha - 1, -alpha, new_depth, ply + 1, true, !cutnode);
            if (score > alpha && score < beta) score = -negamax(-beta, -alpha, new_depth, ply + 1, true, false);
        }
        S->board.unmake(m);
        if (ply == 0) S->root_nodes[from_sq(m)][to_sq(m)] += S->nodes - nodes_before;
        if (S->stopped) return 0;
        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                S->pv[ply][ply] = m;
                for (int j = ply + 1; j < S->pv_len[ply + 1]; ++j) S->pv[ply][j] = S->pv[ply + 1][j];
                S->pv_len[ply] = std::max(S->pv_len[ply + 1], ply + 1);
                if (score >= beta) {
                    if (quiet) {
                        if (S->killers[ply][0] != m) { S->killers[ply][1] = S->killers[ply][0]; S->killers[ply][0] = m; }
                        const int bonus = std::min(depth * depth, 1200);
                        update_history(m, bonus, threats);
                        for (int q = 0; q < n_quiets; ++q) update_history(quiets[q], -bonus, threats);
                    }
                    break;
                }
            }
        }
        if (quiet && n_quiets < 64) quiets[n_quiets++] = m;
    }
    if (legal == 0) return excluded != NO_MOVE ? alpha : in_check ? -MATE + ply : 0;
    // Learn from the search result unless the bound says nothing about the eval's error.
    if (excluded != NO_MOVE) return best;  // a partial search: no TT entry, no correction learning
    if (!in_check && (best_move == NO_MOVE || !is_capture(best_move)) && std::abs(best) < MATE_BOUND
        && !(best >= beta && best <= static_eval) && !(best <= alpha0 && best >= static_eval)) {
        int16_t& c = S->corr[S->board.stm][pawn_index(S->board)];
        const int w = std::min(depth + 1, 16);
        c = int16_t(std::clamp((c * (256 - w) + (best - raw_eval) * 256 * w) / 256, -256 * 32, 256 * 32));
    }
    g_tt.store(S->board.key, best_move, score_to_tt(best, ply), depth,
               best >= beta ? BOUND_LOWER : best > alpha0 ? BOUND_EXACT : BOUND_UPPER);
    return best;
}

void print_info(int depth, int score) {
    const int64_t ms = now_ms() - S->start;
    std::string s = "info depth " + std::to_string(depth) + " seldepth " + std::to_string(S->seldepth) + " score ";
    if (score >= MATE_BOUND) s += "mate " + std::to_string((MATE - score + 1) / 2);
    else if (score <= -MATE_BOUND) s += "mate " + std::to_string(-(MATE + score) / 2);
    else s += "cp " + std::to_string(score);
    const uint64_t nodes = total_nodes();
    s += " nodes " + std::to_string(nodes) + " nps " + std::to_string(ms > 0 ? nodes * 1000 / uint64_t(ms) : 0)
       + " time " + std::to_string(ms) + " pv";
    for (int i = 0; i < S->pv_len[0]; ++i) s += " " + move_to_uci(S->pv[0][i]);
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
    int64_t soft = (l.movestogo > 0 ? plan / (l.movestogo + 1) : plan / 30) + inc / 2;
    // Online play: set Move Overhead above the network lag, or the hard limit (which can reach t - overhead) flags.
    // A quarter-clock cap on top failed non-regression at 8+0.08 (-12.7 +- 6.2, 2026-10-08).
    int64_t hard = std::max<int64_t>(1, std::min(3 * soft, t - overhead));
    return {std::min(soft, hard), hard};
}

void search_init() {
    for (int d = 0; d < 64; ++d)
        for (int m = 0; m < 64; ++m)
            LMR[d][m] = (d && m) ? uint8_t(0.75 + std::log(double(d)) * std::log(double(m)) / 2.25) : 0;
}

void clear_search_state() {
    g_tt.clear();
    for (Searcher& t : searchers) {
        std::memset(t.history, 0, sizeof t.history);
        std::memset(t.killers, 0, sizeof t.killers);
        std::memset(t.corr, 0, sizeof t.corr);
    }
}

static SearchResult iterate(const Board& root, const Limits& limits, bool verbose) {
    S->board = root;
    if (nnue_ready()) nnue_refresh(S->board, S->acc[0]);
    S->limits = limits;
    S->start = now_ms();
    S->budget = compute_budget(limits, root.stm);
    S->nodes = 0;
    S->stopped = false;
    std::memset(S->killers, 0, sizeof S->killers);
    SearchResult res{NO_MOVE, 0, 0, 0};
    {  // fallback: first legal move, so we always have something to play
        MoveList l;
        generate(S->board, l, false);
        for (int i = 0; i < l.size && res.best == NO_MOVE; ++i)
            if (S->board.make(l.moves[i])) { S->board.unmake(l.moves[i]); res.best = l.moves[i]; }
    }
    if (res.best == NO_MOVE) return res;  // checkmated or stalemated at the root
    int prev_score = 0, stable = 0;
    Move prev_best = NO_MOVE;
    std::memset(S->root_nodes, 0, sizeof S->root_nodes);
    for (int d = 1; d <= limits.depth && d < MAX_PLY - 1; ++d) {
        S->seldepth = 0;
        int delta = 25, alpha = -INF, beta = INF;
        if (d >= 4) { alpha = std::max(prev_score - delta, -INF); beta = std::min(prev_score + delta, INF); }
        int score;
        for (;;) {  // re-search with a wider window until the score lands inside it
            score = negamax(alpha, beta, d, 0, true, false);
            if (S->stopped) break;
            if (score <= alpha) { beta = (alpha + beta) / 2; alpha = std::max(score - delta, -INF); }
            else if (score >= beta) beta = std::min(score + delta, INF);
            else break;
            delta *= 2;
        }
        const bool complete = !S->stopped;
        if (S->pv_len[0] > 0 && (complete || d == 1)) { res.best = S->pv[0][0]; res.score = score; res.depth = d; }
        if (!complete) break;
        prev_score = score;
        if (verbose) print_info(d, score);
        // Stop sooner when the best move took most of the effort and has stayed best, later otherwise
        // (node-share and stability scaling of the soft limit; the constants are SPSA candidates).
        stable = res.best == prev_best ? std::min(stable + 1, 4) : 0;
        prev_best = res.best;
        if (S->budget.soft >= 0 && !g_pondering) {
            static constexpr double STABILITY[5] = {1.25, 1.10, 1.00, 0.90, 0.80};
            const double share = double(S->root_nodes[from_sq(res.best)][to_sq(res.best)]) / double(std::max<uint64_t>(S->nodes, 1));
            if (now_ms() - S->start >= int64_t(double(S->budget.soft) * (1.5 - share) * 1.35 * STABILITY[stable])) break;
        }
        if (limits.nodes && S->nodes >= limits.nodes) break;
    }
    // A ponder search may not answer before ponderhit or stop (UCI), even when it has run out of depth. The clock
    // starts at "go ponder", so after a ponderhit the time already spent counts and the hard limit may stop at once.
#if SEARCH_THREADS_MAX > 1  // pondering needs a second thread to read "ponderhit"; single-thread builds never ponder
    while (limits.ponder && g_pondering && !g_stop) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
    res.nodes = S->nodes;
    return res;
}

// The reply we expect to res.best, for "bestmove ... ponder": the PV's second move, else the TT move after it.
static Move expected_reply(const Board& root, Move best) {
    if (S->pv_len[0] > 1 && S->pv[0][0] == best) return S->pv[0][1];
    Board b = root;
    if (!b.make(best)) return NO_MOVE;
    TTEntry tte;
    if (!g_tt.probe(b.key, tte) || tte.move == NO_MOVE) return NO_MOVE;
    MoveList l;
    generate(b, l, false);
    for (int i = 0; i < l.size; ++i)
        if (l.moves[i] == tte.move && b.make(tte.move)) { b.unmake(tte.move); return tte.move; }
    return NO_MOVE;
}

// Lazy SMP: helpers run the same iterative deepening with no clock and share the TT; the main thread alone manages
// time and returns its own best move. Threads = 1 (default) runs no helpers.
SearchResult search(const Board& root, const Limits& limits, bool verbose) {
    S = &searchers[0];
#if SEARCH_THREADS_MAX > 1
    g_helpers_stop = false;
    Limits helper = limits;
    helper.time[WHITE] = helper.time[BLACK] = -1;
    helper.movetime = 0;
    helper.nodes = 0;
    helper.infinite = true;
    helper.ponder = false;  // only the main thread waits for ponderhit
    std::vector<std::thread> helpers;
    for (int t = 1; t < std::min(g_threads, SEARCH_THREADS_MAX); ++t) {
        searchers[t].nodes = 0;  // before the thread starts, so total_nodes() never counts an old search
        helpers.emplace_back([&root, helper, t] { S = &searchers[t]; iterate(root, helper, false); });
    }
#endif
    SearchResult res = iterate(root, limits, verbose);
    res.ponder = expected_reply(root, res.best);
#if SEARCH_THREADS_MAX > 1
    g_helpers_stop = true;
    for (std::thread& h : helpers) h.join();
    g_helpers_stop = false;
    res.nodes = total_nodes();
#endif
    return res;
}
