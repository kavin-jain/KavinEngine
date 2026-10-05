#pragma once
#include <atomic>
#include "board.h"
#include "tt.h"

struct Limits {
    int64_t time[2] = {-1, -1}, inc[2] = {0, 0};  // ms; time -1 = no clock given
    int movestogo = 0;
    int depth = MAX_PLY - 2;
    int64_t movetime = 0;
    uint64_t nodes = 0;
    bool infinite = false;
};
struct TimeBudget { int64_t soft, hard; };  // ms since search start; -1 = unlimited
TimeBudget compute_budget(const Limits& l, Color us);

struct SearchResult { Move best; int score; int depth; uint64_t nodes; };

extern TT g_tt;
extern std::atomic<bool> g_stop;

void search_init();         // reduction table; call once
void clear_search_state();  // ucinewgame: TT, history, killers
// Searches a copy of `root`. Never returns NO_MOVE if a legal move exists. Prints UCI info lines if verbose.
SearchResult search(const Board& root, const Limits& limits, bool verbose);
